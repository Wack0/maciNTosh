// This is the ARC loader.
// Its purpose is to load the ARC firmware and jump to its entry point in little endian mode passing it hardware information.
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include "arc.h"
#include "ofapi.h"
#include "elf_abi.h"
#include "hwdesc.h"

// This is the default gamma table set up in the DAC by the ATI OS9 drivers.
// Every driver contains 4 seperate tables, this is the default one used;
// an identical copy of the tables is present in each driver.
static const UCHAR sc_GammaTable[] = {
	0x00, 0x05, 0x09, 0x0B, 0x0E, 0x10, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1E, 0x20, 0x22, 0x24,
	0x25, 0x27, 0x28, 0x2A, 0x2C, 0x2D, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x36, 0x37, 0x38, 0x3A, 0x3B,
	0x3C, 0x3E, 0x3F, 0x40, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x4F,
	0x50, 0x51, 0x52, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5E, 0x5F, 0x60, 0x61,
	0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
	0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81,
	0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8C, 0x8D, 0x8E, 0x8F,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9B, 0x9C, 0x9D,
	0x9E, 0x9F, 0xA0, 0xA1, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
	0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
	0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC0, 0xC1, 0xC2, 0xC3, 0xC3, 0xC4,
	0xC5, 0xC6, 0xC7, 0xC7, 0xC8, 0xC9, 0xCA, 0xCA, 0xCB, 0xCC, 0xCD, 0xCD, 0xCE, 0xCF, 0xD0, 0xD0,
	0xD1, 0xD2, 0xD3, 0xD3, 0xD4, 0xD5, 0xD6, 0xD6, 0xD7, 0xD8, 0xD9, 0xD9, 0xDA, 0xDB, 0xDC, 0xDC,
	0xDD, 0xDE, 0xDF, 0xDF, 0xE0, 0xE1, 0xE1, 0xE2, 0xE3, 0xE4, 0xE4, 0xE5, 0xE6, 0xE7, 0xE7, 0xE8,
	0xE9, 0xE9, 0xEA, 0xEB, 0xEC, 0xEC, 0xED, 0xEE, 0xEE, 0xEF, 0xF0, 0xF1, 0xF1, 0xF2, 0xF3, 0xF3,
	0xF4, 0xF5, 0xF5, 0xF6, 0xF7, 0xF8, 0xF8, 0xF9, 0xFA, 0xFA, 0xFB, 0xFC, 0xFC, 0xFD, 0xFE, 0xFF,
};

static OFIHANDLE s_stdout = OFINULL;
static ULONG s_FirstFreePage;
static ULONG s_LastFreePage;
static ULONG s_PhysMemLength;
static ULONG s_MacIoStart;
static ULONG s_MrpFlags = 0;

//FILE* stdout = NULL;

static bool InitStdout(void) {
	OFHANDLE Handle = OfGetChosen();
	return ARC_SUCCESS(OfGetPropInt(Handle, "stdout", &s_stdout));
}

static void StdOutWrite(const char* str) {
	if (s_stdout == OFINULL) return;
	if (str[0] == 0) return;
	ULONG Count;
	OfWrite(s_stdout, str, strlen(str), &Count);
}

#if 0 // using printf uses ~2.5kb, we need every byte we can get to meet self-imposed 16kb limit
static void print_f(const char* fmt, ...) {
	if (s_stdout == OFINULL) return;
	va_list va;
	va_start(va, fmt);
	char Buffer[256];
	vsnprintf(Buffer, sizeof(Buffer), fmt, va);
	va_end(va);
	StdOutWrite(Buffer);
}
#endif

// comes from https://stackoverflow.com/questions/21501815/optimal-base-10-only-itoa-function
static char * _i32toa(char *const rtn, int i)    {
    if (NULL == rtn) return NULL;

    // declare local buffer, and write to it back-to-front
    char buff[12];
    uint32_t  ut, ui;
    char minus_sign=0;
    char *p = buff + sizeof(buff)-1;
    *p-- = 0;    // nul-terminate buffer

    // deal with negative numbers while using an unsigned integer
    if (i < 0)    {
        minus_sign = '-';
        ui = (uint32_t)((int)-1 * (int)i);
    }    else    {
        ui = i;
    }

    // core code here...
    while (ui > 9) {
        ut = ui;
        ui /= 10;
        *p-- = (ut - (ui * 10)) + '0';
    }
    *p = ui + '0';

    if ('-' == minus_sign) *--p = minus_sign;

    // knowing how much storage we needed, copy chars from buff to rtn...
    memcpy(rtn, p, sizeof(buff)-(p - buff));

    return rtn;
}

static void print_error(int error) {
	char buffer[12];
	_i32toa(buffer, error);
	StdOutWrite(" (");
	StdOutWrite(buffer);
	StdOutWrite(")\r\n");
}

static ULONG MempGetKernelRamSize(OFHANDLE Handle) {

	// Get all memory.
	OF_REGISTER Memory[16];
	ULONG MemoryCount = 16;
	if (ARC_FAIL(OfGetPropRegs(Handle, "reg", Memory, &MemoryCount))) {
		return 0;
	}

	ULONG BasePage = 0;
	ULONG PhysSize = 0;

	for (ULONG i = 0; i < MemoryCount; i++) {
		ULONG ThisBasePage = Memory[i].PhysicalAddress.LowPart / PAGE_SIZE;
		ULONG PageCount = Memory[i].Size / PAGE_SIZE;
		// Ensure RAM is mapped consecutively for loading the NT kernel. NT kernel can use RAM mapped elsewhere, however.
		if (ThisBasePage != BasePage) {
			// Handle empty RAM slots.
			if (ThisBasePage == 0 && PageCount == 0) continue;
			break;
		}
		BasePage = ThisBasePage + PageCount;
		PhysSize += Memory[i].Size;
	}

	s_PhysMemLength = PhysSize;

	return PhysSize;
}

/// <summary>
/// Maps physical memory to virtual addresses that NT relies on.
/// </summary>
/// <returns>true if succeeded</returns>
int ArcMemInitNtMapping(void) {
	// This loader is for grackle.
	// Grackle can properly switch endianness for the PCI bus.
	// NT runs on systems with this memory controller.
	// Thus, we can kick open firmware out of the way and load our own little endian ARC firmware for these systems.

	// Get OF memory.
	OFHANDLE Chosen = OfGetChosen();
	OFHANDLE Memory;
	{
		OFIHANDLE iMemory;
		if (ARC_FAIL(OfGetPropInt(Chosen, "memory", &iMemory))) {
			return -1;
		}
		Memory = OfInstanceToPackage(iMemory);
		if (Memory == OFNULL) {
			return -2;
		}
	}

	// Find the RAM size:
	ULONG PhysMemSize = MempGetKernelRamSize(Memory);
	ULONG NtMapSize = PhysMemSize;
	//ULONG PhysMemMb = PhysMemSize / (1024 * 1024);
	// Assume that physical memory is mapped consecutively. This isn't the Wii.
	// Store the memory size off for later.
	s_PhysMemLength = NtMapSize;
	if (NtMapSize == 0) return -3;
	// Are we under emulation? Official Apple OF does have "copyright" property at / - openbios does not
	// This is probably a terrible way of doing this, but works for me.
	bool Emulated = false;
	{
		OFHANDLE Handle = OfChild(OFNULL);
		Emulated = !OfPropExists(Handle, "copyright");
	}
	if (Emulated) s_MrpFlags |= MRP_IN_EMULATOR;


	s_LastFreePage = NtMapSize / PAGE_SIZE;

	// Theoretically, we can check to see what's mapped;
	// Practically, stage2 has a hardcoded load address anyway.
	ULONG FirstFreeAddress = 0x100000;
	s_FirstFreePage = FirstFreeAddress / PAGE_SIZE;

	// Now everything is known, grab the MMU:
	OFIHANDLE Mmu = OFINULL;
	if (ARC_FAIL(OfGetPropInt(Chosen, "mmu", &Mmu))) {
		return -4;
	}
	if (Mmu == OFINULL) return -4;

	ARC_STATUS Status = _ESUCCESS;

	// Also grab the mac-io bridge so its register space can be obtained.
	OFHANDLE MacIo = OfFindDevice("mac-io");
	//ULONG MacIoStart = 0;
	if (MacIo != OFINULL) {
		// Got it
		OF_REGISTER Register;
		if (ARC_FAIL(OfGetPropReg(MacIo, "assigned-addresses", 0, &Register))) return -4;
		ULONG Start = Register.PhysicalAddress.LowPart;
		s_MacIoStart = Start;
		//ULONG End = Start + Register.Size;
		// Set the free addresses for a grackle system.
		NtMapSize = PhysMemSize;
		s_LastFreePage = NtMapSize / PAGE_SIZE;
		s_FirstFreePage = 0;
		FirstFreeAddress = 0;
		// Start is either at 0x8000_0000, or 0x8080_0000.
		// For the former, this is a uni-north system, so panic (this version of the loader is for grackle systems)
		// For anything else, this is probably qemu.
		if (Start == 0x80800000) {
			// This is a grackle system.
		}
		else if (Start == 0x80000000) {
			// this is a uni-north system, this loader is for grackle:
			StdOutWrite("This is a uni-north system, you are using the wrong files (this is for grackle)\r\n");
			OfExit();
			return -4;
		}
		else {
			// mac-io should not be mapped here
			if (Emulated) {
				// this is probably an emulated grackle + heathrow system, which maps framebuffer at 0x80000000
				// qemu doesn't emulate any little endian mode either too.
				OFHANDLE Screen = OfFindDevice("screen");
				ULONG FbAddr = 0;
				if (ARC_SUCCESS(OfGetPropInt(Screen, "address", &FbAddr))) {
					if (FbAddr == 0x80000000) {
						StdOutWrite("STICKY FRAMEBUFFER at 0x80000000\r\n");
						StdOutWrite("this is probably qemu g3beige\r\n");
						StdOutWrite("Continuing, but don't expect anything unless your qemu version has little endian support...\r\n");
					}
				}
			} else {
				//print_f("mac-io at %08x-%08x\r\n", Start, End);
				StdOutWrite("incompatible, unknown system\r\n");
				OfExit();
				return -4;
			}
		}
	}
	else return -4; // temp
	
	// Claim the lowest memory (exception handler space), if for some reason not already done
	OfClaim((PVOID)NULL, 0x4000, 0);

	// Claim up to where we start
	extern UCHAR __text_start[];
	extern UCHAR _end[];
	if (OfClaim((PVOID)0x4000, (ULONG)(__text_start) - 0x4000, 0) == NULL) return -5;
	// Claim after we end, up to 16MB or end of RAM
	ULONG LastAddress = 0x1000000;
	if (s_LastFreePage < (LastAddress / PAGE_SIZE)) LastAddress = s_LastFreePage * PAGE_SIZE;
	ULONG EndPage = (ULONG)_end;
	if ((EndPage & (PAGE_SIZE - 1)) != 0) {
		EndPage += PAGE_SIZE;
		EndPage &= ~(PAGE_SIZE - 1);
		// extra 32KB is used here, at least on lombard?
		EndPage += 0x8000;
	}
	if (OfClaim((PVOID)EndPage, LastAddress - EndPage, 0) == NULL) return -5;
	// Map first 16MB (or whatever) of RAM
	Status = OfCallMethod(4, 0, NULL, "map", Mmu, 0, LastAddress, 0);
	if (ARC_FAIL(Status)) return -5;
	// Map in the Grackle PCI configuration space
	Status = OfCallMethod(4, 0, NULL, "map", Mmu, 0xFEC00000, 0xFEC00000, 0x00400000, 0x28);
	if (ARC_FAIL(Status)) return -5;

	// Everything should be mapped fine now.
	return 0;
}

static int ElfValid(void* addr) {
	Elf32_Ehdr* ehdr; /* Elf header structure pointer */

	ehdr = (Elf32_Ehdr*)addr;

	if (!IS_ELF(*ehdr))
		return 0;

	if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
		return -1;

	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
		return -1;

	if (ehdr->e_ident[EI_VERSION] != EV_CURRENT)
		return -1;

	if (ehdr->e_type != ET_EXEC)
		return -1;

	if (ehdr->e_machine != EM_PPC)
		return -1;

	return 1;
}

static void sync_after_write(const void* pv, ULONG len)
{
	ULONG a, b;

	const void* p = (const void*)((ULONG)pv & ~0x80000000);

	a = (ULONG)p & ~0x1f;
	b = ((ULONG)p + len + 0x1f) & ~0x1f;

	for (; a < b; a += 32)
		asm("dcbst 0,%0" : : "b"(a));

	asm("sync ; isync");
}

static void sync_before_exec(const void* pv, ULONG len)
{
	ULONG a, b;

	const void* p = (const void*)((ULONG)pv & ~0x80000000);

	a = (ULONG)p & ~0x1f;
	b = ((ULONG)p + len + 0x1f) & ~0x1f;

	for (; a < b; a += 32)
		asm("dcbst 0,%0 ; sync ; icbi 0,%0" : : "b"(a));

	asm("sync ; isync");
}

static void MsrLeSwap64Single(ULONG* dest32, ULONG* src32) {
	ULONG temp = src32[1];
	dest32[1] = __builtin_bswap32(src32[0]);
	dest32[0] = __builtin_bswap32(temp);
}

static void MsrLeSwap64(void* dest, const void* src, ULONG len, ULONG memlen) {
	uint64_t* dest64 = (uint64_t*)dest;
	uint64_t* src64 = (uint64_t*)src;
	
	// align swap-len to 64 bits.
	if ((len & 7) != 0) len += 8 - (len & 7);
	for (; len != 0; dest64++, src64++, len -= sizeof(*dest64), memlen -= sizeof(*dest64)) {
		ULONG* dest32 = (ULONG*)dest64;
		if (len < sizeof(*dest64)) {
			uint64_t val64 = *src64 & ((1 << (len * 8)) - 1);
			ULONG* val32 = (ULONG*)&val64;
			MsrLeSwap64Single(dest32, val32);
			continue;
		}
		ULONG* src32 = (ULONG*)src64;
		MsrLeSwap64Single(dest32, src32);
	}
	
	if ((memlen & 7) != 0) memlen += 8 - (memlen & 7);
	for (; memlen > 0; dest64++, memlen -= sizeof(*dest64)) {
		*dest64 = 0;
	}
}

static void MsrLeMunge32(void* ptr, ULONG len) {
	ULONG* ptr32 = (ULONG*)ptr;
	
	for (; len > 0; len -= sizeof(uint64_t), ptr32 += 2) {
		ULONG temp = ptr32[0];
		ptr32[0] = ptr32[1];
		ptr32[1] = temp;
	}
}

static ULONG ElfLoad(void* addr) {
	Elf32_Ehdr* ehdr;
	Elf32_Phdr* phdrs;
	UCHAR* image;
	int i;

	ehdr = (Elf32_Ehdr*)addr;

	if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
		//StdOutWrite("ELF has no phdrs\r\n");
		return 0;
	}

	if (ehdr->e_phentsize != sizeof(Elf32_Phdr)) {
		//StdOutWrite("Invalid ELF phdr size\r\n");
		return 0;
	}

	phdrs = (Elf32_Phdr*)(addr + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdrs[i].p_type != PT_LOAD) {
			//print_f("skip PHDR %d of type %d\r\n", i, phdrs[i].p_type);
			continue;
		}

		// translate paddr to this BAT setup
		phdrs[i].p_paddr &= 0x3FFFFFFF;
		//phdrs[i].p_paddr |= 0x80000000;

#if 0
		print_f("PHDR %d 0x%08x [0x%x] -> 0x%08x [0x%x] <", i,
			phdrs[i].p_offset, phdrs[i].p_filesz,
			phdrs[i].p_paddr, phdrs[i].p_memsz);

		if (phdrs[i].p_flags & PF_R)
			print_f("R");
		if (phdrs[i].p_flags & PF_W)
			print_f("W");
		if (phdrs[i].p_flags & PF_X)
			print_f("X");
		print_f(">\r\n");
#endif

		if (phdrs[i].p_filesz > phdrs[i].p_memsz) {
			//print_f("-> file size > mem size\r\n");
			return 0;
		}

		if (phdrs[i].p_filesz) {
			//print_f("-> load 0x%x\r\n", phdrs[i].p_filesz);
			image = (UCHAR*)(addr + phdrs[i].p_offset);
			MsrLeSwap64(
				(void*)(phdrs[i].p_paddr),
				(const void*)image,
				phdrs[i].p_filesz,
				phdrs[i].p_memsz
			);
			memset((void*)image, 0, phdrs[i].p_filesz);

			if (phdrs[i].p_flags & PF_X)
				sync_before_exec((void*)phdrs[i].p_paddr, phdrs[i].p_memsz);
			else
				sync_after_write((void*)phdrs[i].p_paddr, phdrs[i].p_memsz);
		}
		else {
			//print_f("-> skip\r\n");
			memset((void*)phdrs[i].p_paddr + phdrs[i].p_filesz, 0, phdrs[i].p_memsz - phdrs[i].p_filesz);
		}
	}

	// fix the ELF entrypoint to physical address
	ULONG EntryPoint = ehdr->e_entry;
	EntryPoint &= 0x3fffffff;
	return EntryPoint;
}

static OFIHANDLE s_Screen = OFINULL;
static ULONG s_OldDepth = 0, s_CurrDepth = 0;

static bool FbSetDepthByFcode(OFHANDLE Screen) {
	// Open the screen.
	char Path[1024];
	ULONG PathLength = sizeof(Path);
	if (ARC_FAIL(OfPackageToPath(Screen, Path, &PathLength))) return false;

	s_Screen = OfOpen(Path);
	if (s_Screen == OFINULL) return false;

	// Get the current screen depth.
	if (ARC_FAIL(OfGetPropInt(Screen, "depth", &s_OldDepth))) return false;

	

	s_CurrDepth = s_OldDepth;

	// If already 32 colours, don't need to do anything
	if (s_OldDepth == 32) return true;

	// Try to set to 32 colours, don't check return value
	// BUGBUG: only ATI cards' fcode ROMs support this, nvidia cards do not
	// Figure out a way to change colour depth on nvidia cards.
	OfCallMethod(1, 0, NULL, "set-depth", s_Screen, 32);

	do {
		// Get the screen depth after setting to 32 colours
		if (ARC_FAIL(OfGetPropInt(Screen, "depth", &s_CurrDepth))) break;

		// If successfully set depth to 32bpp, done.
		if (s_CurrDepth == 32) return true;
	} while (0);

	// Revert changes
	OfCallMethod(1, 0, NULL, "set-depth", s_Screen, s_OldDepth);
	s_CurrDepth = s_OldDepth;
	// Disallow anything not 32bpp
	if (s_OldDepth != 32) return false;
	return true;
}

static void FbRestoreDepthByFcode(void) {
	// Do nothing if original depth is equal to current
	if (s_OldDepth == s_CurrDepth) return;

	// Set the requested depth
	OfCallMethod(1, 0, NULL, "set-depth", s_Screen, s_OldDepth);
}

static ULONG RoundDiv(ULONG lhs, ULONG rhs) {
	return ((lhs+(rhs/2))/rhs);
}

static int freq(int a1, int a2, int a3, int a4, int a5) {
	double v5 = (double)(a5 * a1);
	if (v5 == 0.0) return 0;
	else return (int)((double)(a3 * a4 * a2) / v5);
}

static bool FbSetDdaRage4(ULONG BaseAddress) {
	// let's try doing what linux does here:
	// get the timings
	static const UCHAR PostDivSet[] = { 0, 1, 2, 4, 8, 3, 6, 12 };
	static const UCHAR TableOs9Driver[] = { 1, 2, 4, 8, 3, 0, 6, 0xC };
	ULONG x_mpll_ref_fb_div;
	ULONG xclk_cntl;
	ULONG Nx, M;
	
	ULONG ref_clk = 2950;
	
	volatile UCHAR* CLOCK_CNTL_INDEX = (volatile UCHAR*)(BaseAddress + 0x8);
	volatile U32LE* CLOCK_CNTL_DATA = (volatile U32LE*)(BaseAddress + 0xC);
	
	CLOCK_CNTL_INDEX[0] = 0x0A;
	__asm__ volatile ("eieio");
	x_mpll_ref_fb_div = CLOCK_CNTL_DATA->v;
	CLOCK_CNTL_INDEX[0] = 0x0D;
	__asm__ volatile ("eieio");
	xclk_cntl = CLOCK_CNTL_DATA->v & 0x7;
	
	CLOCK_CNTL_INDEX[0] = 0x03;
	__asm__ volatile ("eieio");
	ULONG pll_3 = CLOCK_CNTL_DATA->v & 0x3ff;
	ULONG vclk_div = CLOCK_CNTL_INDEX[1] & 3;
	CLOCK_CNTL_INDEX[0] = 4 + vclk_div;
	__asm__ volatile ("eieio");
	ULONG ppll_div = CLOCK_CNTL_DATA->v;
	ULONG tbl_val = TableOs9Driver[(ppll_div >> 16) & 7];
	ULONG ppll_div_low = ppll_div & 0x7FF;
	ULONG vclk = freq(tbl_val, ppll_div_low, 1, 2950, pll_3);
	
	
	Nx = (x_mpll_ref_fb_div & 0x00ff00) >> 8;
	M  = x_mpll_ref_fb_div & 0x0000ff;
	
	ULONG xclk = RoundDiv(2 * Nx * ref_clk, M * (ULONG)PostDivSet[xclk_cntl]);
	if (xclk == 0) xclk = 0x1d4d;
	ULONG fifo_width = 128;
	ULONG fifo_depth = 32;
	
	volatile U32LE* MEM_CNTL = (volatile U32LE*)(BaseAddress + 0x140);
	ULONG mem_type = MEM_CNTL->v & 3;
	
	int x, b, p, ron, roff;
	ULONG n, d, bpp;
	bpp = 32;
	
	n = xclk * fifo_width;
	d = vclk * bpp;
	x = RoundDiv(n, d);

	UCHAR MB = 4;
	UCHAR Trp = 3;
	UCHAR Tr2w = 1;
	UCHAR Rloop = 16;
	UCHAR Trcd, Twr, CL;
	
	switch (mem_type) {
		case 0:
			Trcd = 3;
			Twr = 1;
			CL = 3;
			break;
		case 1:
			Trcd = 1;
			Twr = 1;
			CL = 2;
			break;
		case 2:
		default:
			Trcd = 3;
			Twr = 2;
			CL = 2;
			break;
	}
	
	ron = 4 * MB +
		3 * (Trcd - 2) +
		2 * Trp +
		Twr +
		CL +
		Tr2w +
		x;
	
	b = 0;
	for (; x != 0; b++) x >>= 1;
	p = b + 1;
	
	ron <<= (11 - p);
	n <<= (11 - p);
	x = RoundDiv(n, d);
	roff = x * (fifo_depth - 4);
	
	if ((ron + Rloop) >= roff) return false;
	
	volatile U16LE* DSP_CONFIG = (volatile U16LE*)(BaseAddress + 0x2E0);
	DSP_CONFIG[0].v = x;
	DSP_CONFIG[1].v = p | (Rloop << 4);
	volatile U16LE* DSP_ON_OFF = (volatile U16LE*)(BaseAddress + 0x2E4);
	DSP_ON_OFF[0].v = roff;
	DSP_ON_OFF[1].v = ron;
	__asm__ volatile ("eieio");
	return true;
}

static ULONG FbGetMmioBaseAtiForSwap(OFHANDLE Screen) {
	// Get the parent of screen.
	OFHANDLE GfxDevice = OfParent(Screen);
	if (GfxDevice == OFNULL) return 0;
	
	// Check for "ATY,Fcode" property
	StdOutWrite("\r\n");
	if (!OfPropExists(GfxDevice, "ATY,Fcode") && !OfPropExists(GfxDevice, "ATY,Rom#")) {
		if (OfPropExists(Screen, "ATY,Fcode") || OfPropExists(Screen, "ATY,Rom#")) {
			GfxDevice = Screen;
		} else {
			// this isn't ATI fcode, this isn't an ATI device
			return 0;
		}
	}
	
	// GfxDevice is now the base device.
	// get the assigned-addresses, of which there should be 3, allocate 4 just in case
	ULONG AssignedAddress[4 * 5];
	ULONG AddrLength = sizeof(AssignedAddress);
	ARC_STATUS Status = OfGetProperty(GfxDevice, "assigned-addresses", AssignedAddress, &AddrLength);
	if (ARC_FAIL(Status)) {
		return 0;
	}
	
	ULONG BaseAddress = 0;
	for (ULONG i = 0; i < AddrLength / 5; i++) {
		PULONG Aperture = &AssignedAddress[i * 5];
		if ((Aperture[0] & 0xff) != 0x18) continue; // MMIO area for rage4 and above
		BaseAddress = Aperture[2];
		break;
	}
	
	if (BaseAddress < 0x80800000) {
		return 0;
	}
	
	return BaseAddress;
}

static bool FbAtiSwapEndian(OFHANDLE Screen) {
	ULONG BaseAddress = FbGetMmioBaseAtiForSwap(Screen);
	if (BaseAddress == 0) return false;
	
	// On Rage4 or so this is SURFACE_DELAY
	// That said, only fcode for AGP rage4 has set-depth
	// Hopefully this is fine there!
	volatile U32LE* SURFACE_CNTL = (volatile U32LE*)(BaseAddress + 0xB00);
	SURFACE_CNTL->v = 0;
	__asm__ volatile ("eieio");
	
	// Just in case we end up here on Rage4/etc with set-delay:
	// These register bits are readonly on Radeon cards...
	volatile U32LE* CNFG_CNTL = (volatile U32LE*)(BaseAddress + 0xE0);
	CNFG_CNTL->v = CNFG_CNTL->v & ~3;
	__asm__ volatile ("eieio");
	
	return true;
}

static bool FbSetDepthAti(OFHANDLE Screen) {
	// Get the screen width, needed later.
	ULONG Width;
	if (ARC_FAIL(OfGetPropInt(Screen, "width", &Width))) return false;
	
	// Get the parent of screen.
	OFHANDLE GfxDevice = OfParent(Screen);
	if (GfxDevice == OFNULL) return false;
	
	// Check for "ATY,Fcode" property
	StdOutWrite("\r\n");
	if (!OfPropExists(GfxDevice, "ATY,Fcode") && !OfPropExists(GfxDevice, "ATY,Rom#")) {
		if (OfPropExists(Screen, "ATY,Fcode") || OfPropExists(Screen, "ATY,Rom#")) {
			GfxDevice = Screen;
		} else {
			// this isn't ATI fcode, this isn't an ATI device
			return false;
		}
	}
	
	// GfxDevice is now the base device.
	// get the assigned-addresses, of which there should be 3, allocate 4 just in case
	ULONG AssignedAddress[4 * 5];
	ULONG AddrLength = sizeof(AssignedAddress);
	ARC_STATUS Status = OfGetProperty(GfxDevice, "assigned-addresses", AssignedAddress, &AddrLength);
	if (ARC_FAIL(Status)) {
		return false;
	}
	
	// Rage4 has registers in different places, and uses different physmem block for its MMIO
	bool IsRage4 = false;
	{
		char NameBuffer[32];
		ULONG NameBufLen = sizeof(NameBuffer);
		Status = OfGetProperty(GfxDevice, "name", NameBuffer, &NameBufLen);
		if (ARC_FAIL(Status)) return false;
		IsRage4 = strstr(NameBuffer, "ATY,Rage128") != NULL;
	}
	
	ULONG BaseAddress = 0;
	for (ULONG i = 0; i < AddrLength / 5; i++) {
		PULONG Aperture = &AssignedAddress[i * 5];
		if (!IsRage4 && Aperture[4] < 0x01000000) continue; // 16MB vram area
		if (IsRage4 && (Aperture[0] & 0xff) != 0x18) continue; // rage4 MMIO area.
		BaseAddress = Aperture[2];
		break;
	}
	
	if (BaseAddress < 0x80800000) {
		return false;
	}
	if (!IsRage4) BaseAddress += 0x7ffc00;
	
	// determine register offsets
	ULONG off_CRTC_GEN_CNTL = (IsRage4 ? 0x50 : 0x1C);
	ULONG off_CRTC_H_SYNC_STRT_WID = (IsRage4 ? 0x204 : 4);
	ULONG off_DAC_REGS = (IsRage4 ? 0xB0 : 0xC0);
	ULONG off_DAC_CNTL = (IsRage4 ? 0xB4 : 0xC4);
	ULONG off_DSP_CONFIG = (IsRage4 ? 0x2E0 : 0x20);
	ULONG off_DSP_ON_OFF = (IsRage4 ? 0x2E4 : 0x24);
	
	// power off the CRTC while we make changes:
	volatile U32LE* CRTC_GEN_CNTL = (volatile U32LE*)(BaseAddress + off_CRTC_GEN_CNTL);
	CRTC_GEN_CNTL->v = (CRTC_GEN_CNTL->v & ~(1 << 25));
	__asm__ volatile ("eieio");
	
	// change the mode by using the DAC registers, some cards may require this
	volatile UCHAR* DAC_REGS = (volatile UCHAR*)(BaseAddress + off_DAC_REGS);
	volatile U32LE* DAC_CNTL = (volatile U32LE*)(BaseAddress + off_DAC_CNTL);
	ULONG ChipId = IsRage4 ? 0x5200 : ((volatile U32LE*)(BaseAddress + 0xE0))->v;
	USHORT DeviceId = ChipId & 0xFFFF;
	UCHAR ChipRev = (ChipId >> 24);
	// Emulator fix: dingusppc doing things in a different way as usual:
	if (ChipId == 0) {
		// Set the upper DAC registers
		DAC_CNTL->v |= 1;
		__asm__ volatile ("eieio");
		// Make sure we actually set it.
		if ((DAC_CNTL->v & 1) != 0) {
			// Set DAC indirect register index: PIX_FORMAT (register 10)
			DAC_REGS[0] = 10;
			__asm__ volatile ("eieio");
			DAC_REGS[1] = 0;
			__asm__ volatile ("eieio");
			// Write to DAC register PIX_FORMAT to set 32bpp
			DAC_REGS[2] = 6;
			__asm__ volatile ("eieio");
		}
 	}
	
	// CRTC_PIX_WIDTH = 32bpp
	CRTC_GEN_CNTL->v = (CRTC_GEN_CNTL->v & ~(7 << 8)) | (6 << 8);
	__asm__ volatile ("eieio");
	
#if 0
	// fix the hsync start+width for 32bpp
	// assumption: was set to 8bpp
	if (IsRage4) {
		volatile U16LE* CRTC_H_SYNC_STRT_WID = (volatile U16LE*)(BaseAddress + off_CRTC_H_SYNC_STRT_WID);
		CRTC_H_SYNC_STRT_WID->v -= (18 - 5);
	} else {
		volatile UCHAR* CRTC_H_SYNC_STRT_WID = (volatile UCHAR*)(BaseAddress + off_CRTC_H_SYNC_STRT_WID);
		CRTC_H_SYNC_STRT_WID[0] -= (18 >> 3) - (5 >> 3);
		CRTC_H_SYNC_STRT_WID[1] = 5;
	}
#endif
	
	bool ReallyHasDsp = false;
	bool Success = true;
	// If the card isn't the earliest mach64 type then we need to set the dsp correctly :(
	if (DeviceId != 0 && DeviceId != 0x4758 && DeviceId != 0x4358 && DeviceId != 0x4354 && DeviceId != 0x4554) {
		ReallyHasDsp = true;
		if (DeviceId == 0x5654 || DeviceId == 0x4754) {
			ReallyHasDsp = (ChipRev & 7) > 0;
		}
		if (IsRage4) {
			Success = FbSetDdaRage4(BaseAddress);
		}
		else if (ReallyHasDsp) {
			// BUGBUG: this is correct for Lombard, what about other systems???
			volatile U16LE* DSP_CONFIG = (volatile U16LE*)(BaseAddress + off_DSP_CONFIG);
			volatile U16LE* DSP_ON_OFF = (volatile U16LE*)(BaseAddress + off_DSP_ON_OFF);
			USHORT configHigh = DSP_CONFIG[1].v;
			USHORT loop_latency = configHigh & 0xF;
			USHORT precision = (configHigh >> 4) & 0x7;
			precision /= 2;
			precision <<= 4;
			DSP_CONFIG[1].v = loop_latency | precision;
			DSP_ON_OFF[0].v *= 2;
			DSP_ON_OFF[1].v *= 2;
			__asm__ volatile ("eieio");
		}
	}
	
	// deal with the DAC
	//DAC_REGS[4] &= ~0x20; __asm__ volatile ("eieio");
	DAC_REGS[0] = 0;
	__asm__ volatile ("eieio");
	
	for (ULONG i = 0; i < sizeof(sc_GammaTable); i++) {
		if (IsRage4) {
			ULONG value = sc_GammaTable[i];
			value |= (value << 8) | (value << 16);
			DAC_CNTL->v = value; __asm__ volatile ("eieio");
			continue;
		}
		DAC_REGS[1] = sc_GammaTable[i]; __asm__ volatile ("eieio");
		DAC_REGS[1] = sc_GammaTable[i]; __asm__ volatile ("eieio");
		DAC_REGS[1] = sc_GammaTable[i]; __asm__ volatile ("eieio");
		if (ReallyHasDsp) {
			DAC_REGS[2] = 0xFF;
			__asm__ volatile ("eieio");
		}
	}
	
	
	// power the CRTC back on
	CRTC_GEN_CNTL->v = (CRTC_GEN_CNTL->v | (1 << 25));
	__asm__ volatile ("eieio");
	
	// all done :)
	return Success;
}

static void FbGetDetails(OFHANDLE Screen, PHW_DESCRIPTION HwDesc, bool OverrideStride) {
	// Get the framebuffer details.
	ULONG FbAddr, Width, Height, Stride;
	if (
		ARC_FAIL(OfGetPropInt(Screen, "width", &Width)) ||
		ARC_FAIL(OfGetPropInt(Screen, "height", &Height)) ||
		ARC_FAIL(OfGetPropInt(Screen, "linebytes", &Stride))
	) {
		// restore the screen depth to allow stdout to work again
		if (!OverrideStride) {
			FbRestoreDepthByFcode();
			StdOutWrite("Could not read framebuffer details.\r\n");
		}
		OfExit();
		return;
	}
	
	if (ARC_FAIL(OfGetPropInt(Screen, "address", &FbAddr))) {
		// framebuffer address is in frame-buffer-adr
		// this is how BootX gets it
		OF_ARGUMENT Arg;
		if (ARC_FAIL(OfInterpret(0, 1, &Arg, "frame-buffer-adr"))) {
			OfExit();
			return;
		}
		FbAddr = Arg.Int;
	}
	
	if (OverrideStride) Stride *= sizeof(ULONG);
	
	// Wipe the screen
	memset((PVOID)FbAddr, 0, Height * Stride);
	// Set the framebuffer details
	HwDesc->FrameBufferBase = FbAddr;
	HwDesc->FrameBufferWidth = Width;
	HwDesc->FrameBufferHeight = Height;
	HwDesc->FrameBufferStride = Stride;
}

static ULONG UsbGetBase(OFHANDLE Handle) {
	ULONG AssignedAddress[5];
	ULONG AddrLength = sizeof(AssignedAddress);
	ARC_STATUS Status = OfGetProperty(Handle, "assigned-addresses", AssignedAddress, &AddrLength);
	if (ARC_FAIL(Status)) {
		return 0;
	}
	return AssignedAddress[2];
}

typedef void (*ArcFirmEntry)(PHW_DESCRIPTION HwDesc);
extern void __attribute__((noreturn)) ModeSwitchEntry(ArcFirmEntry Start, PHW_DESCRIPTION HwDesc, ULONG FbAddr);


int _start(int argc, char** argv, tfpOpenFirmwareCall of) {
	(void)argc;
	(void)argv;
	
	if (!OfInit(of)) {
		// can't do a thing!
		return -1;
	}

	// init stdout
	InitStdout();
	StdOutWrite("\r\n");
	
	// on lombard, OF doesn't power on the media bay until something wants to access it
	// so try to open the CD drive
	{
		OFIHANDLE hCd = OfOpen("cd:");
		if (hCd != OFINULL) OfClose(hCd);
	}

	// set up virtual address mappings for NT and set OF properties for stage 2 to find it
	int Error = ArcMemInitNtMapping();
	if (Error < 0) {
		StdOutWrite("Could not init memory mapping for NT");
		print_error(-Error);
		OfExit();
		return -2;
	}
	
	// get full OF path of stage 1.
	static char BootPath[1024];
	OFHANDLE Chosen = OfGetChosen();
	ULONG BootPathLength = sizeof(BootPath);
	ARC_STATUS Status = OfGetPropString(Chosen, "bootpath", BootPath, &BootPathLength);
	if (ARC_FAIL(Status)) {
		StdOutWrite("Could not get bootpath");
		print_error(Status);
		OfExit();
		return -3;
	}
	ULONG BootPathIdx = 0;
	while (BootPathIdx < BootPathLength && BootPath[BootPathIdx] != '\\') BootPathIdx++;
	if (BootPathIdx == BootPathLength) {
		StdOutWrite("Could not parse bootpath: ");
		StdOutWrite(BootPath);
		StdOutWrite("\r\n");
		OfExit();
		return -4;
	}
	BootPathIdx++;
	strcpy(&BootPath[BootPathIdx], "stage2.elf");

	// load stage2 from disk
	OFIHANDLE File = OfOpen(BootPath);
	if (File == OFINULL) {
		StdOutWrite("Could not open stage2: ");
		StdOutWrite(BootPath);
		StdOutWrite("\r\n");
		OfExit();
		return -5;
	}

	// load at exactly 4MB
	// use the NT mapping because the 1:1 mapping may not be present, whereas the NT mapping is guaranteed to be present
	PVOID Addr = (PVOID)((s_FirstFreePage * PAGE_SIZE) + 0x400000);
	ULONG ActualLoad = 0;
	// allow loading up to 12MB, ie, the rest of NT bootloader-accessible RAM when using OpenBIOS
	// if less RAM than that is accessible (shouldn't happen?!), then allow loading only as much as possible
	// BUGBUG: how to get the actual file length?
	ULONG LastAddress = 0x1000000;
	if (s_LastFreePage < (LastAddress / PAGE_SIZE)) LastAddress = s_LastFreePage * PAGE_SIZE;
	Status = OfRead(File, Addr, LastAddress - (ULONG)Addr - (s_FirstFreePage * PAGE_SIZE), &ActualLoad);
	OfClose(File);

	if (ARC_FAIL(Status)) {
		StdOutWrite("Could not read stage2: ");
		StdOutWrite(BootPath);
		print_error(Status);
		OfExit();
		return -7;
	}

	// check for validity
	if (ActualLoad < sizeof(Elf32_Ehdr) || ElfValid(Addr) <= 0) {
		StdOutWrite("Invalid ELF for stage2: ");
		StdOutWrite(BootPath);
		StdOutWrite("\r\n");
		OfExit();
		return -8;
	}

	// load ELF
	ULONG EntryPoint = ElfLoad(Addr);
	if (EntryPoint == 0) {
		StdOutWrite("Failed to load ELF for stage2: ");
		StdOutWrite(BootPath);
		StdOutWrite("\r\n");
		OfExit();
		return -9;
	}

	// zero ELF out of memory
	memset(Addr, 0, ActualLoad);
	
	// We now have free memory at exactly 4MB, we can use this to store our descriptor.
	PHW_DESCRIPTION Desc = (PHW_DESCRIPTION) Addr;
	Desc->MemoryLength = s_PhysMemLength;
	Desc->MacIoStart = s_MacIoStart;
	
	{
		ULONG DecrementerFrequency = 0;
		OFHANDLE Cpu = OfFindDevice("/cpus");
		if (Cpu != OFNULL) {
			Cpu = OfChild(Cpu);
			if (Cpu != OFNULL) {
				OfGetPropInt(Cpu, "timebase-frequency", &DecrementerFrequency);
			}
		}
		if (DecrementerFrequency == 0) {
			StdOutWrite("Could not obtain timebase-frequency\r\n");
			OfExit();
			return -10;
		}
		Desc->DecrementerFrequency = DecrementerFrequency;
	}
	
	if (OfFindDevice("pmu") == OFNULL) s_MrpFlags |= MRP_VIA_IS_CUDA;
	Desc->MrpFlags = s_MrpFlags;
	
	{
		// Get the framebuffer from OF.
		OFHANDLE Screen = OfFindDevice("screen");
		if (Screen == OFNULL) {
			StdOutWrite("Could not obtain screen device, incompatible Open Firmware\r\n");
			OfExit();
			return -11;
		}
		// Try to set screen depth to 32 bits by fcode implementation (this will work in some cases)
		if (FbSetDepthByFcode(Screen)) {
			// Only radeon cards have set-depth
			// set-depth also configures the card to be big endian, swap it
			if (!FbAtiSwapEndian(Screen)) {
				// Could not swap endianness on this card?
				FbRestoreDepthByFcode();
				StdOutWrite("Could not configure the framebuffer to be little-endian on this ATI card\r\n");
				OfExit();
				return -12;
			}
			FbGetDetails(Screen, Desc, false);
		} else {
			if (!FbSetDepthAti(Screen)) {
				// TODO: support NV cards
				StdOutWrite("Could not set up 32bpp framebuffer, nvidia cards are not supported yet\r\n");
				OfExit();
				return -12;
			}
			FbGetDetails(Screen, Desc, true);
		}
	}
	
	{
		// Get the usb controller.
		OFHANDLE Usb = OfFindDevice("usb");
		if (Usb != OFNULL) Desc->UsbOhciStart[0] = UsbGetBase(Usb);
		else {
			Usb = OfFindDevice("usb0");
			if (Usb != OFNULL) {
				Desc->UsbOhciStart[0] = UsbGetBase(Usb);
				Usb = OfFindDevice("usb1");
				if (Usb != OFNULL) {
					Desc->UsbOhciStart[1] = UsbGetBase(Usb);
				}
			}
		}
	}
	
	ULONG FbAddr = Desc->FrameBufferBase;
	// munge descriptor
	MsrLeMunge32(Desc, sizeof(*Desc));

	// call entrypoint through mode switch
	ArcFirmEntry NextEntry = (ArcFirmEntry)EntryPoint;
	ModeSwitchEntry(NextEntry, Desc, FbAddr);
}

extern const ULONG StartAixCall[2];
const ULONG StartAixCall[2] = { (ULONG) _start, 0 };
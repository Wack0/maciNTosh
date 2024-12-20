#include <MacTypes.h>
#include <MixedMode.h>
#include <Quickdraw.h>
#include <Palettes.h>
#include <Displays.h>
#include <Devices.h>
#include <Slots.h>
#include <Gestalt.h>
#include <ROMDefs.h>
#include <Video.h>
#include <NameRegistry.h>
#include <ShutDown.h>
#include <Events.h>

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include "arc.h"
#include "elf_abi.h"
#include "hwdesc.h"

enum {
	KERNEL_MAGIC_1 = 'Gary',
	KERNEL_MAGIC_2 = 0x05051956,
	KERNEL_MAGIC_PROCINFO = // 0x2C779802
		kRegisterBased |
		REGISTER_ROUTINE_PARAMETER(1, kRegisterA0, SIZE_CODE(sizeof(UInt32))) |
		REGISTER_ROUTINE_PARAMETER(2, kRegisterA1, SIZE_CODE(sizeof(UInt32))) |
		REGISTER_ROUTINE_PARAMETER(3, kRegisterD0, SIZE_CODE(sizeof(UInt32))) |
		REGISTER_ROUTINE_PARAMETER(4, kRegisterD2, SIZE_CODE(sizeof(UInt32))),
	//KERNEL_MAGIC_PROCINFO = 0x3C779802
};

enum {
	PHYSADDR_DESC = 0x400000
};

typedef struct {
    UInt32 Function;
    UInt32 Toc;
} AIXCALL_FPTR, *PAIXCALL_FPTR;

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

static void MsrLeSwap64InPlace(void* ptr, ULONG len) {
	ULONG* ptr32 = (ULONG*)ptr;
	
	for (; len > 0; len -= sizeof(uint64_t), ptr32 += 2) {
		ULONG temp = __builtin_bswap32(ptr32[0]);
		ptr32[0] = __builtin_bswap32(ptr32[1]);
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

#if 0
static GDHandle Set32bppResolutionNubus(void) {
	// This comes from Apple sample code.
	{
		SpBlock spBlock;
		if (SVersion(&spBlock) != noErr) return NULL;
	}
	
	GDHandle gd = NULL;
	GDHandle thisGd = NULL;
	UInt32 thisDisplayMode = 0, thisDepthMode = 0;
	UInt32 thisWidth = 0, thisHeight = 0;
	for (
		gd = DMGetFirstScreenDevice(true);
		gd != NULL;
		gd = DMGetNextScreenDevice(gd, true)
	) {
		if (!HasDepth(gd, 32, 1, 1)) continue;
		AuxDCEHandle hDce = (AuxDCEHandle) GetDCtlEntry(gd[0]->gdRefNum);
		SpBlock spBlock;
		spBlock.spSlot = hDce[0]->dCtlSlot;
		spBlock.spID = hDce[0]->dCtlSlotId;
		spBlock.spExtDev = hDce[0]->dCtlExtDev;
		spBlock.spHwDev = 0;
		spBlock.spParamData = (1 << foneslot);
		spBlock.spTBMask = 3;
		OSErr err = SGetSRsrc(&spBlock);
		if (err != noErr) continue;
		spBlock.spID = 0;
		spBlock.spTBMask = 2;
		spBlock.spParamData = (1 << fall) | (1 << foneslot) | (1 << fnext);
		spBlock.spCategory = catDisplay;
		spBlock.spCType = typeVideo;
		err = SGetTypeSRsrc(&spBlock);
		if (err != noErr) continue;
		while (err == noErr) {
			UInt32 displayMode = (UInt8)spBlock.spID;
			for (short mode = firstVidMode; mode <= sixthVidMode; mode++) {
				UInt32 depthMode = mode;
				UInt32 switchFlags;
				Boolean modeOk;
				OSErr err2 = DMCheckDisplayMode(gd, displayMode, depthMode, &switchFlags, 0, &modeOk);
				if (err2 != noErr || !modeOk) continue;
				// Check for video mode
				SpBlock modeBlock = spBlock;
				modeBlock.spID = depthMode;
				err2 = SFindStruct(&modeBlock);
				if (err2 != noErr) continue;
				// Read video mode
				modeBlock.spID = 1;
				err2 = SGetBlock(&modeBlock);
				if (err2 != noErr) continue;
				VPBlock* vpData = (VPBlock*)modeBlock.spResult;
				if (vpData == NULL) continue;
				do {
					if (vpData->vpPixelSize != 32) break;
					UInt32 width = vpData->vpBounds.right - vpData->vpBounds.left;
					UInt32 height = vpData->vpBounds.bottom - vpData->vpBounds.top;
					if (thisGd != NULL) {
						// if height is lower than existing then ignore
						if (height < thisHeight) continue;
						// if height is same but width is lower then ignore
						if (height == thisHeight && width < thisWidth) continue;
					}
					// this is better than the existing, or there is no existing
					thisGd = gd;
					thisDisplayMode = displayMode;
					thisDepthMode = depthMode;
					thisWidth = width;
					thisHeight = height;
				} while (false);
				DisposePtr((Ptr)vpData);
			}
			// get next
			spBlock.spTBMask = 2;
			spBlock.spParamData = (1 << fall) | (1 << foneslot) | (1 << fnext);
			err = SGetTypeSRsrc(&spBlock);
		}
	}
	
	if (thisGd != NULL) {
		// set the mode
		VDSwitchInfoRec switchInfo = {0};
		switchInfo.csMode = thisDepthMode;
		switchInfo.csData = thisDisplayMode;
		Handle hDisplayState = NULL;
		OSErr err = DMBeginConfigureDisplays(&hDisplayState);
		if (err == noErr) {
			err = DMSetDisplayMode(thisGd, thisDisplayMode, &thisDepthMode, (UInt32)&switchInfo, hDisplayState);
			DMEndConfigureDisplays(hDisplayState);
		}
		if (err != noErr) thisGd = NULL;
		else gd = thisGd;
	}
	
	return thisGd;
}
#endif

static OSErr VideoGetNextResolution(short refNum, VDResolutionInfoPtr resInfo) {
	CntrlParam pBlock;
	pBlock.ioNamePtr = NULL;
	pBlock.ioCRefNum = refNum;
	pBlock.csCode = cscGetNextResolution;
	memcpy(&pBlock.csParam[0], &resInfo, sizeof(resInfo));
	return PBStatusSync((ParmBlkPtr)&pBlock);
}

#if 0
static OSErr VideoSwitchVideoMode(short refNum, VDSwitchInfoPtr newMode) {
	CntrlParam pBlock;
	pBlock.ioNamePtr = NULL;
	pBlock.ioCRefNum = refNum;
	pBlock.csCode = cscSwitchMode;
	memcpy(&pBlock.csParam[0], &newMode, sizeof(newMode));
	return PBControlSync((ParmBlkPtr)&pBlock);
}
#endif

static GDHandle Set32bppResolutionPci(void) {
	GDHandle gd = NULL;
	GDHandle thisGd = NULL;
	UInt32 thisDisplayMode = 0, thisDepthMode = 0;
	UInt32 thisWidth = 0, thisHeight = 0;
	for (
		gd = DMGetFirstScreenDevice(true);
		gd != NULL;
		gd = DMGetNextScreenDevice(gd, true)
	) {
		
		short modeIdWanted = HasDepth(gd, 32, 1, 1);
		if (modeIdWanted == 0) continue;
		
		short gdRefNum = gd[0]->gdRefNum;
		// Ask driver for current resolution to get refresh rate.
		VDResolutionInfoRec resInfoCurr = {0};
		resInfoCurr.csPreviousDisplayModeID = kDisplayModeIDCurrent;
		OSErr errCurr = VideoGetNextResolution(gdRefNum, &resInfoCurr);
		if (errCurr != noErr) continue;
		// If current resolution is greater than 640x480, don't try to switch out of it.
		if (resInfoCurr.csHorizontalPixels > 640) continue;
		// Ask driver for each resolution and check it.
		VDResolutionInfoRec resInfo = {0};
		resInfo.csPreviousDisplayModeID = kDisplayModeIDFindFirstResolution;
		for (
			OSErr err = VideoGetNextResolution(gdRefNum, &resInfo);
			err == noErr && resInfo.csDisplayModeID != (DisplayModeID)kDisplayModeIDNoMoreResolutions;
			resInfo.csPreviousDisplayModeID = resInfo.csDisplayModeID,
			err = VideoGetNextResolution(gdRefNum, &resInfo)
		) {
			// BUGBUG: this may not be correct for some drivers. investigate.
			if (resInfo.csMaxDepthMode < modeIdWanted) continue;
			// make sure this mode is safe
			UInt32 switchFlags = 0;
			Boolean modeOk = false;
			OSErr err2 = DMCheckDisplayMode(gd, resInfo.csDisplayModeID, modeIdWanted, &switchFlags, 0, &modeOk);
			if (err2 != noErr) continue;
			// Check the refresh rate against the current one, don't allow higher
			// If DMCheckDisplayMode says the mode is OK, don't bother checking that.
			if (!modeOk && resInfo.csRefreshRate > resInfoCurr.csRefreshRate) continue;
			// mode supports 32bpp and is safe to switch to
			UInt32 width = resInfo.csHorizontalPixels;
			UInt32 height = resInfo.csVerticalLines;
			if (thisGd != NULL) {
				// if height is lower than existing then ignore
				if (height < thisHeight) continue;
				// if height is same but width is lower then ignore
				if (height == thisHeight && width < thisWidth) continue;
			}
			// this is better than the existing, or there is no existing
			thisGd = gd;
			thisDisplayMode = resInfo.csDisplayModeID;
			thisDepthMode = modeIdWanted;
			thisWidth = width;
			thisHeight = height;
		}
	}
	
	if (thisGd != NULL) {
		// set the mode
		VDSwitchInfoRec switchInfo = {0};
		switchInfo.csMode = thisDepthMode;
		switchInfo.csData = thisDisplayMode;
		Handle hDisplayState = NULL;
		OSErr err = DMBeginConfigureDisplays(&hDisplayState);
		if (err == noErr) {
			err = DMSetDisplayMode(thisGd, thisDisplayMode, &thisDepthMode, (UInt32)&switchInfo, hDisplayState);
			DMEndConfigureDisplays(hDisplayState);
		}
		if (err != noErr) thisGd = NULL;
		else gd = thisGd;
	}
	
	return thisGd;
}

static GDHandle GetDevice32Bpp(void) {
	// Thanks TurboOF for this
#if 0
	GDHandle gd = GetMainDevice();
	if (gd[0]->gdPMap[0]->pixelSize != 32) {
		if (!HasDepth(gd, 32, 1, 1)) return NULL;
		OSErr err = SetDepth(gd, 32, 1, 1);
		if (err) return NULL;
	}
	SetGDevice(gd);
#endif
	
	GDHandle gd = Set32bppResolutionPci();
#if 0 // let's not support nubus for time being
	if (gd == NULL) gd = Set32bppResolutionNubus();
#endif

	if (gd == NULL) {
		// could not find mode/gd to switch to, or an error occurred
		// final fallback, try to switch to 32bpp on current screen, no res switch
		gd = GetMainDevice();
		if (gd[0]->gdPMap[0]->pixelSize != 32) {
			if (!HasDepth(gd, 32, 1, 1)) return NULL;
			OSErr err = SetDepth(gd, 32, 1, 1);
			if (err != noErr) return NULL;
		}
	}
	
	SetGDevice(gd);
	//MainDevice = gd;
	return gd;
}

static void ConsoleWipe(void) {
	GDHandle gd = GetGDevice();
	Rect r = gd[0]->gdPMap[0]->bounds;
	ForeColor(blackColor);
	PaintRect(&r);
	ForeColor(whiteColor);
	MoveTo(1,12);
}

static void InitConsole(GDHandle hGd) {
	if (hGd == NULL) hGd = GetMainDevice();
	//SetGDevice(hGd);
	//if (GetGDevice() != hGd) while (1);
	
#if 0
	Rect r = hGd[0]->gdPMap[0]->bounds;
	ForeColor(blackColor);
	PaintRect(&r);
	ForeColor(whiteColor);
	MoveTo(1,12);
#endif
	
	//InitFonts();
	ConsoleWipe();
	
#if 0
	Rect r = hGd[0]->gdPMap[0]->bounds;
	ForeColor(blackColor);
	PaintRect(&r);
	ForeColor(whiteColor);
	MoveTo(1,12);
#endif
	
#if 0
	// Wipe the screen
	void* pFrameBuffer = (void*)hGd[0]->gdPMap[0]->baseAddr;
	UInt32 Stride = (UInt32)hGd[0]->gdPMap[0]->rowBytes & 0x3FFF;
	UInt32 Height = r.bottom - r.top;
	memset(pFrameBuffer, 0, Height * Stride);
#endif
}

void PrintChar(char chr) {
	PenState penState;
	GetPenState(&penState);
	GDHandle gd = GetGDevice();
	Rect r = gd[0]->gdPMap[0]->bounds;
	UInt32 height = (r.bottom - r.top);
	UInt32 width = (r.right - r.left);
	
	if (chr == '\n') {
		penState.pnLoc.v += 12;
		
		if (penState.pnLoc.v >= height) {
			ConsoleWipe();
		} else {
			MoveTo(1, penState.pnLoc.v);
		}
	} else if (chr == '\r') {
		MoveTo(1, penState.pnLoc.v);
	} else {
		DrawChar(chr);
		penState.pnLoc.h += 9;
		if (penState.pnLoc.h >= width) {
			penState.pnLoc.v += 12;
			if (penState.pnLoc.v < height) {
				MoveTo(1, penState.pnLoc.v);
			}
		}
	}
}

static void PrintCStr(const char* str) {
	for (int i = 0; str[i] != 0; i++) {
		PrintChar(str[i]);
	}
}

static void __attribute__((noreturn)) RestartSystem(void) {
	PrintCStr("Rebooting in 5 seconds.");
	for (int i = 0; i < 5; i++) {
		unsigned long secs;
		GetDateTime(&secs);
		unsigned long currSecs = secs;
		while (currSecs == secs) GetDateTime(&currSecs);
		PrintChar('.');
	}
	ShutDwnStart();
	while (1); // should not get here...
}

static Boolean SearchDeviceTree(const RegPropertyName* name, const void* value, RegPropertyValueSize valueSize, RegEntryID* EntryID) {
	RegEntryIter cookie;
	RegEntryID dev;
	Boolean done = false;
	OSErr err = RegistryEntryIterateCreate(&cookie);
	if (err != noErr) return false;
	err = RegistryEntrySearch(&cookie, kRegIterSubTrees, &dev, &done, name, value, valueSize);
	RegistryEntryIterateDispose(&cookie);
	if (err != noErr) return false;
	if (done) return false;
	if (EntryID != NULL) *EntryID = dev;
	return true;
}

static Boolean SearchDeviceTreeExists(const RegPropertyName* name, const void* value, RegPropertyValueSize valueSize) {
	return SearchDeviceTree(name, value, valueSize, NULL);
}

static Boolean DeviceTreeIterate(const RegEntryID* device, RegEntryIterationOp relationship, RegEntryID* nextDevice) {
	if (nextDevice == NULL) return false;
	RegEntryIter cookie;
	Boolean done = false;
	OSErr err = RegistryEntryIterateCreate(&cookie);
	if (err != noErr) return false;
	err = RegistryEntryIterateSet(&cookie, device);
	if (err != noErr) {
		RegistryEntryIterateDispose(&cookie);
		return false;
	}
	err = RegistryEntryIterate(&cookie, relationship, nextDevice, &done);
	RegistryEntryIterateDispose(&cookie);
	if (err != noErr) return false;
	if (done) return false;
	return true;
}

static Boolean DeviceTreeParent(const RegEntryID* device, RegEntryID* parentDevice) {
	return DeviceTreeIterate(device, kRegIterParents, parentDevice);
}

static Boolean DeviceTreeChild(const RegEntryID* device, RegEntryID* childDevice) {
	return DeviceTreeIterate(device, kRegIterChildren, childDevice);
}

static Boolean DevicePropertyExists(const RegEntryID* device, const RegPropertyName* name) {
	UInt32 size = 0;
	return RegistryPropertyGetSize(device, name, &size) == noErr;
}

enum { ASSIGNED_ADDRESS_MAX = 10 };

static UInt32 DeviceGetPhysicalBAR(RegEntryID *pciDevice, UInt32 BarIndex) {
	
	UInt32 assignAddrs[5*ASSIGNED_ADDRESS_MAX] = {0};

	UInt32 size = sizeof(assignAddrs);
	OSErr err = RegistryPropertyGet(pciDevice, "assigned-addresses", (void *)assignAddrs, &size);
	if (err != noErr) return 0;
	
	//for (int i=0; i<ASSIGNED_ADDRESS_MAX; i++)
	{
		// Only interested in PCI 32 or 64-bit memory space
		if (((assignAddrs[BarIndex*5] >> 24) & 3) < 2) return 0;

		return assignAddrs[ (BarIndex * 5) + 2 ];
	}
}

static UInt32 DeviceGetPhysicalBARById(RegEntryID *pciDevice, UInt32 BarId) {
	
	UInt32 assignAddrs[5*ASSIGNED_ADDRESS_MAX] = {0};

	UInt32 size = sizeof(assignAddrs);
	OSErr err = RegistryPropertyGet(pciDevice, "assigned-addresses", (void *)assignAddrs, &size);
	if (err != noErr) return 0;
	
	for (int i=0; i<(size / 5); i++)
	{
		// Only interested in PCI 32 or 64-bit memory space
		if (((assignAddrs[i*5] >> 24) & 3) < 2) continue;
		if ((assignAddrs[i*5] & 0xff) != BarId) continue;

		return assignAddrs[ (i * 5) + 2 ];
	}
	
	return 0;
}

static UInt32 DeviceGetPhysicalBARByMinLength(RegEntryID *pciDevice, UInt32 MinLength) {
	
	UInt32 assignAddrs[5*ASSIGNED_ADDRESS_MAX] = {0};

	UInt32 size = sizeof(assignAddrs);
	OSErr err = RegistryPropertyGet(pciDevice, "assigned-addresses", (void *)assignAddrs, &size);
	if (err != noErr) return 0;
	
	for (int i=0; i<(size / 5); i++)
	{
		// Only interested in PCI 32 or 64-bit memory space
		if (((assignAddrs[i*5] >> 24) & 3) < 2) continue;
		if (assignAddrs[ (i*5) + 4 ] < MinLength) continue;

		return assignAddrs[ (i * 5) + 2 ];
	}
	
	return 0;
}

static UInt32 DeviceGetAaplAddress(RegEntryID *pciDevice, UInt32 Index) {
	UInt32 ApplAddress[ASSIGNED_ADDRESS_MAX] = {0};

	UInt32 size = sizeof(ApplAddress);
	OSErr err = RegistryPropertyGet(pciDevice, "AAPL,address", (void *)ApplAddress, &size);
	if (err != noErr) return 0;
	return ApplAddress[Index];
}

#define STRLEN_CONST(x) (sizeof(x) - 1)
#define STR_AND_LEN_CONST(x) x, sizeof(x)

static Boolean FbSwapEndianAti(RegEntryID* dev, RegEntryID* parent) {
	RegEntryID* baseDev = dev;
	// Is this an ATI device?
	if (!DevicePropertyExists(baseDev, "ATY,Fcode") && !DevicePropertyExists(baseDev, "ATY,Rom#")) {
		baseDev = parent;
		if (!DevicePropertyExists(baseDev, "ATY,Fcode") && !DevicePropertyExists(baseDev, "ATY,Rom#")) {
			return false;
		}
	}
	
	// Get the device id.
	ULONG DeviceId;
	ULONG Size = sizeof(DeviceId);
	OSErr err = RegistryPropertyGet(baseDev, "device-id", &DeviceId, &Size);
	if (err != noErr) return false;
	
	// Is this a mach64?
	// This is basically the same list used by the linux driver.
	if (
		DeviceId == 0x4354 || DeviceId == 0x4358 || DeviceId == 0x4554 ||
		DeviceId == 0x4742 || DeviceId == 0x4744 || DeviceId == 0x4749 ||
		(DeviceId >= 0x474C && DeviceId <= 0x475A) ||
		DeviceId == 0x4c42 || DeviceId == 0x4c44 || DeviceId == 0x4c47 ||
		DeviceId == 0x4c49 ||
		(DeviceId >= 0x4c4d && DeviceId <= 0x4c54) ||
		(DeviceId >= 0x5654 && DeviceId <= 0x5656)
	) {
		// yes, get the MMIO base address
		ULONG Mach64Base = DeviceGetPhysicalBARByMinLength(baseDev, 0x01000000);
		if (Mach64Base == 0) {
			Mach64Base = DeviceGetAaplAddress(baseDev, 0);
			if (Mach64Base == 0) return false;
		}
		ULONG Mach64Regs = Mach64Base + 0x7ffc00;
		// MEM_CNTL.LOWER_APER_ENDIAN = MEM_CNTL.UPPER_APER_ENDIAN = APER_ENDIAN_LITTLE;
		volatile U32LE* MEM_CNTL = (volatile U32LE*)(Mach64Regs + 0xB0);
		// Some mach64 cards have endianness switch here, others have something else, try to figure out what it is
		ULONG MemCntlRead = (MEM_CNTL->v >> 24) & 0xF;
		// For these cards, both apertures are set to 32bpp big endian mode, ie, 0b1010
		// So check that.
		if (MemCntlRead == 0x0A) {
			MEM_CNTL->v &= ~0x0F000000;
			__asm__ volatile ("eieio");
		} else {
			// If the framebuffer base address is over 8MB into the memory space,
			// then it's in the big endian aperture, move it back down to the little endian one.
			// yes, this is a giant hack, but nobody cares
			PHW_DESCRIPTION Desc = (PHW_DESCRIPTION) PHYSADDR_DESC;
			if (Desc->FrameBufferBase > (Mach64Base + 0x800000)) {
				Desc->FrameBufferBase -= 0x800000;
			} else {
				// this card needs its own support added.
				return false;
			}
		}
		return true;
	}
	
	// make sure it's not a mach32 (did any ppc mac ever use mach32???)
	if (DeviceId == 0x4158) return false;
	
	// Mach128 or Radeon
	// Get the MMIO base address
	ULONG RageBase = DeviceGetPhysicalBARById(baseDev, 0x18);
	if (RageBase == 0) return false;
	
	// Do the MMIO pokes for all other cards.
	// The one for the other range does de jure or de facto no operation.
	// (either poking a register that won't be used, or writing readonly register bits)
	volatile U32LE* SURFACE_CNTL = (volatile U32LE*)(RageBase + 0xB00);
	SURFACE_CNTL->v = 0;
	__asm__ volatile ("eieio");
	
	volatile U32LE* CNFG_CNTL = (volatile U32LE*)(RageBase + 0xE0);
	CNFG_CNTL->v = CNFG_CNTL->v & ~3;
	__asm__ volatile ("eieio");
	return true;
	
}

static Boolean FbSwapEndianNvidia(RegEntryID* dev, RegEntryID* parent) {
	RegEntryID* baseDev = dev;
	// Is this an nvidia device?
	if (!DevicePropertyExists(baseDev, "NVDA,Features")) {
		baseDev = parent;
		if (!DevicePropertyExists(baseDev, "NVDA,Features")) {
			return false;
		}
	}
	
	ULONG NvBase = DeviceGetPhysicalBARById(baseDev, 0x10);
	if (NvBase == 0) return false;
	
	// Unset some CRTC register bit for both CRTCs
	ULONG PCRTC = NvBase + 0x600000;
	*(volatile ULONG*)(PCRTC + 0x804) &= ~(1 << 31); // First
	*(volatile ULONG*)(PCRTC + 0x2804) &= ~(1 << 31); // And second
	// And switch the card endianness back to little
	*(volatile ULONG*)(NvBase + 4) = 0;
	return true;
}

static Boolean FbSwapEndian(USHORT driverRef) {
	// Get the display device.
	RegEntryID dev;
	if (!SearchDeviceTree("driver-ref", &driverRef, sizeof(driverRef), &dev)) {
		return false;
	}
	
	// Get the parent device.
	RegEntryID parent;
	if (!DeviceTreeParent(&dev, &parent)) return false;
	
	// Attempt to swap endianness for both ati and nvidia.
	if (FbSwapEndianAti(&dev, &parent)) {
		//PrintCStr("Swapped card endian for ATI\n");
		return true;
	}
	
	if (FbSwapEndianNvidia(&dev, &parent)) {
		//PrintCStr("Swapped card endian for NV\n");
		return true;
	}
	
	return false;
}

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
	PrintCStr(" (");
	PrintCStr(buffer);
	PrintCStr(")\n");
}

typedef void (*ArcFirmEntry)(PHW_DESCRIPTION HwDesc);
extern void __attribute__((noreturn)) ModeSwitchEntry(ArcFirmEntry Start, PHW_DESCRIPTION HwDesc, ULONG FbAddr);

static void ppc_escalate_impl(void) {
	typedef void (*fpResetSystem)(UInt32 Magic1, UInt32 Magic2, UInt32 MsrClear, UInt32 Unused, UInt32 MsrSet);
	AIXCALL_FPTR TrapResetSystem = *(PAIXCALL_FPTR)0x68FFE648;
	TrapResetSystem.Function += (sizeof(UInt32) * 2);
	// Memory barrier here, to prevent untoward over-optimisation
	asm volatile("" : : : "memory");
	fpResetSystem ResetSystem = (fpResetSystem)&TrapResetSystem;
	ResetSystem(KERNEL_MAGIC_1, KERNEL_MAGIC_2, 0x0000C000, 0, 0x00000000);
	// we should be in kernel mode now, with Mac OS moribund
	PHW_DESCRIPTION Desc = (PHW_DESCRIPTION) PHYSADDR_DESC;
	PULONG Context = (PULONG)&Desc[1];
	ArcFirmEntry NextEntry = (ArcFirmEntry)Context[0];
	ULONG FbAddr = Desc->FrameBufferBase;
	// munge descriptor
	MsrLeMunge32(Desc, sizeof(*Desc));
	// call entrypoint through mode switch
	ModeSwitchEntry(NextEntry, Desc, FbAddr);
}

static void ppc_escalate(void) {
	static UInt8 m68k_code[] = {
		0x22, 0x7C, 0x68, 0xFF, 0xF9, 0x94, // movea.l #$68FFF994, a1
		0x20, 0x29, 0x00, 0x04,             // move.l 4(a1),d0
		0xB0, 0x91,                         // cmp.l (a1),d0
                0x66, 0x08,                         // bne.s done
                0x22, 0x81,                         // move.l d1,(a1)
                0xfc, 0x1e,                         // _Trap1E ; just overwritten
		// should never get here, if we did something wrong
		0x22, 0xA9, 0x00, 0x04,             // move.l 4(a1),(a1)
		// done:
		0x4E, 0x75,                         // rts
	};
	enum {
		PPC_ESCALATE_PROCINFO =
			kRegisterBased |
			REGISTER_ROUTINE_PARAMETER(1, kRegisterD1, SIZE_CODE(sizeof(UInt32)))
	};
	PAIXCALL_FPTR fptr = (PAIXCALL_FPTR)ppc_escalate_impl;
	CallUniversalProc((UniversalProcPtr)m68k_code, PPC_ESCALATE_PROCINFO, ((PAIXCALL_FPTR)fptr)->Function);
	// should not get here
	while (1);
}

void __start(void) {
	// Attempt to switch to 32bpp
	GDHandle hGd = GetDevice32Bpp();
	// Init console.
	InitConsole(hGd);
	PrintCStr("Old World stage 1 bootloader for Gossamer chipset starting\n");
	// Ensure we are at 32bpp graphics
	if (hGd[0]->gdPMap[0]->pixelSize != 32) {
		PrintCStr("Fatal error: Could not set colour depth to 32bpp\n");
		RestartSystem();
	}
	// Make sure we have grackle.
	if (!SearchDeviceTreeExists("compatible", STR_AND_LEN_CONST("grackle"))) {
		PrintCStr("This bootloader is intended for systems using the \"Gossamer\" chipset.\n");
		PrintCStr("Fatal error: System is incompatible with this bootloader\n");
		RestartSystem();
	}
	
	// Claim some physical memory
	enum {
		PHYSMEM_CLAIM_START = 0x400000,
		PHYSMEM_MINIMUM_REQUIRED_GRACKLE = 0xC00000, // 12MB
		PHYSMEM_RECOMMENDED_GRACKLE = 0x1000000, // 16MB
	};
	
	// Ensure we have at least 12MB of RAM
	UInt32 PhysMemSize;
	if (Gestalt(gestaltPhysicalRAMSize, (long*)&PhysMemSize) != noErr) {
		// can't assume anything???
		PhysMemSize = PHYSMEM_MINIMUM_REQUIRED_GRACKLE - 1;
	}
	if (PhysMemSize < PHYSMEM_MINIMUM_REQUIRED_GRACKLE) {
		PrintCStr("Fatal error: At least 12MB of RAM is required for loading.\n");
		RestartSystem();
	}
	
	UInt32 ClaimEnd = PhysMemSize;
	if (ClaimEnd > PHYSMEM_RECOMMENDED_GRACKLE)
		ClaimEnd = PHYSMEM_RECOMMENDED_GRACKLE;
	// Allocate from 4MB to end of RAM or 16MB
	OSErr err = HoldMemory((Ptr)PHYSMEM_CLAIM_START, ClaimEnd - PHYSMEM_CLAIM_START);
	if (err != noErr) {
		PrintCStr("Fatal error: Cannot allocate physical memory\n");
		RestartSystem();
	}
	
	// Load stage2 from disk.
	// Load at 4MB.
	ParamBlockRec pb = {0};
	pb.ioParam.ioNamePtr = "\x0astage2.elf";
	pb.ioParam.ioVRefNum = 0;
	err = PBOpenDFSync(&pb);
	if (err != noErr) {
		PrintCStr("Fatal error: Could not open :stage2.elf");
		print_error(err);
		RestartSystem();
	}
	// Get the file size.
	err = PBGetEOFSync(&pb);
	if (err != noErr) {
		PBCloseSync(&pb);
		PrintCStr("Fatal error: Could not open :stage2.elf");
		print_error(err);
		RestartSystem();
	}
	// Read file.
	long fileSize = (long)pb.ioParam.ioMisc;
	PVOID Addr = (PVOID)PHYSADDR_DESC;
	pb.ioParam.ioBuffer = Addr;
	pb.ioParam.ioReqCount = fileSize;
	err = PBReadSync(&pb);
	// Close file.
	PBCloseSync(&pb);
	if (err != noErr) {
		PrintCStr("Fatal error: Could not read :stage2.elf");
		print_error(err);
		RestartSystem();
	}
	if (pb.ioParam.ioActCount != fileSize) {
		PrintCStr("Fatal error: Could not fully read :stage2.elf");
		print_error(pb.ioParam.ioActCount);
		RestartSystem();
	}
	ULONG ActualLoad = (ULONG)fileSize;
	
	// check for validity
	if (ActualLoad < sizeof(Elf32_Ehdr) || ElfValid(Addr) <= 0) {
		PrintCStr("Fatal error: :stage2.elf is not a valid ELF file\n");
		RestartSystem();
	}

	// load ELF
	ULONG EntryPoint = ElfLoad(Addr);
	if (EntryPoint == 0) {
		PrintCStr("Fatal error: Could not load :stage2.elf\n");
		RestartSystem();
	}

	// zero ELF out of memory
	memset(Addr, 0, ActualLoad);
	
	// We now have free memory at exactly 4MB, we can use this to store our descriptor.
	PHW_DESCRIPTION Desc = (PHW_DESCRIPTION) Addr;
	Desc->MemoryLength = PhysMemSize;
	ULONG MrFlags = MRF_OLD_WORLD;
	if (SearchDeviceTreeExists("compatible", STR_AND_LEN_CONST("via-cuda")))
		MrFlags |= MRF_VIA_IS_CUDA;
	Desc->MrFlags = MrFlags;
	
	Desc->FrameBufferBase = (ULONG)hGd[0]->gdPMap[0]->baseAddr;
	Desc->FrameBufferStride = (UInt32)hGd[0]->gdPMap[0]->rowBytes & 0x3FFF;
	Desc->FrameBufferHeight = hGd[0]->gdPMap[0]->bounds.bottom - hGd[0]->gdPMap[0]->bounds.top;
	Desc->FrameBufferWidth = hGd[0]->gdPMap[0]->bounds.right - hGd[0]->gdPMap[0]->bounds.left;
	
	// Get decrementer frequency
	{
		ULONG DecrementerFrequency = 0;
		RegEntryID cpus;
		if (SearchDeviceTree("name", STR_AND_LEN_CONST("cpus"), &cpus)) {
			RegEntryID cpu;
			if (DeviceTreeChild(&cpus, &cpu)) {
				ULONG Size = sizeof(DecrementerFrequency);
				err = RegistryPropertyGet(&cpu, "timebase-frequency", &DecrementerFrequency, &Size);
				if (err != noErr) DecrementerFrequency = 0;
			}
		}
		
		if (DecrementerFrequency == 0) {
			PrintCStr("Fatal error: Could not obtain timebase-frequency\n");
			RestartSystem();
		}
		Desc->DecrementerFrequency = DecrementerFrequency;
	}
	
	// Get the mac-io controller from the device tree.
	// Obtain its base address from PCI BAR and place it in descriptor.
	{
		RegEntryID MacIo;
		if (!SearchDeviceTree("name", STR_AND_LEN_CONST("mac-io"), &MacIo)) {
			// um
			PrintCStr("Fatal error: Cannot find super I/O controller in device tree\n");
			RestartSystem();
		}
		UInt32 MacIoStart = DeviceGetPhysicalBAR(&MacIo, 0);
		if (MacIoStart == 0) {
			PrintCStr("Fatal error: Cannot find super I/O controller physical address\n");
			RestartSystem();
		}
		Desc->MacIoStart = MacIoStart;
	}
	
	// Put context directly after the desc, so post-escalation code can find it
	PULONG Context = (PULONG)&Desc[1];
	Context[0] = EntryPoint;
	
	if (!FbSwapEndian(hGd[0]->gdRefNum)) {
		PrintCStr("Fatal error: Failed to swap framebuffer endianness\n");
		RestartSystem();
	}
	// Wipe screen at this point
	memset((PVOID)Desc->FrameBufferBase, 0, Desc->FrameBufferStride * Desc->FrameBufferHeight);
	
	// Escalate to kernel (and disable interrupts).
	// When in kernel mode, we munge the descriptor for little endian mode,
	// then switch endianness and jump to stage2.
	ppc_escalate();
}
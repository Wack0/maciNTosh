#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "arc.h"
#include "arcio.h"
#include "arcmem.h"
#include "coff.h"
#include "ppcinst.h"

enum {
    STYP_REG = 0x00000000,
    STYP_TEXT = 0x00000020,
    STYP_INIT = 0x80000000,
    STYP_RDATA = 0x00000100,
    STYP_DATA = 0x00000040,
    STYP_LIT8 = 0x08000000,
    STYP_LIT4 = 0x10000000,
    STYP_SDATA = 0x00000200,
    STYP_SBSS = 0x00000080,
    STYP_BSS = 0x00000400,
    STYP_LIB = 0x40000000,
    STYP_UCODE = 0x00000800,
    S_NRELOC_OVFL = 0x20000000,

    SECTION_REQUIRES_LOAD = STYP_TEXT | STYP_INIT | STYP_RDATA | STYP_DATA | STYP_SDATA,
    SECTION_REQUIRES_ZERO = STYP_BSS | STYP_SBSS,

    SECTION_REQUIRES_LOAD_PE = IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA,
    SECTION_REQUIRES_ZERO_PE = IMAGE_SCN_CNT_UNINITIALIZED_DATA
};

enum {
    R_SN_TEXT = 1,
    R_SN_RDATA,
    R_SN_DATA,
    R_SN_SDATA,
    R_SN_SBSS,
    R_SN_BSS,
    R_SN_INIT,
    R_SN_LIT8,
    R_SN_LIT4,
    R_SN_MAX
};

enum {
    SECTOR_SIZE = 0x200
};

typedef struct _SECTION_RELOCATION_ENTRY {
    ULONG FixupValue;
    ULONG PointerToRelocations;
    USHORT NumberOfRelocations;
} SECTION_RELOCATION_ENTRY, * PSECTION_RELOCATION_ENTRY;

#define	MAX_ARGUMENT	(512 - sizeof(ULONG) - 16*sizeof(PUCHAR))
typedef	struct _SAVED_ARGUMENTS {
    ULONG Argc;
    U32LE Argv[16];
    U32LE Envp[16];
    UCHAR Arguments[MAX_ARGUMENT];
} SAVED_ARGUMENTS, * PSAVED_ARGUMENTS;

static SAVED_ARGUMENTS SavedArgs;

static PVOID ScratchAddress = NULL;
static bool s_OldCoffLoaded = false;
static bool s_MemoryMapFixedForOldCoff = false;

static inline ARC_FORCEINLINE ULONG ScratchEnd() {
    return (ULONG)ScratchAddress + ARCFW_MEM2_SIZE;
}

PVOID ArcLoadGetScratchAddress(void) {
    return ScratchAddress;
}

#define mfpvr() ({u32 _rval; \
		__asm__ __volatile__ ("mfpvr %0" : "=r"(_rval)); _rval;})

static void InstructionPerformPatch(
    PU32LE SectionBaseLittle,
    ULONG Offset
) {
    ULONG instruction = SectionBaseLittle[Offset].v;

    static ULONG PvrVersion = 0;
    if (PvrVersion == 0) PvrVersion = mfpvr() >> 16;
    // NT 4 osloader checks pvr, if it's not in hardcoded list then panic
    // so patch this check :)
    if (PvrVersion > 9 && PvrVersion != 20) {
        PPC_INSTRUCTION_BIG Insn;
        Insn.Long = instruction;
        if (Insn.Primary_Op == X31_OP && Insn.XFXform_XO == MFSPR_OP && Insn.XFXform_spr == 1000) {
            // Make NT believe this is an Arthur derivative
            // addis rx, 0, 8
            PPC_INSTRUCTION_BIG Patch;
            Patch.Long = 0;
            Patch.Primary_Op = ADDIS_OP;
            Patch.Dform_RT = Insn.XFXform_RT;
            Patch.Dform_RA = 0;
            Patch.Dform_D = 8;
            instruction = Patch.Long;
            SectionBaseLittle[Offset].v = instruction;
        }
    }
}

static ARC_STATUS RelocatePEBlock(ULONG VirtualAddress, ULONG Length, PU16LE Block, LONG Diff, bool IsLittleEndian) {
    for (ULONG Index = 0; Index < Length; Index++) {
        USHORT Offset = Block[Index].v;
        USHORT Type = Offset >> 12;
        Offset &= (ARC_BIT(12) - 1);
        ULONG FixupVA = VirtualAddress + Offset;
        PU16BE DataBig = (PU16BE)FixupVA;
        PU16LE DataLittle = (PU16LE)FixupVA;
        switch (Type) {
        case IMAGE_REL_BASED_HIGHLOW:
            // 32-bit relocation
        {
            LONG Base;
            memcpy(&Base, (PVOID)FixupVA, sizeof(Base));
            if (!IsLittleEndian) {
                Base = SwapEndianness32(Base);
            }
            Base += Diff;
            if (!IsLittleEndian) {
                Base = SwapEndianness32(Base);
            }
            memcpy((PVOID)FixupVA, &Base, sizeof(Base));
        }
        break;

        case IMAGE_REL_BASED_HIGH:
            // high 16-bit relocation
        {
            ULONG Temp = (IsLittleEndian ? DataLittle->v : DataBig->v) << 16;
            Temp += Diff;
            if (IsLittleEndian) DataLittle->v = (Temp >> 16);
            else DataBig->v = (Temp >> 16);
        }
        break;

        case IMAGE_REL_BASED_HIGHADJ:
            // high 16-bit relocation with adjustment
        {
            if (Index == Length) {
                // whoops, better not overflow
                printf("HighAdj relocation overflows table\n");
                return _EBADF;
            }
            ULONG Temp = (IsLittleEndian ? DataLittle->v : DataBig->v) << 16;
            Index++;
            PU16BE BlockBig = (PU16BE)(ULONG)Block;
            // tfw MS seriously did this
            Temp += (IsLittleEndian ? Block[Index].v : BlockBig[Index].v);
            Temp += Diff;
            Temp += INT16_MAX + 1;

            if (IsLittleEndian) DataLittle->v = (Temp >> 16);
            else DataBig->v = (Temp >> 16);
        }
        break;

        case IMAGE_REL_BASED_LOW:
            // low 16-bit relocation
        {
            ULONG Temp = (IsLittleEndian ? DataLittle->v : DataBig->v);
            Temp += Diff;
            if (IsLittleEndian) DataLittle->v = Temp;
            else DataBig->v = Temp;
        }
        break;

        //case IMAGE_REL_BASED_MIPS_JMPADDR: // not valid for PowerPC, osloader does it for all architectures for some reason

        case IMAGE_REL_BASED_ABSOLUTE:
            // no fixup required
            break;

        default:
            // invalid for powerpc
            printf("Invalid relocation %x\n", Type);
            return _EBADF;
        }
    }

    // all good
    return _ESUCCESS;
}

static ARC_STATUS RelocatePE(ULONG ImageBase, PIMAGE_FILE_HEADER FileHeader, PIMAGE_OPTIONAL_HEADER OptionalHeader, PIMAGE_SECTION_HEADER Sections) {
    bool IsLittleEndian = FileHeader->Machine == IMAGE_FILE_MACHINE_POWERPC;

    PIMAGE_DATA_DIRECTORY RelocDir = &OptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    bool HasRelocations = RelocDir->VirtualAddress != 0 && RelocDir->Size != 0;
    PIMAGE_DATA_DIRECTORY ExceptionDir = &OptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    bool HasExceptions = ExceptionDir->VirtualAddress != 0 && ExceptionDir->Size != 0;

    if (ImageBase == OptionalHeader->ImageBase) {
        // no need to relocate
        HasRelocations = false;
    }
    else if (HasRelocations) {
        // no relocations yet base moved
        printf("Needs relocation but image has none\n");
        return _EBADF;
    }

    if (!HasRelocations && !IsLittleEndian) {
        // nothing needs to be done here
        return _ESUCCESS;
    }

    ARC_STATUS Status;

    // apply all PE relocations first. remember endianness.
    if (HasRelocations) {
        ULONG OldBase = OptionalHeader->ImageBase;
        PIMAGE_BASE_RELOCATION Block = (PIMAGE_BASE_RELOCATION)(ImageBase + RelocDir->VirtualAddress);
        ULONG BlockStart = (ULONG)Block;
        ULONG RelocLength = RelocDir->Size;

        for (ULONG Offset = 0; Offset < RelocLength;) {
            Offset += Block->SizeOfBlock;
            ULONG SizeOfBlock = Block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION);
            PU16LE Entries = (PU16LE)(ULONG)&Block[1];

            Status = RelocatePEBlock(
                ImageBase + Block->VirtualAddress, 
                SizeOfBlock / sizeof(USHORT),
                Entries,
                ImageBase - OldBase,
                IsLittleEndian
            );

            if (ARC_FAIL(Status)) return Status;

            Block = (PIMAGE_BASE_RELOCATION)(BlockStart + Offset);
        }
    }

    // Instructions need patching here.
    {
        // Exception data is never present, just check every 32 bit value of every section containing code.
        for (int i = 0; i < FileHeader->NumberOfSections - 1; i++) {
            if ((Sections[i].Characteristics & IMAGE_SCN_CNT_CODE) == 0) continue;

            ULONG SectionBase = ImageBase + Sections[i].VirtualAddress;
            PU32BE SectionBaseBig = (PU32BE)SectionBase;
            PU32LE SectionBaseLittle = (PU32LE)SectionBase;

            for (ULONG off = 0; off < Sections[i].SizeOfRawData / sizeof(ULONG); off++) {
                InstructionPerformPatch(SectionBaseLittle, off);
            }
        }
    }

    return _ESUCCESS;
}

// Reads the COFF symbol table and string table to scratch buffer
static ARC_STATUS ReadCoffSymbolTable(ULONG FileId, ULONG PointerToSymbolTable, ULONG NumberOfSymbols) {
    PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

    // If no scratch address was allocated, error
    if (ScratchAddress == NULL) {
        return _ENOMEM;
    }

    PULONG SymbolCount = (PULONG)ScratchAddress;
    *SymbolCount = NumberOfSymbols;

    PVOID ScratchAddress = (SymbolCount + 1);

    // Get file length
    FILE_INFORMATION Info;
    ARC_STATUS Status = Api->GetFileInformationRoutine(FileId, &Info);
    if (ARC_FAIL(Status)) return Status;

    // Seek to symbol table
    LARGE_INTEGER Offset = Int64ToLargeInteger(PointerToSymbolTable);
    Status = Api->SeekRoutine(FileId, &Offset, SeekAbsolute);
    if (ARC_FAIL(Status)) return Status;

    int64_t ExpectedLength64 = Info.EndingAddress.QuadPart - PointerToSymbolTable;
    // Needs to fit in ULONG
    if (ExpectedLength64 > UINT32_MAX) return _E2BIG;
    ULONG ExpectedLength = (ULONG)ExpectedLength64;
    
    // If the symbol table is too long for the scratch memory, error
    if ((ULONG)ScratchAddress + ExpectedLength > ScratchEnd()) {
        return _ENOMEM;
    }

    // Read to scratch memory
    U32LE Count;
    Status = Api->ReadRoutine(FileId, ScratchAddress, ExpectedLength, &Count);
    if (ARC_FAIL(Status)) return Status;
    if (Count.v != ExpectedLength) return _EBADF;

    return _ESUCCESS;
}

// Gets the address of a symbol using the loaded COFF symbol table in scratch
static PVOID GetSymbolEntryCoff(ULONG ImageBase, const char* SymbolName) {
    PIMAGE_SYMBOL SymbolTable = (PIMAGE_SYMBOL)((ULONG)ScratchAddress + sizeof(ULONG));
    ULONG SymbolCount = *(PULONG)ScratchAddress;
    PCHAR StringTable = (PCHAR)&SymbolTable[SymbolCount];

    // for each symbol
    for (ULONG i = 0; i < SymbolCount; i += (1 + SymbolTable[i].NumberOfAuxSymbols)) {
        // storage class must be external for a function
        if (SymbolTable[i].StorageClass != IMAGE_SYM_CLASS_EXTERNAL) continue;
        // symbol cannot be absolute
        if (SymbolTable[i].SectionNumber == 0) continue;
        if (SymbolTable[i].N.Name.Short == 0) {
            // offset into string-table
            if (!strcmp(SymbolName, &StringTable[SymbolTable[i].N.Name.Long])) {
                // found it
                return (PVOID)(ImageBase + SymbolTable[i].Value);
            }
            continue;
        }
        else {
            // short symbol
            if (
                !memcmp(SymbolName, SymbolTable[i].N.ShortName, sizeof(SymbolTable[i].N.ShortName)) || // 8 character names aren't null terminated
                !strcmp(SymbolName, SymbolTable[i].N.ShortName)
            ) {
                // found it
                return (PVOID)(ImageBase + SymbolTable[i].Value);
            }
            continue;
        }
    }

    return NULL;
}

static ARC_STATUS ArcLoadImpl(
    IN PCHAR ImagePath,
    IN ULONG TopAddress,
    OUT PULONG EntryAddress,
    OUT PULONG LowAddress,
    OUT PULONG ImageBasePage,
    OUT PULONG ImageSizePage
) {
    if (EntryAddress == NULL || LowAddress == NULL) {
        printf("Required pointers are NULL\n");
        return _EFAULT;
    }
    PSECTION_RELOCATION_ENTRY RelocationTable = NULL;
    BYTE LocalBuffer[2 * SECTOR_SIZE + 0x40];
    // Align the COFF header to a dcache line.
    PBYTE LocalPointer = (PVOID)(((ULONG)(&LocalBuffer[DCACHE_LINE_SIZE - 1])) & ~(DCACHE_LINE_SIZE - 1));

    // Initialise the entry address to NULL.
    *EntryAddress = 0;

    PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

    // Open the file to load readonly.
    U32LE _FileId;
    ARC_STATUS Status = Api->OpenRoutine(ImagePath, ArcOpenReadOnly, &_FileId);
    if (ARC_FAIL(Status)) {
        return Status;
    }
    ULONG FileId = _FileId.v;

    do {
        // Read two sectors to get the COFF header.
        // Two sectors can fit COFF file header + full PE optional header + 19 section headers.
        U32LE Count;
        Status = Api->ReadRoutine(FileId, LocalPointer, SECTOR_SIZE * 2, &Count);
        if (ARC_FAIL(Status)) break;
        if (Count.v != SECTOR_SIZE * 2) {
            printf("Tried to read %x bytes and read %x bytes", SECTOR_SIZE * 2, Count.v);
            Status = _EFAULT;
            break;
        }

        PIMAGE_FILE_HEADER FileHeader = (PIMAGE_FILE_HEADER)LocalPointer;
        PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)LocalPointer;
        bool IsFullPE = (DosHeader->e_magic == IMAGE_DOS_SIGNATURE);
        if (IsFullPE) {
            // this is full PE
            Status = _EBADF;
            break;
#if 0
            if ((DosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS)) > Count.v) {
                printf("Bad PE\n");
                Status = _EBADF;
                break;
            }
            NtHeaders = (PIMAGE_NT_HEADERS)(LocalPointer + DosHeader->e_lfanew);
            FileHeader = &NtHeaders->FileHeader;
#endif
        }
        if (
            // Don't accept any COFF that isn't for PPC(LE).
            (FileHeader->Machine != IMAGE_FILE_MACHINE_POWERPC) || // && FileHeader->Machine != IMAGE_FILE_MACHINE_POWERPCBE) ||
            // Don't accept a COFF that isn't executable.
            (FileHeader->Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) == 0 ||
            // Don't accept a COFF with an optional header that's too small.
            FileHeader->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) // sizeof(IMAGE_OPTIONAL_HEADER_COFF)
        ) {
            printf("Bad COFF\n");
            Status = _EBADF;
            break;
        }

        // NT kernels before (somewhere between 1234 and 1314) hardcode setting BATs to the first 8MB of RAM.
        // So we need to set our memchunks up to handle this if we load an older NT bootloader.
        // 1314 bootloaders were compiled mid-April 1996.
        // NT 3.51 SP5 provides a newer veneer, but not a newer osloader.
        // Thus, we can rely on the TimeDateStamp for this.
        bool IsOldCoff = FileHeader->TimeDateStamp < 828316800; // 1996-04-01

        bool IsLittleEndian = FileHeader->Machine == IMAGE_FILE_MACHINE_POWERPC;
        //bool HasPEOptionalHeader = FileHeader->SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER);
        bool HasRelocations = (FileHeader->Characteristics & IMAGE_FILE_RELOCS_STRIPPED) == 0;
        bool HasExceptions = false;
        USHORT NumberOfSections = FileHeader->NumberOfSections;
        PIMAGE_OPTIONAL_HEADER OptionalHeader = (PIMAGE_OPTIONAL_HEADER)&FileHeader[1];
        PIMAGE_SECTION_HEADER Sections = (PIMAGE_SECTION_HEADER)((size_t)OptionalHeader + FileHeader->SizeOfOptionalHeader);

        //if (HasPEOptionalHeader)
        {
            if (
                // Optional header magic must be PE32
                OptionalHeader->Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
                // With the full PE optional header, the header size is known.
                // If this is greater than what was read, then don't accept this file.
                OptionalHeader->SizeOfHeaders > Count.v
            ) {
                printf("Bad opthdr\n");
                Status = _EBADF;
                break;
            }

            // If base relocation directory addr or size is 0, then this executable has no relocations
            PIMAGE_DATA_DIRECTORY RelocDir = &OptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            HasRelocations = RelocDir->VirtualAddress != 0 && RelocDir->Size != 0;
            PIMAGE_DATA_DIRECTORY ExceptionDir = &OptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
            HasExceptions = ExceptionDir->VirtualAddress != 0 && ExceptionDir->Size != 0;
        }
#if 0
        else if (HasRelocations) {
            // Walk through all sections, if one section has relocations then reloc is allowed.
            HasRelocations = false;
            for (int i = 0; i < NumberOfSections; i++) {
                if (Sections[i].NumberOfRelocations != 0) {
                    HasRelocations = true;
                    break;
                }
            }
        }
#endif

        ULONG SizeOfImage;
        ULONG ImageBase;

        //if (HasPEOptionalHeader)
        {
            // Ignore the caller.
            // This is a COFF with a full PE optional header, and has a known base.
            ImageBase = OptionalHeader->ImageBase & ~0xC0000000;
            SizeOfImage = OptionalHeader->SizeOfImage;
        }
#if 0
        else if (HasRelocations) {
            SizeOfImage = OptionalHeader->SizeOfCode + OptionalHeader->SizeOfInitializedData + OptionalHeader->SizeOfUninitializedData;
            ImageBase = (TopAddress - SizeOfImage) - ~(PAGE_SIZE - 1);
        }
        else {
            // a COFF with no relocations
            ImageBase = OptionalHeader->BaseOfCode;
            SizeOfImage = OptionalHeader->BaseOfData + OptionalHeader->SizeOfInitializedData - ImageBase;
        }
#endif

        //ULONG ImageBasePage = (ImageBase & 0x1FFFFFFF) >> PAGE_SHIFT;

        // If the last section is named ".debug", don't load it
        if (!strcmp(&Sections[NumberOfSections - 1].Name, ".debug")) {
            NumberOfSections--;
            SizeOfImage -= Sections[NumberOfSections].SizeOfRawData;
            OptionalHeader->SizeOfImage = SizeOfImage;
            FileHeader->NumberOfSections = NumberOfSections;
        }

        if (NumberOfSections == 0) {
            // nothing to load?
            printf("No sections to load\n");
            Status = _EBADF;
            break;
        }

        ULONG ImageBaseK0 = (ULONG)MEM_PHYSICAL_TO_K0(ImageBase);
        *LowAddress = ImageBaseK0;
        *EntryAddress = ImageBaseK0 + OptionalHeader->AddressOfEntryPoint - OptionalHeader->BaseOfCode;
        if (ImageBasePage) *ImageBasePage = ImageBase >> PAGE_SHIFT;
        if (ImageSizePage) *ImageSizePage = (SizeOfImage + PAGE_SIZE - 1) >> PAGE_SHIFT;

        // Allocate and zero the relocation table if needed.
#if 0
        if (HasRelocations && !HasPEOptionalHeader) {
            size_t LenRelocs = sizeof(RelocationTable[0]) * NumberOfSections;
            RelocationTable = malloc(LenRelocs);
            if (RelocationTable == NULL) {
                Status = _ENOMEM;
                break;
            }
            memset(RelocationTable, 0, LenRelocs);
        }
#endif

        ULONG SectionOffset = 0;

        // Load sections into memory.
        USHORT NumberOfSectionsToLoad = NumberOfSections;
        //if (IsLittleEndian) NumberOfSectionsToLoad--;
        for (int i = 0; i < NumberOfSectionsToLoad; i++) {
            ULONG Flags = Sections[i].Characteristics;
            ULONG SectionBase = ImageBaseK0 + SectionOffset;

#if 0
            if (HasRelocations && !HasPEOptionalHeader) {
                PSECTION_RELOCATION_ENTRY RelocEntry = &RelocationTable[i];
                RelocEntry->FixupValue = SectionBase - Sections[i].VirtualAddress;
                RelocEntry->NumberOfRelocations = Sections[i].NumberOfRelocations;
                RelocEntry->PointerToRelocations = Sections[i].PointerToRelocations;
            }
            else
#endif
            {
                SectionBase = Sections[i].VirtualAddress;
                //if (HasPEOptionalHeader)
                {
                    SectionBase += ImageBaseK0;
                }
            }

            // If needed, read this section into memory
            bool IncreaseOffset = false;
            if ((Flags & SECTION_REQUIRES_LOAD) != 0) {
                LARGE_INTEGER SeekPosition = Int64ToLargeInteger(Sections[i].PointerToRawData);
                Status = Api->SeekRoutine(FileId, &SeekPosition, SeekAbsolute);
                if (ARC_FAIL(Status)) break;
                Status = Api->ReadRoutine(FileId, (PVOID)SectionBase, Sections[i].SizeOfRawData, &Count);
                if (ARC_FAIL(Status)) break;
                if (Count.v != Sections[i].SizeOfRawData) {
                    printf("Tried to read %x bytes and read %x bytes\n", Sections[i].SizeOfRawData, Count.v);
                    Status = _EFAULT;
                    break;
                }
                IncreaseOffset = true;
            }
            else if ((Flags & SECTION_REQUIRES_ZERO) != 0) {
                memset((PVOID)SectionBase, 0, Sections[i].SizeOfRawData);
                IncreaseOffset = true;
            }

            if (IncreaseOffset) {
                SectionOffset += Sections[i].SizeOfRawData;
            }
        }
        if (ARC_FAIL(Status)) break;

        // Relocate the COFF.
#if 0 // Required code patching is done here (to fix overly strict processor checks). Enforce it.
        if (HasRelocations || IsLittleEndian)
#endif
        {
            Status = RelocatePE(ImageBaseK0, FileHeader, OptionalHeader, Sections);
#if 0
            if (HasPEOptionalHeader) {
                Status = RelocatePE(ImageBaseK0, FileHeader, OptionalHeader, Sections);
            }
            else {
                Status = RelocateCoff(FileId, RelocationTable, NumberOfSections, FileHeader->PointerToSymbolTable);
            }
#endif
        }

        if (ARC_FAIL(Status)) break;
        s_OldCoffLoaded = IsOldCoff;

#if 0 // no more hooking any more, no more hooking any more
        // For a COFF binary with a PE optional header; we need to hook things.
#if 0
        if (HasPEOptionalHeader)
#endif
        {
            // Read entire symbol table to scratch memory in DDR
            Status = ReadCoffSymbolTable(FileId, FileHeader->PointerToSymbolTable, FileHeader->NumberOfSymbols);
            if (ARC_FAIL(Status)) break;

            //printf("Image Base: %08x\r\n", ImageBaseK0);
            // Get address of BlOpen, BlFileTable, BlSetupForNt using COFF symbol table
            PVOID BlOpen = GetSymbolEntryCoff(ImageBaseK0, "..BlOpen");
            PVOID BlFileTable = GetSymbolEntryCoff(ImageBaseK0, "BlFileTable");
            PVOID BlSetupForNt = GetSymbolEntryCoff(ImageBaseK0, "..BlSetupForNt");
            PVOID BlReadSignature = GetSymbolEntryCoff(ImageBaseK0, "..BlReadSignature");
            if (BlOpen != NULL && BlFileTable != NULL && BlSetupForNt != NULL && BlReadSignature != NULL) {
                // This must be an osloader binary (linked with ARC bootlib)
                OslHookInit(BlOpen, BlFileTable, BlSetupForNt, BlReadSignature);
            }
        }
#endif

        // Flush all caches.
        Api->FlushAllCachesRoutine();
    } while (false);
    if (RelocationTable != NULL) free(RelocationTable);
    Api->CloseRoutine(FileId);
    return Status;
}

/// <summary>
/// Loads a COFF executable.
/// </summary>
/// <param name="ImagePath">Path of the executable to load.</param>
/// <param name="TopAddress">Address to load the COFF to</param>
/// <param name="EntryAddress">Entry point is written here</param>
/// <param name="LowAddress">End address of the loaded executable is written here</param>
/// <returns>ARC status code</returns>
static ARC_STATUS ArcLoad(
    IN PCHAR ImagePath,
    IN ULONG TopAddress,
    OUT PU32LE EntryAddress,
    OUT PU32LE LowAddress
) {
    ULONG LocalEntry, LocalLow;
    ARC_STATUS Status = ArcLoadImpl(ImagePath, TopAddress, &LocalEntry, &LocalLow, NULL, NULL);
    if (ARC_FAIL(Status)) return Status;
    if (EntryAddress != NULL) EntryAddress->v = LocalEntry;
    if (LowAddress != NULL) LowAddress->v = LocalLow;
    return Status;
}

/// <summary>
/// Calls the entry point of a previously loaded program.
/// </summary>
/// <param name="EntryAddress">Entry point to call.</param>
/// <param name="StackAddress">Stack pointer to set.</param>
/// <param name="Argc">Number of arguments.</param>
/// <param name="Argv">Array of arguments.</param>
/// <param name="Envp">Environment variables.</param>
/// <returns>ARC status code.</returns>
static ARC_STATUS ArcInvoke(
    IN ULONG EntryAddress,
    IN ULONG StackAddress,
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[]
) {
    // Both entry point and stack pointer must be aligned to 32 bits.
    if ((EntryAddress & 3) != 0 || (StackAddress & 3) != 0) {
        printf("Unaligned entry/stack addrs\n");
        return _EFAULT;
    }

    // If we have loaded an older COFF executable, fix the memory chunks to set anything > 8MB and < 256MB as LoadedProgram.
    if (s_OldCoffLoaded && !s_MemoryMapFixedForOldCoff) {
        PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
        for (
            PMEMORY_DESCRIPTOR MemChunk = Api->MemoryRoutine(NULL);
            MemChunk != NULL;
            MemChunk = Api->MemoryRoutine(MemChunk)
        ) {
            // Looking for a free memchunk
            if (MemChunk->MemoryType != MemoryFree) continue;

            // Starting from 8MB
            if (MemChunk->BasePage < (0x800000 / PAGE_SIZE)) continue;

            // Ending at or before 256MB
            ULONG ChunkEnd = MemChunk->BasePage + MemChunk->PageCount;
            if (ChunkEnd > (256 * (0x100000 / PAGE_SIZE))) continue;

            // Mark it as LoadedProgram.
            // (Motorola and IBM ARC firmware implementations do this)
            // Older implementations use FirmwareTemporary, but if we have a false positive then newer NT bootloaders will detect LoadedProgram here and treat it as free.
            MemChunk->MemoryType = MemoryLoadedProgram;
        }

        s_MemoryMapFixedForOldCoff = true;
    }

    // Stack pointer is otherwise unused, do not change it.

    PU32LE CallingConv = (PU32LE)EntryAddress;
    //printf("Entry point: %08x - toc: %08x\r\n", CallingConv[0].v, CallingConv[1].v);
    // Read from the entry point to make sure it's mapped, yay for having pagetables instead of BATs!
    *(volatile ULONG*)(CallingConv[0].v);
    extern void __ArcInvokeImpl(ULONG EntryAddress, ULONG Toc, ULONG Argc, PCHAR Argv[], PCHAR Envp[]);
    __ArcInvokeImpl(CallingConv[0].v, CallingConv[1].v, Argc, Argv, Envp);
    return _ESUCCESS;
}

// Copy arguments to a buffer handled by the ARC firmware implementation
static ARC_STATUS CopyArguments(ULONG Argc, PCHAR Argv[], PCHAR Envp[]) {
    SavedArgs.Argc = Argc;
    memset(SavedArgs.Arguments, 0, sizeof(SavedArgs.Arguments));
    ULONG Length = sizeof(SavedArgs.Arguments);
    ULONG Offset = 0;
    for (ULONG Arg = 0; Arg < Argc; Arg++) {
        ULONG RemainingLength = Length - Offset;
        int err = snprintf(&SavedArgs.Arguments[Offset], RemainingLength, "%s", Argv[Arg]);
        if (err < 0) {
            printf("Could not copy argument %d\n", Arg);
            return _EFAULT;
        }
        if (err >= RemainingLength) return _E2BIG;
        SavedArgs.Argv[Arg].v = (ULONG)&SavedArgs.Arguments[Offset];
        Offset += err;
        SavedArgs.Arguments[Offset] = 0;
        Offset++;
    }
    memset(SavedArgs.Envp, 0, sizeof(SavedArgs.Envp));
    for (ULONG Arg = 0; Envp[Arg] != NULL; Arg++) {
        SavedArgs.Envp[Arg].v = (ULONG)Envp[Arg];
    }
    return _ESUCCESS;
}


/// <summary>
/// Loads and executes a COFF executable.
/// </summary>
/// <param name="ImagePath">Path of the executable to load.</param>
/// <param name="Argc">Number of arguments.</param>
/// <param name="Argv">Array of arguments.</param>
/// <param name="Envp">Environment variables.</param>
/// <returns>ARC status code.</returns>
static ARC_STATUS ArcExecute(
    IN PCHAR ImagePath,
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[]
) {
    PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
    char CopyPath[260];

    // Copy the arguments and path so the provided buffers can be overwritten if needed
    ARC_STATUS Status = CopyArguments(Argc, Argv, Envp);
    if (ARC_FAIL(Status)) return Status;
    int err = snprintf(CopyPath, sizeof(CopyPath), "%s", ImagePath);
    if (err < 0) return _EFAULT;
    if (err >= sizeof(CopyPath)) return _E2BIG;

    // Find an area of memory.
    for (
        PMEMORY_DESCRIPTOR MemChunk = Api->MemoryRoutine(NULL);
        MemChunk != NULL;
        MemChunk = Api->MemoryRoutine(MemChunk)
    ) {
        // Looking for a free memchunk
        if (MemChunk->MemoryType != MemoryFree) continue;
        // At least 4MB
        if (MemChunk->PageCount < (ARC_MB(4) / PAGE_SIZE)) continue;

        // Try to load here
        ULONG EntryPoint, BaseAddress, ImageBasePage, ImageSizePage;
        Status = ArcLoadImpl(CopyPath, MemChunk->BasePage + MemChunk->PageCount, &EntryPoint, &BaseAddress, &ImageBasePage, &ImageSizePage);
        if (ARC_FAIL(Status)) {
            if (Status != _ENOMEM) return Status;
            continue;
        }

        // Find the memory descriptor used.
        PMEMORY_DESCRIPTOR UsedChunk = ArcMemFindChunk(ImageBasePage, ImageSizePage);

        if (UsedChunk == NULL) {
            // what?
            printf("Could not find used mem chunk\n");
            return _EFAULT;
        }

        // Initialise a memchunk for this executable
        Status = ArcMemAllocateFromFreeChunk(UsedChunk, ImageBasePage, ImageSizePage, MemoryLoadedProgram);
        if (ARC_FAIL(Status)) return Status;

        // Dump memchunks for debug.
#if 0
        printf("Prg: %08x-%08x\r\n", ImageBasePage * PAGE_SIZE, (ImageBasePage + ImageSizePage) * PAGE_SIZE);
        for (PMEMORY_DESCRIPTOR MemChunk = Api->MemoryRoutine(NULL); MemChunk != NULL; MemChunk = Api->MemoryRoutine(MemChunk)) {
            printf("%x: %08x-%08x\r\n", MemChunk->MemoryType, MemChunk->BasePage * PAGE_SIZE, (MemChunk->BasePage + MemChunk->PageCount) * PAGE_SIZE);
        }
        IOSKBD_ReadChar();
#endif

        Status = Api->InvokeRoutine(EntryPoint, BaseAddress, SavedArgs.Argc, (PCHAR*) SavedArgs.Argv, (PCHAR*) SavedArgs.Envp);
        if (ARC_SUCCESS(Status)) UsedChunk->MemoryType = MemoryLoadedProgram;
        return Status;
    }

    return _ENOMEM;
}

void ArcLoadInit(void) {
    PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
    Api->LoadRoutine = ArcLoad;
    Api->InvokeRoutine = ArcInvoke;
    Api->ExecuteRoutine = ArcExecute;

    // Initialise the scratch address.
    // Try to allocate some memory for this, hopefully in an area that cannot be used by boot.
    ScratchAddress = ArcMemAllocTemp(ARCFW_MEM2_SIZE);
}
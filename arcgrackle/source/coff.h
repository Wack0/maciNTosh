#pragma once

#include "types.h"

// MZ header
typedef struct ARC_LE ARC_PACKED _IMAGE_DOS_HEADER {      // DOS .EXE header
    WORD   e_magic;                     // Magic number
    WORD   e_cblp;                      // Bytes on last page of file
    WORD   e_cp;                        // Pages in file
    WORD   e_crlc;                      // Relocations
    WORD   e_cparhdr;                   // Size of header in paragraphs
    WORD   e_minalloc;                  // Minimum extra paragraphs needed
    WORD   e_maxalloc;                  // Maximum extra paragraphs needed
    WORD   e_ss;                        // Initial (relative) SS value
    WORD   e_sp;                        // Initial SP value
    WORD   e_csum;                      // Checksum
    WORD   e_ip;                        // Initial IP value
    WORD   e_cs;                        // Initial (relative) CS value
    WORD   e_lfarlc;                    // File address of relocation table
    WORD   e_ovno;                      // Overlay number
    WORD   e_res[4];                    // Reserved words
    WORD   e_oemid;                     // OEM identifier (for e_oeminfo)
    WORD   e_oeminfo;                   // OEM information; e_oemid specific
    WORD   e_res2[10];                  // Reserved words
    LONG   e_lfanew;                    // File address of new exe header
} IMAGE_DOS_HEADER, * PIMAGE_DOS_HEADER;

enum {
    IMAGE_DOS_SIGNATURE = 0x5A4D // 0x4D5A
};

// COFF header
typedef struct ARC_LE _IMAGE_FILE_HEADER {
    USHORT  Machine;
    USHORT  NumberOfSections;
    ULONG   TimeDateStamp;
    ULONG   PointerToSymbolTable;
    ULONG   NumberOfSymbols;
    USHORT  SizeOfOptionalHeader;
    USHORT  Characteristics;
} IMAGE_FILE_HEADER, * PIMAGE_FILE_HEADER;

enum {
    IMAGE_FILE_RELOCS_STRIPPED = ARC_BIT(0),
    IMAGE_FILE_EXECUTABLE_IMAGE = ARC_BIT(1),
    IMAGE_FILE_LINE_NUMS_STRIPPED = ARC_BIT(2),
    IMAGE_FILE_LOCAL_SYMS_STRIPPED = ARC_BIT(3),
    IMAGE_FILE_BYTES_REVERSED_LOW = ARC_BIT(7),
    IMAGE_FILE_32BIT_ARCHITECTURE = ARC_BIT(8),
    IMAGE_FILE_DEBUG_STRIPPED = ARC_BIT(9),
    IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP = ARC_BIT(10),
    IMAGE_FILE_NET_RUN_FROM_SWAP = ARC_BIT(11),
    IMAGE_FILE_SYSTEM = ARC_BIT(12),
    IMAGE_FILE_DLL = ARC_BIT(13),
    IMAGE_FILE_BYTES_REVERSED_HIGH = ARC_BIT(15)
};

enum {
    IMAGE_FILE_MACHINE_POWERPC = 0x1F0,
    IMAGE_FILE_MACHINE_POWERPCBE = 0x1F2
};

// COFF optional header
typedef struct ARC_LE _IMAGE_OPTIONAL_HEADER_COFF {
    USHORT  Magic;
    UCHAR   MajorLinkerVersion;
    UCHAR   MinorLinkerVersion;
    ULONG   SizeOfCode;
    ULONG   SizeOfInitializedData;
    ULONG   SizeOfUninitializedData;
    ULONG   AddressOfEntryPoint;
    ULONG   BaseOfCode;
    ULONG   BaseOfData;
} IMAGE_OPTIONAL_HEADER_COFF, * PIMAGE_OPTIONAL_HEADER_COFF;

// PE directory entry
typedef struct ARC_LE _IMAGE_DATA_DIRECTORY {
    ULONG   VirtualAddress;
    ULONG   Size;
} IMAGE_DATA_DIRECTORY, * PIMAGE_DATA_DIRECTORY;

enum {
    IMAGE_NUMBEROF_DIRECTORY_ENTRIES = 16
};

// PE optional header
typedef struct ARC_LE _IMAGE_OPTIONAL_HEADER {
    // COFF fields (like IMAGE_OPTIONAL_HEADER_COFF)
    USHORT  Magic;
    UCHAR   MajorLinkerVersion;
    UCHAR   MinorLinkerVersion;
    ULONG   SizeOfCode;
    ULONG   SizeOfInitializedData;
    ULONG   SizeOfUninitializedData;
    ULONG   AddressOfEntryPoint;
    ULONG   BaseOfCode;
    ULONG   BaseOfData;

    //
    // NT additional fields.
    //

    ULONG   ImageBase;
    ULONG   SectionAlignment;
    ULONG   FileAlignment;
    USHORT  MajorOperatingSystemVersion;
    USHORT  MinorOperatingSystemVersion;
    USHORT  MajorImageVersion;
    USHORT  MinorImageVersion;
    USHORT  MajorSubsystemVersion;
    USHORT  MinorSubsystemVersion;
    ULONG   Reserved1;
    ULONG   SizeOfImage;
    ULONG   SizeOfHeaders;
    ULONG   CheckSum;
    USHORT  Subsystem;
    USHORT  DllCharacteristics;
    ULONG   SizeOfStackReserve;
    ULONG   SizeOfStackCommit;
    ULONG   SizeOfHeapReserve;
    ULONG   SizeOfHeapCommit;
    ULONG   LoaderFlags;
    ULONG   NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER, * PIMAGE_OPTIONAL_HEADER;

enum {
    IMAGE_NT_OPTIONAL_HDR_MAGIC = 0x10b,

    IMAGE_SUBSYSTEM_NATIVE = 1
};

// PE headers
typedef struct ARC_LE _IMAGE_NT_HEADERS {
    ULONG Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER OptionalHeader;
} IMAGE_NT_HEADERS, * PIMAGE_NT_HEADERS;

enum {
    IMAGE_NT_SIGNATURE = 0x00004550
};

// COFF section header

enum {
    IMAGE_SIZEOF_SHORT_NAME = 8
};

typedef struct ARC_LE _IMAGE_SECTION_HEADER {
    UCHAR   Name[IMAGE_SIZEOF_SHORT_NAME];
    union ARC_LE {
        ULONG   PhysicalAddress;
        ULONG   VirtualSize;
    } Misc;
    ULONG   VirtualAddress;
    ULONG   SizeOfRawData;
    ULONG   PointerToRawData;
    ULONG   PointerToRelocations;
    ULONG   PointerToLinenumbers;
    USHORT  NumberOfRelocations;
    USHORT  NumberOfLinenumbers;
    ULONG   Characteristics;
} IMAGE_SECTION_HEADER, * PIMAGE_SECTION_HEADER;

enum {
    IMAGE_SCN_TYPE_NO_PAD = 0x00000008, // Reserved.

    IMAGE_SCN_CNT_CODE = 0x00000020, // Section contains code.
    IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040, // Section contains initialized data.
    IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080, // Section contains uninitialized data.

    IMAGE_SCN_LNK_OTHER = 0x00000100, // Reserved.
    IMAGE_SCN_LNK_INFO = 0x00000200, // Section contains comments or some other type of information.
    IMAGE_SCN_LNK_REMOVE = 0x00000800, // Section contents will not become part of image.
    IMAGE_SCN_LNK_COMDAT = 0x00001000, // Section contents comdat.

    IMAGE_SCN_ALIGN_1BYTES = 0x00100000, //
    IMAGE_SCN_ALIGN_2BYTES = 0x00200000, //
    IMAGE_SCN_ALIGN_4BYTES = 0x00300000, //
    IMAGE_SCN_ALIGN_8BYTES = 0x00400000, //
    IMAGE_SCN_ALIGN_16BYTES = 0x00500000, // Default alignment if no others are specified.
    IMAGE_SCN_ALIGN_32BYTES = 0x00600000, //
    IMAGE_SCN_ALIGN_64BYTES = 0x00700000, //

    IMAGE_SCN_MEM_DISCARDABLE = 0x02000000, // Section can be discarded.
    IMAGE_SCN_MEM_NOT_CACHED = 0x04000000, // Section is not cachable.
    IMAGE_SCN_MEM_NOT_PAGED = 0x08000000, // Section is not pageable.
    IMAGE_SCN_MEM_SHARED = 0x10000000, // Section is shareable.
    IMAGE_SCN_MEM_EXECUTE = 0x20000000, // Section is executable.
    IMAGE_SCN_MEM_READ = 0x40000000, // Section is readable.
    IMAGE_SCN_MEM_WRITE = 0x80000000, // Section is writeable.
};

// COFF relocation
typedef struct ARC_PACKED ARC_LE _IMAGE_RELOCATION {
    union {
        ULONG   VirtualAddress;
        ULONG   RelocCount;             // Set to the real count when IMAGE_SCN_LNK_NRELOC_OVFL is set
    };
    ULONG   SymbolTableIndex;
    USHORT  Type;
} IMAGE_RELOCATION;

enum {
    IMAGE_REL_PPC_ABSOLUTE = 0x0000, // NOP
    IMAGE_REL_PPC_ADDR64 = 0x0001, // 64-bit address
    IMAGE_REL_PPC_ADDR32 = 0x0002, // 32-bit address
    IMAGE_REL_PPC_ADDR24 = 0x0003, // 26-bit address, shifted left 2 (branch absolute)
    IMAGE_REL_PPC_ADDR16 = 0x0004, // 16-bit address
    IMAGE_REL_PPC_ADDR14 = 0x0005, // 16-bit address, shifted left 2 (load doubleword)
    IMAGE_REL_PPC_REL24 = 0x0006, // 26-bit PC-relative offset, shifted left 2 (branch relative)
    IMAGE_REL_PPC_REL14 = 0x0007, // 16-bit PC-relative offset, shifted left 2 (br cond relative)
    IMAGE_REL_PPC_TOCREL16 = 0x0008, // 16-bit offset from TOC base
    IMAGE_REL_PPC_TOCREL14 = 0x0009, // 16-bit offset from TOC base, shifted left 2 (load doubleword)

    IMAGE_REL_PPC_ADDR32NB = 0x000A, // 32-bit addr w/o image base
    IMAGE_REL_PPC_SECREL = 0x000B, // va of containing section (as in an image sectionhdr)
    IMAGE_REL_PPC_SECTION = 0x000C, // sectionheader number

    IMAGE_REL_PPC_TYPEMASK = 0x00FF, // mask to isolate above values in IMAGE_RELOCATION.Type

    // Flag bits in IMAGE_RELOCATION.TYPE

    IMAGE_REL_PPC_NEG = 0x0100, // subtract reloc value rather than adding it
    IMAGE_REL_PPC_BRTAKEN = 0x0200, // fix branch prediction bit to predict branch taken
    IMAGE_REL_PPC_BRNTAKEN = 0x0400, // fix branch prediction bit to predict branch not taken
    IMAGE_REL_PPC_TOCDEFN = 0x0800, // toc slot defined in file (or, data in toc)
};

// COFF symbol
typedef struct ARC_PACKED ARC_LE _IMAGE_SYMBOL {
    union ARC_LE {
        UCHAR   ShortName[8];
        struct ARC_LE {
            ULONG   Short;     // if 0, use LongName
            ULONG   Long;      // offset into string table
        } Name;
        PUCHAR  LongName[2];
    } N;
    ULONG   Value;
    SHORT   SectionNumber;
    USHORT  Type;
    UCHAR   StorageClass;
    UCHAR   NumberOfAuxSymbols;
} IMAGE_SYMBOL, *PIMAGE_SYMBOL;

enum {
    IMAGE_SYM_CLASS_EXTERNAL = 2
};

// Directory entries
enum {
    IMAGE_DIRECTORY_ENTRY_EXPORT       = 0,  // Export Directory
    IMAGE_DIRECTORY_ENTRY_IMPORT       = 1,  // Import Directory
    IMAGE_DIRECTORY_ENTRY_RESOURCE     = 2,  // Resource Directory
    IMAGE_DIRECTORY_ENTRY_EXCEPTION    = 3,  // Exception Directory
    IMAGE_DIRECTORY_ENTRY_SECURITY     = 4,  // Security Directory
    IMAGE_DIRECTORY_ENTRY_BASERELOC    = 5,  // Base Relocation Table
    IMAGE_DIRECTORY_ENTRY_DEBUG        = 6,  // Debug Directory
    IMAGE_DIRECTORY_ENTRY_COPYRIGHT    = 7,  // Description String
    IMAGE_DIRECTORY_ENTRY_GLOBALPTR    = 8,  // Machine Value (MIPS GP)
    IMAGE_DIRECTORY_ENTRY_TLS          = 9,  // TLS Directory
    IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG  = 10, // Load Configuration Directory
    IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT = 11, // Bound Imports
};

// Exception directory entry
typedef struct ARC_LE _RUNTIME_FUNCTION_ENTRY
{
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG ExceptionHandler;
    ULONG HandlerData;
    ULONG PrologEndAddress;
} RUNTIME_FUNCTION_ENTRY, *PRUNTIME_FUNCTION_ENTRY;

// PE relocation directory.
typedef struct ARC_LE _IMAGE_BASE_RELOCATION {
    ULONG   VirtualAddress;
    ULONG   SizeOfBlock;
} IMAGE_BASE_RELOCATION, *PIMAGE_BASE_RELOCATION;

enum {
    IMAGE_REL_BASED_ABSOLUTE = 0,
    IMAGE_REL_BASED_HIGH = 1,
    IMAGE_REL_BASED_LOW = 2,
    IMAGE_REL_BASED_HIGHLOW = 3,
    IMAGE_REL_BASED_HIGHADJ = 4,
    IMAGE_REL_BASED_MIPS_JMPADDR = 5,
};

// PE export directory
typedef struct ARC_LE _IMAGE_EXPORT_DIRECTORY {
    ULONG Characteristics;
    ULONG TimeDateStamp;
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG Name;
    ULONG Base;
    ULONG NumberOfFunctions;
    ULONG NumberOfNames;
    ULONG AddressOfFunctions;
    ULONG AddressOfNames;
    ULONG AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY, * PIMAGE_EXPORT_DIRECTORY;

// PE import descriptor
typedef struct ARC_LE _IMAGE_IMPORT_DESCRIPTOR {
    union {
        ULONG Characteristics;            // 0 for terminating null import descriptor
        ULONG OriginalFirstThunk;         // RVA to original unbound IAT (PIMAGE_THUNK_DATA)
    };
    ULONG   TimeDateStamp;                  // 0 if not bound,
    // -1 if bound, and real date\time stamp
    //     in IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT (new BIND)
    // O.W. date/time stamp of DLL bound to (Old BIND)

    ULONG   ForwarderChain;                 // -1 if no forwarders
    ULONG   Name;
    ULONG   FirstThunk;                     // RVA to IAT (if bound this IAT has actual addresses)
} IMAGE_IMPORT_DESCRIPTOR, *PIMAGE_IMPORT_DESCRIPTOR;

// Alignment macros
#define ARC_ALIGNDOWN(x, align) ((x) & -(align))
#define ARC_ALIGNUP(x, align) (-(-(x) & -(align)))

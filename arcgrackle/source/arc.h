#pragma once

#include "types.h"

enum {
    PAGE_SHIFT = 12
};
enum {
    PAGE_SIZE = (1 << PAGE_SHIFT)
};

#define MEM_K0_TO_PHYSICAL(x) (void*)((ULONG)(x)-0x80000000)
#define MEM_PHYSICAL_TO_K0(x) (void*)((ULONG)(x)+0x80000000)

enum {
    ARC_VERSION_MAJOR = 1,
    ARC_VERSION_MINOR = 2
};

typedef enum _ARC_CODES {
    _ESUCCESS,
    _E2BIG,
    _EACCES,
    _EAGAIN,
    _EBADF,
    _EBUSY,
    _EFAULT,
    _EINVAL,
    _EIO,
    _EISDIR,
    _EMFILE,
    _EMLINK,
    _ENAMETOOLONG,
    _ENODEV,
    _ENOENT,
    _ENOEXEC,
    _ENOMEM,
    _ENOSPC,
    _ENOTDIR,
    _ENOTTY,
    _ENXIO,
    _EROFS,
    _EMAXIMUM
} ARC_CODES;

#define ARC_SUCCESS(code) ((code) == _ESUCCESS)
#define ARC_FAIL(code) ((code) != _ESUCCESS)

enum {
	ARC_CONSOLE_INPUT = 0,
	ARC_CONSOLE_OUTPUT
};

typedef uint32_t ARC_STATUS;

typedef enum _CONFIGURATION_CLASS {
    SystemClass,
    ProcessorClass,
    CacheClass,
    AdapterClass,
    ControllerClass,
    PeripheralClass,
    MemoryClass,
    MaximumClass
} CONFIGURATION_CLASS, * PCONFIGURATION_CLASS;

typedef enum _CONFIGURATION_TYPE {
    ArcSystem,
    CentralProcessor,
    FloatingPointProcessor,
    PrimaryIcache,
    PrimaryDcache,
    SecondaryIcache,
    SecondaryDcache,
    SecondaryCache,
    EisaAdapter,
    TcAdapter,
    ScsiAdapter,
    DtiAdapter,
    MultiFunctionAdapter,
    DiskController,
    TapeController,
    CdromController,
    WormController,
    SerialController,
    NetworkController,
    DisplayController,
    ParallelController,
    PointerController,
    KeyboardController,
    AudioController,
    OtherController,
    DiskPeripheral,
    FloppyDiskPeripheral,
    TapePeripheral,
    ModemPeripheral,
    MonitorPeripheral,
    PrinterPeripheral,
    PointerPeripheral,
    KeyboardPeripheral,
    TerminalPeripheral,
    OtherPeripheral,
    LinePeripheral,
    NetworkPeripheral,
    SystemMemory,
    MaximumType,
    PartitionEntry
} CONFIGURATION_TYPE, * PCONFIGURATION_TYPE;

typedef enum _CM_RESOURCE_TYPE {
    CmResourceTypeNull = 0, // Reserved
    CmResourceTypePort,
    CmResourceTypeInterrupt,
    CmResourceTypeMemory,
    CmResourceTypeDma,
    CmResourceTypeDeviceSpecific,
    CmResourceTypeVendor,
    CmResourceTypeProductName,
    CmResourceTypeSerialNumber
} CM_RESOURCE_TYPE;

typedef enum _CM_SHARE_DISPOSITION {
    CmResourceShareUndetermined = 0,
    CmResourceShareDeviceExclusive,
    CmResourceShareDriverExclusive,
    CmResourceShareShared
} CM_SHARE_DISPOSITION;

enum {
    CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE = 0,
    CM_RESOURCE_INTERRUPT_LATCHED = 1
};

enum {
    CM_RESOURCE_MEMORY_READ_WRITE = 0,
    CM_RESOURCE_MEMORY_READ_ONLY = 1,
    CM_RESOURCE_MEMORY_WRITE_ONLY = 2
};

enum {
    CM_RESOURCE_PORT_MEMORY = 0,
    CM_RESOURCE_PORT_IO = 1
};


typedef enum _ARC_DEVICE_FLAGS {
    ARC_DEVICE_NONE = 0,
    ARC_DEVICE_FAILED = ARC_BIT(0),
    ARC_DEVICE_READONLY = ARC_BIT(1),
    ARC_DEVICE_REMOVABLE = ARC_BIT(2),
    ARC_DEVICE_CONSOLE_IN = ARC_BIT(3),
    ARC_DEVICE_CONSOLE_OUT = ARC_BIT(4),
    ARC_DEVICE_INPUT = ARC_BIT(5),
    ARC_DEVICE_OUTPUT = ARC_BIT(6)
} ARC_DEVICE_FLAGS;

typedef struct ARC_LE _CONFIGURATION_COMPONENT {
    CONFIGURATION_CLASS Class;
    CONFIGURATION_TYPE Type;
    ULONG Flags;
    USHORT Version;
    USHORT Revision;
    ULONG Key;
    ULONG AffinityMask;
    ULONG ConfigurationDataLength;
    ULONG IdentifierLength;
    size_t Identifier;
} CONFIGURATION_COMPONENT, * PCONFIGURATION_COMPONENT;

typedef struct ARC_LE ARC_ALIGNED(4) ARC_PACKED _CM_PARTIAL_RESOURCE_DESCRIPTOR {
    UCHAR Type;
    UCHAR ShareDisposition;
    USHORT Flags;
    union ARC_LE ARC_ALIGNED(4) ARC_PACKED {

        //
        // Range of port numbers, inclusive. These are physical, bus
        // relative.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            PHYSICAL_ADDRESS Start;
            ULONG Length;
        } Port;

        //
        // IRQL and vector.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            ULONG Level;
            ULONG Vector;
            ULONG Affinity;
        } Interrupt;

        //
        // Range of memory addresses, inclusive. These are physical, bus
        // relative.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            PHYSICAL_ADDRESS Start;    // 64 bit physical addresses.
            ULONG Length;
        } Memory;

        //
        // Physical DMA channel.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            ULONG Channel;
            ULONG Port;
            ULONG Reserved1;
        } Dma;

        //
        // Vendor string.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            CHAR Vendor[12];
        } Vendor;

        //
        // Product name string.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            CHAR ProductName[12];
        } ProductName;

        //
        // Serial Number string.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            CHAR SerialNumber[12];
        } SerialNumber;

        //
        // Device Specific information defined by the driver.
        // The DataSize field indicates the size of the data in bytes. The
        // data is located immediately after the DeviceSpecificData field in
        // the structure.
        //

        struct ARC_LE ARC_ALIGNED(4) ARC_PACKED {
            ULONG DataSize;
            ULONG Reserved1;
            ULONG Reserved2;
        } DeviceSpecificData;
    };
} CM_PARTIAL_RESOURCE_DESCRIPTOR, * PCM_PARTIAL_RESOURCE_DESCRIPTOR;
_Static_assert(__builtin_offsetof(CM_PARTIAL_RESOURCE_DESCRIPTOR, Memory.Start.LowPart) == 4);

typedef struct ARC_LE _CM_PARTIAL_RESOURCE_LIST_HEADER {
    USHORT Version;
    USHORT Revision;
    ULONG Count;
} CM_PARTIAL_RESOURCE_LIST_HEADER, * PCM_PARTIAL_RESOURCE_LIST_HEADER;

typedef struct ARC_LE _CM_PARTIAL_RESOURCE_LIST {
    USHORT Version;
    USHORT Revision;
    ULONG Count;
    CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[1];
} CM_PARTIAL_RESOURCE_LIST, * PCM_PARTIAL_RESOURCE_LIST;

typedef struct ARC_LE _SYSTEM_ID {
    CHAR VendorId[8];
    CHAR ProductId[8];
} SYSTEM_ID, * PSYSTEM_ID;

typedef enum _MEMORY_TYPE {
    MemoryExceptionBlock,
    MemorySystemBlock,
    MemoryFree,
    MemoryBad,
    MemoryLoadedProgram,
    MemoryFirmwareTemporary,
    MemoryFirmwarePermanent,
    MemoryFreeContiguous,
    MemorySpecialMemory,
    MemoryMaximum
} MEMORY_TYPE;

typedef struct ARC_LE _MEMORY_DESCRIPTOR {
    MEMORY_TYPE MemoryType;
    ULONG BasePage;
    ULONG PageCount;
} MEMORY_DESCRIPTOR, * PMEMORY_DESCRIPTOR;

typedef struct ARC_LE _TIME_FIELDS {
    CSHORT Year;        // range [1601...]
    CSHORT Month;       // range [1..12]
    CSHORT Day;         // range [1..31]
    CSHORT Hour;        // range [0..23]
    CSHORT Minute;      // range [0..59]
    CSHORT Second;      // range [0..59]
    CSHORT Milliseconds;// range [0..999]
    CSHORT Weekday;     // range [0..6] == [Sunday..Saturday]
} TIME_FIELDS;
typedef TIME_FIELDS* PTIME_FIELDS;

enum {
    ArcReadOnlyFile = ARC_BIT(0),
    ArcHiddenFile = ARC_BIT(1),
    ArcSystemFile = ARC_BIT(2),
    ArcArchiveFile = ARC_BIT(3),
    ArcDirectoryFile = ARC_BIT(4),
    ArcDeleteFile = ARC_BIT(5)
};

typedef enum _OPEN_MODE {
    ArcOpenReadOnly,
    ArcOpenWriteOnly,
    ArcOpenReadWrite,
    ArcCreateWriteOnly,
    ArcCreateReadWrite,
    ArcSupersedeWriteOnly,
    ArcSupersedeReadWrite,
    ArcOpenDirectory,
    ArcCreateDirectory,
    ArcOpenMaximumMode
} OPEN_MODE;

typedef struct ARC_LE _FILE_INFORMATION {
    LARGE_INTEGER StartingAddress;
    LARGE_INTEGER EndingAddress;
    LARGE_INTEGER CurrentPosition;
    CONFIGURATION_TYPE Type;
    ULONG FileNameLength;
    UCHAR Attributes;
    CHAR FileName[32];
} FILE_INFORMATION, * PFILE_INFORMATION;

typedef enum _SEEK_MODE {
    SeekAbsolute,
    SeekRelative,
    SeekMaximum
} SEEK_MODE;

typedef enum _MOUNT_OPERATION {
    MountLoadMedia,
    MountUnloadMedia,
    MountMaximum
} MOUNT_OPERATION;

typedef struct ARC_LE _DIRECTORY_ENTRY {
    ULONG FileNameLength;
    UCHAR FileAttribute;
    CHAR FileName[32];
} DIRECTORY_ENTRY, * PDIRECTORY_ENTRY;

typedef struct ARC_LE _ARC_DISPLAY_STATUS {
    USHORT CursorXPosition;
    USHORT CursorYPosition;
    USHORT CursorMaxXPosition;
    USHORT CursorMaxYPosition;
    UCHAR ForegroundColor;
    UCHAR BackgroundColor;
    BOOLEAN HighIntensity;
    BOOLEAN Underscored;
    BOOLEAN ReverseVideo;
} ARC_DISPLAY_STATUS, * PARC_DISPLAY_STATUS;

// Macros for component related stuff.
#define ARC_MAKE_COMPONENT(Class, Type, Flags, Key, ConfigurationDataLength) (CONFIGURATION_COMPONENT) \
    { Class, Type, ((Flags) & ~ARC_DEVICE_FAILED), ARC_VERSION_MAJOR, ARC_VERSION_MINOR, Key, 0, ConfigurationDataLength, 0, 0 }

#define ARC_RESOURCE_DESCRIPTOR_PORT(Flags, Start, Length) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypePort, CmResourceShareDeviceExclusive, Flags, { .Port = { INT32_TO_LARGE_INTEGER(Start), Length } } })

#define ARC_RESOURCE_DESCRIPTOR_INTERRUPT(Flags, Vector) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypeInterrupt, CmResourceShareDeviceExclusive, Flags, { .Interrupt = { Vector, Vector, 0 } } })

#define ARC_RESOURCE_DESCRIPTOR_MEMORY(Flags, Start, Length) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypeMemory, CmResourceShareDeviceExclusive, Flags, { .Memory = { INT32_TO_LARGE_INTEGER(Start), Length } } })

#define ARC_RESOURCE_DESCRIPTOR_DMA(Channel, Port) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypeDma, CmResourceShareDeviceExclusive, 0, { .Dma = { Channel, Port, 0 } } })

#define ARC_RESOURCE_DESCRIPTOR_VENDOR(VendorName) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypeVendor, CmResourceShareDeviceExclusive, 0, { .Vendor = { VendorName } } })

#define ARC_RESOURCE_DESCRIPTOR_PRODUCT(Product) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypeProductName, CmResourceShareDeviceExclusive, 0, { .ProductName = { Product } } })

#define ARC_RESOURCE_DESCRIPTOR_SERIALNUMBER(Serial) ((CM_PARTIAL_RESOURCE_DESCRIPTOR) \
    { CmResourceTypeSerialNumber, CmResourceShareDeviceExclusive, 0, { .Serial = { Serial } } })



#define ARC_RESOURCE_LIST(Name, ...) \
    struct ARC_LE { \
        CM_PARTIAL_RESOURCE_LIST_HEADER Header; \
        CM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors[GET_ARG_COUNT(__VA_ARGS__)]; \
    } Name = { { ARC_VERSION_MAJOR, ARC_VERSION_MINOR, GET_ARG_COUNT(__VA_ARGS__) }, { __VA_ARGS__ } }


// Firmware function pointers, in order of offset.

typedef
ARC_STATUS
(*PARC_LOAD_ROUTINE) (
    IN PCHAR ImagePath,
    IN ULONG TopAddress,
    OUT PU32LE EntryAddress,
    OUT PU32LE LowAddress
);

typedef
ARC_STATUS
(*PARC_INVOKE_ROUTINE) (
    IN ULONG EntryAddress,
    IN ULONG StackAddress,
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[]
);

typedef
ARC_STATUS
(*PARC_EXECUTE_ROUTINE) (
    IN PCHAR ImagePath,
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[]
);

typedef
VOID
(*PARC_HALT_ROUTINE) (
    VOID
    );

typedef
VOID
(*PARC_POWERDOWN_ROUTINE) (
    VOID
    );

typedef
VOID
(*PARC_RESTART_ROUTINE) (
    VOID
    );

typedef
VOID
(*PARC_REBOOT_ROUTINE) (
    VOID
    );

typedef
VOID
(*PARC_INTERACTIVE_MODE_ROUTINE) (
    VOID
    );

typedef
PCONFIGURATION_COMPONENT
(*PARC_GET_PEER_ROUTINE) (
    IN PCONFIGURATION_COMPONENT Component
    );

typedef
PCONFIGURATION_COMPONENT
(*PARC_GET_CHILD_ROUTINE) (
    IN PCONFIGURATION_COMPONENT Component OPTIONAL
    );

typedef
PCONFIGURATION_COMPONENT
(*PARC_GET_PARENT_ROUTINE) (
    IN PCONFIGURATION_COMPONENT Component
    );

typedef
ARC_STATUS
(*PARC_GET_DATA_ROUTINE) (
    OUT PVOID ConfigurationData,
    IN PCONFIGURATION_COMPONENT Component
    );


typedef
PCONFIGURATION_COMPONENT
(*PARC_ADD_CHILD_ROUTINE) (
    IN PCONFIGURATION_COMPONENT Component,
    IN PCONFIGURATION_COMPONENT NewComponent,
    IN PVOID ConfigurationData
    );

typedef
ARC_STATUS
(*PARC_DELETE_COMPONENT_ROUTINE) (
    IN PCONFIGURATION_COMPONENT Component
    );

typedef
PCONFIGURATION_COMPONENT
(*PARC_GET_COMPONENT_ROUTINE) (
    IN PCHAR Path
    );

typedef
ARC_STATUS
(*PARC_SAVE_CONFIGURATION_ROUTINE) (
    VOID
    );

typedef
PSYSTEM_ID
(*PARC_GET_SYSTEM_ID_ROUTINE) (
    VOID
    );

typedef
PMEMORY_DESCRIPTOR
(*PARC_MEMORY_ROUTINE) (
    IN PMEMORY_DESCRIPTOR MemoryDescriptor OPTIONAL
    );

typedef
PTIME_FIELDS
(*PARC_GET_TIME_ROUTINE) (
    VOID
    );

typedef
ULONG
(*PARC_GET_RELATIVE_TIME_ROUTINE) (
    VOID
    );

typedef
ARC_STATUS
(*PARC_GET_DIRECTORY_ENTRY_ROUTINE) (
    IN ULONG FileId,
    OUT PDIRECTORY_ENTRY Buffer,
    IN ULONG Length,
    OUT PU32LE Count
    );

typedef
ARC_STATUS
(*PARC_GET_DIRECTORY_ENTRY_ROUTINE_INTERNAL) (
    IN ULONG FileId,
    OUT PDIRECTORY_ENTRY Buffer,
    IN ULONG Length,
    OUT PULONG Count
    );

typedef
ARC_STATUS
(*PARC_OPEN_ROUTINE) (
    IN PCHAR OpenPath,
    IN OPEN_MODE OpenMode,
    OUT PU32LE FileId
    );

typedef
ARC_STATUS
(*PARC_OPEN_ROUTINE_INTERNAL) (
    IN PCHAR OpenPath,
    IN OPEN_MODE OpenMode,
    OUT PULONG FileId
    );

typedef
ARC_STATUS
(*PARC_CLOSE_ROUTINE) (
    IN ULONG FileId
    );

typedef
ARC_STATUS
(*PARC_READ_ROUTINE) (
    IN ULONG FileId,
    OUT PVOID Buffer,
    IN ULONG Length,
    OUT PU32LE Count
    );

typedef
ARC_STATUS
(*PARC_READ_ROUTINE_INTERNAL) (
    IN ULONG FileId,
    OUT PVOID Buffer,
    IN ULONG Length,
    OUT PULONG Count
    );

typedef
ARC_STATUS
(*PARC_READ_STATUS_ROUTINE) (
    IN ULONG FileId
    );

typedef
ARC_STATUS
(*PARC_WRITE_ROUTINE) (
    IN ULONG FileId,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PU32LE Count
    );

typedef
ARC_STATUS
(*PARC_WRITE_ROUTINE_INTERNAL) (
    IN ULONG FileId,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG Count
    );

typedef
ARC_STATUS
(*PARC_SEEK_ROUTINE) (
    IN ULONG FileId,
    IN PLARGE_INTEGER Offset,
    IN SEEK_MODE SeekMode
    );

typedef
ARC_STATUS
(*PARC_MOUNT_ROUTINE) (
    IN PCHAR MountPath,
    IN MOUNT_OPERATION Operation
    );

typedef
PCHAR
(*PARC_GET_ENVIRONMENT_ROUTINE) (
    IN PCHAR Variable
    );

typedef
ARC_STATUS
(*PARC_SET_ENVIRONMENT_ROUTINE) (
    IN PCHAR Variable,
    IN PCHAR Value
    );

typedef
ARC_STATUS
(*PARC_GET_FILE_INFO_ROUTINE) (
    IN ULONG FileId,
    OUT PFILE_INFORMATION FileInformation
    );

typedef
ARC_STATUS
(*PARC_SET_FILE_INFO_ROUTINE) (
    IN ULONG FileId,
    IN ULONG AttributeFlags,
    IN ULONG AttributeMask
    );

typedef
VOID
(*PARC_FLUSH_ALL_CACHES_ROUTINE) (
    VOID
    );

typedef
ARC_STATUS
(*PARC_TEST_UNICODE_CHARACTER_ROUTINE) (
    IN ULONG FileId,
    IN WCHAR UnicodeCharacter
    );

typedef
PARC_DISPLAY_STATUS
(*PARC_GET_DISPLAY_STATUS_ROUTINE) (
    IN ULONG FileId
    );

// Data structures starting at 0x8000_4000.
enum {
    ARC_SYSTEM_TABLE_ADDRESS_PHYS = 0x4000,
    ARC_SYSTEM_TABLE_ADDRESS = (uint32_t)MEM_PHYSICAL_TO_K0(ARC_SYSTEM_TABLE_ADDRESS_PHYS),
    ARC_SYSTEM_TABLE_LENGTH = 0xC000 - ARC_SYSTEM_TABLE_ADDRESS_PHYS,
    ARCFW_MEM2_SIZE = 8 * 1024 * 1024
};

// Debug block
typedef struct ARC_LE _DEBUG_BLOCK {
    ULONG Signature;
    ULONG Length;
} DEBUG_BLOCK, * PDEBUG_BLOCK;

// Restart block
enum {
    ARC_RESTART_BLOCK_SIGNATURE = 0x42545352,
    ARC_SYSTEM_BLOCK_SIGNATURE = 0x53435241,
};

typedef struct ARC_LE _BOOT_STATUS {
    ULONG BootStarted : 1;
    ULONG BootFinished : 1;
    ULONG RestartStarted : 1;
    ULONG RestartFinished : 1;
    ULONG PowerFailStarted : 1;
    ULONG PowerFailFinished : 1;
    ULONG ProcessorReady : 1;
    ULONG ProcessorRunning : 1;
    ULONG ProcessorStart : 1;
} BOOT_STATUS, * PBOOT_STATUS;

typedef struct ARC_LE _PPC_RESTART_STATE {

    //
    // Floating register state.
    //

    ULONG FltF0;
    ULONG FltF1;
    ULONG FltF2;
    ULONG FltF3;
    ULONG FltF4;
    ULONG FltF5;
    ULONG FltF6;
    ULONG FltF7;
    ULONG FltF8;
    ULONG FltF9;
    ULONG FltF10;
    ULONG FltF11;
    ULONG FltF12;
    ULONG FltF13;
    ULONG FltF14;
    ULONG FltF15;
    ULONG FltF16;
    ULONG FltF17;
    ULONG FltF18;
    ULONG FltF19;
    ULONG FltF20;
    ULONG FltF21;
    ULONG FltF22;
    ULONG FltF23;
    ULONG FltF24;
    ULONG FltF25;
    ULONG FltF26;
    ULONG FltF27;
    ULONG FltF28;
    ULONG FltF29;
    ULONG FltF30;
    ULONG FltF31;

    //
    // Floating status state.
    //

    ULONG Fsr;

    //
    // Integer register state.
    //

    ULONG IntR0;
    ULONG IntR1;
    ULONG IntR2;
    ULONG IntR3;
    ULONG IntR4;
    ULONG IntR5;
    ULONG IntR6;
    ULONG IntR7;
    ULONG IntR8;
    ULONG IntR9;
    ULONG IntR10;
    ULONG IntR11;
    ULONG IntR12;
    ULONG IntR13;
    ULONG IntR14;
    ULONG IntR15;
    ULONG IntR16;
    ULONG IntR17;
    ULONG IntR18;
    ULONG IntR19;
    ULONG IntR20;
    ULONG IntR21;
    ULONG IntR22;
    ULONG IntR23;
    ULONG IntR24;
    ULONG IntR25;
    ULONG IntR26;
    ULONG IntR27;
    ULONG IntR28;
    ULONG IntR29;
    ULONG IntR30;
    ULONG IntR31;

    ULONG CondR;                        // Condition register
    ULONG XER;                          // Fixed point exception reg

    //
    // Machine state register and instruction address register
    //

    ULONG Msr;
    ULONG Iar;

} PPC_RESTART_STATE, * PPPC_RESTART_STATE;

typedef struct ARC_LE _RESTART_BLOCK {
    ULONG Signature;
    ULONG Length;
    USHORT Version;
    USHORT Revision;
    struct _RESTART_BLOCK* NextRestartBlock;
    PVOID RestartAddress;
    ULONG BootMasterId;
    ULONG ProcessorId;
    volatile BOOT_STATUS BootStatus;
    ULONG CheckSum;
    ULONG SaveAreaLength;
    PPC_RESTART_STATE SaveArea;
} RESTART_BLOCK, * PRESTART_BLOCK;

typedef struct ARC_LE _FIRMWARE_VECTOR_TABLE {
    PARC_LOAD_ROUTINE LoadRoutine;
    PARC_INVOKE_ROUTINE InvokeRoutine;
    PARC_EXECUTE_ROUTINE ExecuteRoutine;
    PARC_HALT_ROUTINE HaltRoutine;
    PARC_POWERDOWN_ROUTINE PowerDownRoutine;
    PARC_RESTART_ROUTINE RestartRoutine;
    PARC_REBOOT_ROUTINE RebootRoutine;
    PARC_INTERACTIVE_MODE_ROUTINE InteractiveModeRoutine;
    PVOID Reserved1;
    PARC_GET_PEER_ROUTINE GetPeerRoutine;
    PARC_GET_CHILD_ROUTINE GetChildRoutine;
    PARC_GET_PARENT_ROUTINE GetParentRoutine;
    PARC_GET_DATA_ROUTINE GetDataRoutine;
    PARC_ADD_CHILD_ROUTINE AddChildRoutine;
    PARC_DELETE_COMPONENT_ROUTINE DeleteComponentRoutine;
    PARC_GET_COMPONENT_ROUTINE GetComponentRoutine;
    PARC_SAVE_CONFIGURATION_ROUTINE SaveConfigurationRoutine;
    PARC_GET_SYSTEM_ID_ROUTINE GetSystemIdRoutine;
    PARC_MEMORY_ROUTINE MemoryRoutine;
    PVOID Reserved2;
    PARC_GET_TIME_ROUTINE GetTimeRoutine;
    PARC_GET_RELATIVE_TIME_ROUTINE GetRelativeTimeRoutine;
    PARC_GET_DIRECTORY_ENTRY_ROUTINE GetDirectoryEntryRoutine;
    PARC_OPEN_ROUTINE OpenRoutine;
    PARC_CLOSE_ROUTINE CloseRoutine;
    PARC_READ_ROUTINE ReadRoutine;
    PARC_READ_STATUS_ROUTINE ReadStatusRoutine;
    PARC_WRITE_ROUTINE WriteRoutine;
    PARC_SEEK_ROUTINE SeekRoutine;
    PARC_MOUNT_ROUTINE MountRoutine;
    PARC_GET_ENVIRONMENT_ROUTINE GetEnvironmentRoutine;
    PARC_SET_ENVIRONMENT_ROUTINE SetEnvironmentRoutine;
    PARC_GET_FILE_INFO_ROUTINE GetFileInformationRoutine;
    PARC_SET_FILE_INFO_ROUTINE SetFileInformationRoutine;
    PARC_FLUSH_ALL_CACHES_ROUTINE FlushAllCachesRoutine;
    PARC_TEST_UNICODE_CHARACTER_ROUTINE TestUnicodeCharacterRoutine;
    PARC_GET_DISPLAY_STATUS_ROUTINE GetDisplayStatusRoutine;
} FIRMWARE_VECTOR_TABLE, *PFIRMWARE_VECTOR_TABLE;

typedef struct _VENDOR_VECTOR_TABLE {
    PARC_LOAD_ROUTINE LoadRoutine;
    PARC_INVOKE_ROUTINE InvokeRoutine;
    PARC_EXECUTE_ROUTINE ExecuteRoutine;
    PARC_HALT_ROUTINE HaltRoutine;
    PARC_POWERDOWN_ROUTINE PowerDownRoutine;
    PARC_RESTART_ROUTINE RestartRoutine;
    PARC_REBOOT_ROUTINE RebootRoutine;
    PARC_INTERACTIVE_MODE_ROUTINE InteractiveModeRoutine;
    PVOID Reserved1;
    PARC_GET_PEER_ROUTINE GetPeerRoutine;
    PARC_GET_CHILD_ROUTINE GetChildRoutine;
    PARC_GET_PARENT_ROUTINE GetParentRoutine;
    PARC_GET_DATA_ROUTINE GetDataRoutine;
    PARC_ADD_CHILD_ROUTINE AddChildRoutine;
    PARC_DELETE_COMPONENT_ROUTINE DeleteComponentRoutine;
    PARC_GET_COMPONENT_ROUTINE GetComponentRoutine;
    PARC_SAVE_CONFIGURATION_ROUTINE SaveConfigurationRoutine;
    PARC_GET_SYSTEM_ID_ROUTINE GetSystemIdRoutine;
    PARC_MEMORY_ROUTINE MemoryRoutine;
    PVOID Reserved2;
    PARC_GET_TIME_ROUTINE GetTimeRoutine;
    PARC_GET_RELATIVE_TIME_ROUTINE GetRelativeTimeRoutine;
    PARC_GET_DIRECTORY_ENTRY_ROUTINE GetDirectoryEntryRoutine;
    PARC_OPEN_ROUTINE OpenRoutine;
    PARC_CLOSE_ROUTINE CloseRoutine;
    PARC_READ_ROUTINE ReadRoutine;
    PARC_READ_STATUS_ROUTINE ReadStatusRoutine;
    PARC_WRITE_ROUTINE WriteRoutine;
    PARC_SEEK_ROUTINE SeekRoutine;
    PARC_MOUNT_ROUTINE MountRoutine;
    PARC_GET_ENVIRONMENT_ROUTINE GetEnvironmentRoutine;
    PARC_SET_ENVIRONMENT_ROUTINE SetEnvironmentRoutine;
    PARC_GET_FILE_INFO_ROUTINE GetFileInformationRoutine;
    PARC_SET_FILE_INFO_ROUTINE SetFileInformationRoutine;
    PARC_FLUSH_ALL_CACHES_ROUTINE FlushAllCachesRoutine;
    PARC_TEST_UNICODE_CHARACTER_ROUTINE TestUnicodeCharacterRoutine;
    PARC_GET_DISPLAY_STATUS_ROUTINE GetDisplayStatusRoutine;
} VENDOR_VECTOR_TABLE, * PVENDOR_VECTOR_TABLE;

typedef struct ARC_LE _LITTLE_ENDIAN32 {
    uint32_t v;
} LITTLE_ENDIAN32, *PLITTLE_ENDIAN32;

typedef struct ARC_LE _SYSTEM_PARAMETER_BLOCK {
    ULONG Signature;
    ULONG Length;
    USHORT Version;
    USHORT Revision;
    PRESTART_BLOCK RestartBlock;
    PDEBUG_BLOCK DebugBlock;
    PVOID GenerateExceptionVector;
    PVOID TlbMissExceptionVector;
    ULONG FirmwareVectorLength;
    PFIRMWARE_VECTOR_TABLE FirmwareVector;
    ULONG VendorVectorLength;
    PVENDOR_VECTOR_TABLE VendorVector;
    ULONG AdapterCount;
    ULONG Adapter0Type;
    ULONG Adapter0Length;
    PVOID* Adapter0Vector;
    PVOID* RuntimePointers;
} SYSTEM_PARAMETER_BLOCK, * PSYSTEM_PARAMETER_BLOCK;

typedef struct ARC_LE _SYSTEM_PARAMETER_BLOCK_LE {
    ULONG Signature;
    ULONG Length;
    USHORT Version;
    USHORT Revision;
    size_t RestartBlock;
    size_t DebugBlock;
    size_t GenerateExceptionVector;
    size_t TlbMissExceptionVector;
    ULONG FirmwareVectorLength;
    size_t FirmwareVector;
    ULONG VendorVectorLength;
    size_t VendorVector;
    ULONG AdapterCount;
    ULONG Adapter0Type;
    ULONG Adapter0Length;
    size_t Adapter0Vector;
    ULONG RuntimePointers;
} SYSTEM_PARAMETER_BLOCK_LE, * PSYSTEM_PARAMETER_BLOCK_LE;

#define ARC_SYSTEM_TABLE() ((PSYSTEM_PARAMETER_BLOCK)(ARC_SYSTEM_TABLE_ADDRESS))
#define ARC_SYSTEM_TABLE_LE() ((PSYSTEM_PARAMETER_BLOCK_LE)(ARC_SYSTEM_TABLE_ADDRESS))
#define ARC_VENDOR_VECTORS() ((PVENDOR_VECTOR_TABLE)(ARC_SYSTEM_TABLE_LE()->VendorVector))

PCHAR ArcGetErrorString(ARC_STATUS Status);

// Define screen colours.
typedef enum _ARC_SCREEN_COLOUR {
    ArcColourBlack,
    ArcColourRed,
    ArcColourGreen,
    ArcColourYellow,
    ArcColourBlue,
    ArcColourMagenta,
    ArcColourCyan,
    ArcColourWhite,
    MaximumArcColour
} ARC_SCREEN_COLOUR;

// Define print macros.
#define ArcClearScreen() \
    printf("%s", "\x9B\x32J")

#define ArcSetScreenColour(FgColour, BgColour)  do {\
    char Buf[16];\
    snprintf(Buf, sizeof(Buf), "\x9B\x33%dm", FgColour); printf("%s", Buf); \
    snprintf(Buf, sizeof(Buf), "\x9B\x34%dm", BgColour); printf("%s", Buf); \
} while (0)

#define ArcSetScreenAttributes( HighIntensity, Underscored, ReverseVideo ) do {\
    printf("%s", "\x9B\x30m"); \
    if (HighIntensity) { \
        printf("%s", "\x9B\x31m"); \
    } \
    if (Underscored) { \
        printf("%s", "\x9B\x34m"); \
    } \
    if (ReverseVideo) { \
        printf("%s", "\x9B\x37m"); \
    } \
} while (0);

#define ArcSetPosition( Row, Column ) do {\
    char Buf[16];\
    snprintf(Buf, sizeof(Buf), "\x9B%d;%dH", (Row + 1), (Column + 1)); printf("%s", Buf);\
} while (0);

#define ArcMoveCursorLeft(Spaces) do {\
    char Buf[16];\
    snprintf(Buf, sizeof(Buf), "\x9B%dD", Spaces); printf("%s", Buf);\
} while (0);

#define ArcMoveCursorToColumn(Spaces) do { \
    printf( "\r" ); \
    if ( Spaces > 1 ) {\
        char Buf[16];\
        snprintf(Buf, sizeof(Buf), "\x9B%dC", Spaces - 1); printf("%s", Buf); \
    }\
} while (0);

// I'm lazy, reimplementing the RVL arc firmware keyboard low level driver based on high level ARC calls:
UCHAR IOSKBD_ReadChar(void);
bool IOSKBD_CharAvailable(void);

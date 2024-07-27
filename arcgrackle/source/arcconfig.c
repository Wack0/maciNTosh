#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "arc.h"
#include "arcconfig.h"
#include "arcdevice.h"
#include "arcenv.h"
#include "arcconsole.h"
#include "arcmem.h"
#include "arcio.h"
#include "arcdisk.h"
#include "runtime.h"

enum {
    MAXIMUM_DEVICE_COUNT = 256
};

// Declare stub routines
static ARC_STATUS StubOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PULONG FileId) { return _ESUCCESS; }
static ARC_STATUS StubClose(ULONG FileId) {
    // TODO: set closed flag in file table
    return _ESUCCESS;
}
static ARC_STATUS StubMount(PCHAR MountPath, MOUNT_OPERATION Operation) { return _EINVAL; }
static ARC_STATUS StubSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode) { return _ESUCCESS; }
static ARC_STATUS StubRead(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) { return _ESUCCESS; }
static ARC_STATUS StubWrite(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) { return _ESUCCESS; }
static ARC_STATUS StubGetReadStatus(ULONG FileId) { return _ESUCCESS; }
static ARC_STATUS StubGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo) { return _EINVAL; }

// Declare display write routine
static ARC_STATUS DisplayWrite(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
    *Count = ArcConsoleWrite((PBYTE)Buffer, Length);
    return _ESUCCESS;
}

static const CHAR MonitorPath[] = "multi(0)video(0)monitor(0)";
static const CHAR s_DisplayIdentifier[] = "FRAME_BUF";

static const DEVICE_VECTORS MonitorVectors = {
    .Open = StubOpen,
    .Close = StubClose,
    .Mount = StubMount,
    .Read = StubRead,
    .Write = DisplayWrite,
    .Seek = StubSeek,
    .GetReadStatus = StubGetReadStatus,
    .GetFileInformation = StubGetFileInformation,
    .SetFileInformation = NULL,
    .GetDirectoryEntry = NULL
};

// Let printf work.
static size_t StdoutWrite(FILE* Instance, const char* bp, size_t n) {
    return ArcConsoleWrite(bp, n);
}
static struct File_methods s_fmStdout = {
    StdoutWrite, NULL
};
static FILE s_fStdout = { &s_fmStdout };
FILE* stdout = &s_fStdout;

// Declare keyboard routines
static ARC_STATUS KeyRead(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
    // Read Length chars into buffer, blocking if needed.
    PUCHAR Buffer8 = (PUCHAR)Buffer;
    ULONG RealCount = 0;
    for (; RealCount < Length; RealCount++) {
        Buffer8[RealCount] = IOSKBD_ReadChar();
    }
    *Count = Length;
    return _ESUCCESS;
}

static ARC_STATUS KeyGetReadStatus(ULONG FileId) {
    // return EAGAIN if no chars available, otherwise return ESUCCESS
    return IOSKBD_CharAvailable() ? _ESUCCESS : _EAGAIN;
}

static const CHAR KeyboardPath[] = "multi(1)keyboard(0)";

static const DEVICE_VECTORS KeyboardVectors = {
    .Open = StubOpen,
    .Close = StubClose,
    .Mount = StubMount,
    .Read = KeyRead,
    .Write = StubWrite,
    .Seek = StubSeek,
    .GetReadStatus = KeyGetReadStatus,
    .GetFileInformation = StubGetFileInformation,
    .SetFileInformation = NULL,
    .GetDirectoryEntry = NULL
};

// ARC path names.
static const PCHAR DeviceTable[] = {
    "arc",
    "cpu",
    "fpu",
    "pic",
    "pdc",
    "sic",
    "sdc",
    "sc",
    "eisa",
    "tc",
    "scsi",
    "dti",
    "multi",
    "disk",
    "tape",
    "cdrom",
    "worm",
    "serial",
    "net",
    "video",
    "par",
    "point",
    "key",
    "audio",
    "other",
    "rdisk",
    "fdisk",
    "tape",
    "modem",
    "monitor",
    "print",
    "pointer",
    "keyboard",
    "term",
    "other",
    "line",
    "network",
    "system",
    "maximum",
    "partition"
};

static DEVICE_ENTRY Root;
static DEVICE_ENTRY Cpu;
static DEVICE_ENTRY Pci;
static DEVICE_ENTRY MacIO;
static DEVICE_ENTRY Video;

// USB mass storage
static DEVICE_ENTRY UsbDisk = {
    .Component = ARC_MAKE_COMPONENT(AdapterClass, ScsiAdapter, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 0, 0),
    .Parent = &Pci
};

// Monitor
static DEVICE_ENTRY Monitor = {
    .Component = ARC_MAKE_COMPONENT(PeripheralClass, MonitorPeripheral, ARC_DEVICE_OUTPUT | ARC_DEVICE_CONSOLE_OUT, 0, 0),
    .Parent = &Video,
    .Vectors = &MonitorVectors
};

// IDE controllers
static DEVICE_ENTRY Ide = {
    .Component = ARC_MAKE_COMPONENT(AdapterClass, MultiFunctionAdapter, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 0, 0),
    .Parent = &MacIO
};

// SCSI controller
static DEVICE_ENTRY Scsi = {
    .Component = ARC_MAKE_COMPONENT(AdapterClass, ScsiAdapter, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 0, 0),
    .Parent = &MacIO,
    .Peer = &Ide
};

// Video
static DEVICE_ENTRY Video = {
    .Component = ARC_MAKE_COMPONENT(ControllerClass, DisplayController, ARC_DEVICE_OUTPUT | ARC_DEVICE_CONSOLE_OUT, 0, 0),
    .Parent = &Pci,
    .Child = &Monitor,
    .Peer = &UsbDisk
};

// Keyboard
static DEVICE_ENTRY Keyboard = {
    .Component = ARC_MAKE_COMPONENT(PeripheralClass, KeyboardPeripheral, ARC_DEVICE_INPUT | ARC_DEVICE_CONSOLE_IN, 0, 0),
    .Parent = &MacIO,
    .Peer = &Scsi,
    .Vectors = &KeyboardVectors
};

// We need some valid bus for some NT driver classes to work at all.
// VME bus is allowed here, and nothing actually uses it. Perfect for us :)
static char s_PciIdentifier[] = "PCI";
static char s_MioIdentifier[] = "VME";

// MacIO bus.
static ARC_RESOURCE_LIST(s_MioResource,
    ARC_RESOURCE_DESCRIPTOR_MEMORY(0, 0, 0)
);
static DEVICE_ENTRY MacIO = {
    .Component = ARC_MAKE_COMPONENT(AdapterClass, MultiFunctionAdapter, ARC_DEVICE_NONE, 1, sizeof(s_MioResource)),
    .Parent = &Root,
    .Child = &Keyboard,
    .ConfigData = &s_MioResource.Header,
};

// Dummy PCI bus, some NT code only will check the number of PCI buses present in the ARC device tree.
// If this system only has one PCI bus it's ok.
static DEVICE_ENTRY Pci2 = {
    .Component = ARC_MAKE_COMPONENT(AdapterClass, MultiFunctionAdapter, ARC_DEVICE_NONE, 2, 0),
    .Parent = &Root,
    .Peer = &MacIO,
};

// PCI bus.
static DEVICE_ENTRY Pci = {
    .Component = ARC_MAKE_COMPONENT(AdapterClass, MultiFunctionAdapter, ARC_DEVICE_NONE, 0, 0),
    .Parent = &Root,
    .Peer = &Pci2,
    .Child = &Video
};

// PPC750 caches.
// TODO: is this correct?
static DEVICE_ENTRY L2Cache = {
    .Component = ARC_MAKE_COMPONENT(CacheClass, SecondaryCache, ARC_DEVICE_NONE, 0x1060006, 0),
    .Parent = &Cpu,
};
static DEVICE_ENTRY Dcache = {
    .Component = ARC_MAKE_COMPONENT(CacheClass, PrimaryDcache, ARC_DEVICE_NONE, 0x1060003, 0),
    .Parent = &Cpu,
    .Peer = &L2Cache
};
static DEVICE_ENTRY Icache = {
    .Component = ARC_MAKE_COMPONENT(CacheClass, PrimaryIcache, ARC_DEVICE_NONE, 0x1060003, 0),
    .Parent = &Cpu,
    .Peer = &Dcache
};

static const char s_CpuIdentifier[] = "Power Macintosh"; // ok, not really a cpu identifier, but lolololol

// CPU
static DEVICE_ENTRY Cpu = {
    .Component = ARC_MAKE_COMPONENT(ProcessorClass, CentralProcessor, ARC_DEVICE_NONE, 613, 0),
    .Parent = &Root,
    .Child = &Icache,
    .Peer = &Pci,
};

static const char s_RootIdentifier[] = "Gossamer";

// Root
static DEVICE_ENTRY Root = {
    .Component = ARC_MAKE_COMPONENT(SystemClass, ArcSystem, ARC_DEVICE_NONE, 0, 0),
    .Child = &Cpu
};

// BUGBUG: If extra default components are added, add them to this list.
static PDEVICE_ENTRY s_DefaultComponents[] = {
    // Root
    &Root,

    // First level
    &Cpu,
    &Pci,
    &Pci2,
    &MacIO,

    // Cpu
    &Icache,
    &Dcache,
    &L2Cache,

    // Pci
    &Video,
    &UsbDisk,

    // Video
    &Monitor,
    
    // MacIO
    &Keyboard,
    &Scsi,
    &Ide,
};

// Space for additional components.
static DEVICE_ENTRY g_AdditionalComponents[MAXIMUM_DEVICE_COUNT] = { 0 };
static BYTE g_ConfigurationDataBuffer[MAXIMUM_DEVICE_COUNT * 0x100] = {0};
static ULONG g_AdditionalComponentsCount = 0;
static ULONG g_ConfigurationDataOffset = 0;
_Static_assert(sizeof(DEVICE_ENTRY) < 0x100);

// Config functions implementation.

static bool DeviceEntryIsValidImpl(PCONFIGURATION_COMPONENT Component, PULONG Index) {
    bool ValueForDefault = true;
    if (Index != NULL) {
        *Index = -1;
        ValueForDefault = false;
    }
    if (Component == NULL) return false;
    // Must be a default component, or a used additional component.
    PDEVICE_ENTRY Entry = (PDEVICE_ENTRY)Component;
    for (ULONG def = 0; def < sizeof(s_DefaultComponents) / sizeof(s_DefaultComponents[0]); def++) {
        if (Entry == s_DefaultComponents[def]) return ValueForDefault;
    }

    for (ULONG i = 0; i < g_AdditionalComponentsCount; i++) {
        if (Entry == &g_AdditionalComponents[i]) {
            if (Index != NULL) *Index = i;
            return true;
        }
    }

    return false;
}

static bool DeviceEntryIsValid(PCONFIGURATION_COMPONENT Component) {
    return DeviceEntryIsValidImpl(Component, NULL);
}

static bool DeviceEntryIsValidForDelete(PCONFIGURATION_COMPONENT Component, PULONG Index) {
    return DeviceEntryIsValidImpl(Component, Index);
}

/// <summary>
/// Add a new component entry as a child of Component.
/// </summary>
/// <param name="Component">Parent component which will be added to.</param>
/// <param name="NewComponent">Child component to add.</param>
/// <param name="ConfigurationData">Resource list of the child component.</param>
/// <returns></returns>
static PCONFIGURATION_COMPONENT ArcAddChild(
    IN PCONFIGURATION_COMPONENT Component,
    IN PCONFIGURATION_COMPONENT NewComponent,
    IN PVOID ConfigurationData OPTIONAL
) {
    // Component must be valid.
    // Other implementations allow to replace the root component entirely.
    // We do not.
    if (Component == NULL) return NULL;
    if (!DeviceEntryIsValid(Component)) return NULL;

    // Ensure there's enough space to allocate an additional component.
    if (g_AdditionalComponentsCount >= sizeof(g_AdditionalComponents) / sizeof(g_AdditionalComponents[0])) return NULL;

    ULONG ConfigDataLength = NewComponent->ConfigurationDataLength;
    if (ConfigurationData == NULL) ConfigDataLength = 0;

    // ...and for the configuration data
    if (g_ConfigurationDataOffset + ConfigDataLength >= sizeof(g_ConfigurationDataBuffer)) return NULL;

    // Allocate the device entry.
    PDEVICE_ENTRY Entry = &g_AdditionalComponents[g_AdditionalComponentsCount];
    g_AdditionalComponentsCount++;

    // Copy the new component to the list.
    Entry->Component = *NewComponent;

    // If no config data was specified, ensure the length is zero.
    if (ConfigurationData == NULL) Entry->Component.ConfigurationDataLength = 0;

    // Set the parent.
    Entry->Parent = (PDEVICE_ENTRY)Component;

    // Copy the configuration data.
    if (ConfigDataLength != 0) {
        Entry->ConfigData = (PCM_PARTIAL_RESOURCE_LIST_HEADER)&g_ConfigurationDataBuffer[g_ConfigurationDataOffset];
        memcpy(Entry->ConfigData, ConfigurationData, ConfigDataLength);
        g_ConfigurationDataOffset += ConfigDataLength;
    }

    // Set the new entry as last child of parent.
    PDEVICE_ENTRY Parent = (PDEVICE_ENTRY)Component;
    if (Parent->Child == NULL) Parent->Child = Entry;
    else {
        PDEVICE_ENTRY This = Parent->Child;
        for (; This->Peer != NULL; This = This->Peer) {}
        This->Peer = Entry;
    }

    // All done.
    return &Entry->Component;
}

/// <summary>
/// Deletes a component entry. Can not delete an entry with children, or an entry that wasn't added by AddChild.
/// </summary>
/// <param name="Component">Component to delete</param>
/// <returns>ARC status code.</returns>
static ARC_STATUS ArcDeleteComponent(IN PCONFIGURATION_COMPONENT Component) {
    ULONG ComponentIndex;
    if (!DeviceEntryIsValidForDelete(Component, &ComponentIndex)) return _EINVAL;

    PDEVICE_ENTRY Entry = &g_AdditionalComponents[ComponentIndex];
    if (Entry->Parent == NULL) return _EINVAL;
    if (Entry->Child != NULL) return _EACCES;

    // Get the parent.
    PDEVICE_ENTRY Parent = Entry->Parent;

    // Point the child to Component's peer.
    if (Parent->Child == Entry) Parent->Child = Entry->Peer;
    else {
        PDEVICE_ENTRY This = Parent->Child;
        for (; This->Peer != Entry; This = This->Peer) {}
        This->Peer = Entry->Peer;
    }

    // Zero out the parent to remove the entry from the component hierarchy.
    Entry->Parent = NULL;

    return _ESUCCESS;
}

/// <summary>
/// Parses the next part of an ARC device path.
/// </summary>
/// <param name="pPath">Pointer to the part of the string to parse. On function success, pointer is advanced past the part that was parsed.</param>
/// <param name="ExpectedType">The device type that is expected to be parsed.</param>
/// <param name="Key">On function success, returns the parsed key from the string.</param>
/// <returns>True if parsing succeeded, false if it failed.</returns>
bool ArcDeviceParse(PCHAR* pPath, CONFIGURATION_TYPE ExpectedType, ULONG* Key) {
    PCHAR ExpectedString = DeviceTable[ExpectedType];

    // Ensure *pPath == ExpectedString (case-insensitive)
    PCHAR Path = *pPath;
    while (*ExpectedString != 0) {
        CHAR This = *Path | 0x20;
        //if (This < 'a' || This > 'z') return false;
        if (This != *ExpectedString) return false;
        ExpectedString++;
        Path++;
    }

    // Next char must be '('
    if (*Path != '(') return false;
    Path++;

    // Digits followed by ')'
    ULONG ParsedKey = 0;
    while (*Path != ')' && *Path != 0) {
        CHAR This = *Path;
        if (This < '0' || This > '9') return false;
        ParsedKey *= 10;
        ParsedKey += (This - '0');
        Path++;
    }
    if (*Path != ')') return false;
    Path++;

    // Success
    *pPath = Path;
    *Key = ParsedKey;
    return true;
}

/// <summary>
/// For an ARC device, get its ARC path.
/// </summary>
/// <param name="Component">ARC component</param>
/// <param name="Path">Buffer to write path to</param>
/// <param name="Length">Length of buffer</param>
/// <returns>Length written without null terminator</returns>
ULONG ArcDeviceGetPath(PCONFIGURATION_COMPONENT Component, PCHAR Path, ULONG Length) {
    PDEVICE_ENTRY This = (PDEVICE_ENTRY)Component;

    ULONG Offset = 0;
    // Recurse for each element.
    if (This->Parent != NULL && This->Parent != &Root) {
        Offset = ArcDeviceGetPath(&This->Parent->Component, Path, Length);
        if (Offset == 0) return 0;
    }

    if (Offset > Length) return 0;

    int Ret = snprintf(&Path[Offset], Length - Offset, "%s(%u)", DeviceTable[This->Component.Type], This->Component.Key);
    if (Ret < 0) return 0;
    if (Ret >= (Length - Offset)) return 0;
    return Offset + Ret;
}

static bool IsDevice(PDEVICE_ENTRY This, PCHAR* pPath) {
    // Initialise the key to zero.
    ULONG Key = 0;
    // Get the path string parsed in, don't update the pointer on a failure.
    PCHAR Path = *pPath;

    // Try to parse this part of the device string, by this device entry's type.
    // If it failed, this isn't the device being looked for.
    if (!ArcDeviceParse(&Path, This->Component.Type, &Key)) return false;

    // Ensure provided key matches the component's
    if (Key != This->Component.Key) return false;

    // Success
    *pPath = Path;
    return true;
}

/// <summary>
/// Obtains a component from an ARC path string. Returns the best component that can be found in the hierarchy.
/// </summary>
/// <param name="PathName">Path name to search</param>
/// <returns>Found component</returns>
static PCONFIGURATION_COMPONENT ArcGetComponent(IN PCHAR PathName) {
    // Get the root component.
    PDEVICE_ENTRY Match = &Root;

    // Keep searching until there are no more entries.
    PCHAR Pointer = PathName;
    while (*Pointer != 0) {
        // Search each child.
        PDEVICE_ENTRY This;
        for (This = Match->Child; This != NULL; This = This->Peer) {
            if (IsDevice(This, &Pointer)) {
                Match = This;
                break;
            }
        }

        if (This == NULL) break;
    }

    return &Match->Component;
}

/// <summary>
/// Gets the child of the current component
/// </summary>
/// <param name="Component">Component to obtain the child of. If null, obtains the root component.</param>
/// <returns>Component</returns>
static PCONFIGURATION_COMPONENT ArcGetChild(IN PCONFIGURATION_COMPONENT Component OPTIONAL) {
    if (Component == NULL) return &Root.Component;
    if (!DeviceEntryIsValid(Component)) return NULL;
    PDEVICE_ENTRY Child = ((PDEVICE_ENTRY)Component)->Child;
    if (Child == NULL) return NULL;
    return &Child->Component;
}

/// <summary>
/// Gets the parent of the current component
/// </summary>
/// <param name="Component">Component to obtain the parent of.</param>
/// <returns>Parent component</returns>
static PCONFIGURATION_COMPONENT ArcGetParent(IN PCONFIGURATION_COMPONENT Component) {
    if (Component == NULL) return NULL;
    if (!DeviceEntryIsValid(Component)) return NULL;
    PDEVICE_ENTRY Parent = ((PDEVICE_ENTRY)Component)->Parent;
    if (Parent == NULL) return NULL;
    return &Parent->Component;
}

/// <summary>
/// Gets the peer of the current component
/// </summary>
/// <param name="Component">Component to obtain the peer of.</param>
/// <returns>Peer component</returns>
static PCONFIGURATION_COMPONENT ArcGetPeer(IN PCONFIGURATION_COMPONENT Component) {
    if (Component == NULL) return NULL;
    if (!DeviceEntryIsValid(Component)) return NULL;
    PDEVICE_ENTRY Peer = ((PDEVICE_ENTRY)Component)->Peer;
    if (Peer == NULL) return NULL;
    return &Peer->Component;
}

/// <summary>
/// Gets the configuration data of the current component
/// </summary>
/// <param name="ConfigurationData">Buffer to write the configuration data into</param>
/// <param name="Component">Component to obtain the configuration data of</param>
/// <returns>ARC status value</returns>
static ARC_STATUS ArcGetData(OUT PVOID ConfigurationData, IN PCONFIGURATION_COMPONENT Component) {
    if (!DeviceEntryIsValid(Component)) return _EINVAL;

    PDEVICE_ENTRY Device = (PDEVICE_ENTRY)Component;
    memcpy(ConfigurationData, Device->ConfigData, Device->Component.ConfigurationDataLength);
    return _ESUCCESS;
}

static ARC_STATUS ArcSaveConfiguration(void) {
    // No operation.
    return _ESUCCESS;
}

static ARC_DISPLAY_STATUS s_DisplayStatus = { 0 };
static PARC_DISPLAY_STATUS ArcGetDisplayStatus(ULONG FileId) {
    ArcConsoleGetStatus(&s_DisplayStatus);
    return &s_DisplayStatus;
}

static ARC_STATUS ArcTestUnicodeCharacter(ULONG FileId, WCHAR UnicodeCharacter) {
    if ((UnicodeCharacter >= ' ') && (UnicodeCharacter <= '~')) return _ESUCCESS;
    return _EINVAL;
}

static SYSTEM_ID s_SystemId = { 0 };
static char s_VendorId[] = "MacRISC";
static char s_UnknownProductId[] = "ppcMac";
_Static_assert(sizeof(s_VendorId) <= sizeof(s_SystemId.VendorId));
static PSYSTEM_ID ArcGetSystemId(void) {
    return &s_SystemId;
}

#define CONSOLE_WRITE_CONST(str) ArcConsoleWrite((str), sizeof(str) - 1)

static ULONG s_PrintTreeCount = 0;

static void ArcPrintDevice(PDEVICE_ENTRY Device) {
    char ArcPath[1024];
    if (ArcDeviceGetPath(&Device->Component, ArcPath, sizeof(ArcPath)) == 0) {
        printf("unknown device at %08x\r\n", Device);
    }
    else {
        printf("%s\r\n", ArcPath);
    }
    if (s_PrintTreeCount != 0 && (s_PrintTreeCount & 0x1f) == 0) IOSKBD_ReadChar();
    s_PrintTreeCount++;
}

static void ArcPrintTreeImpl(PDEVICE_ENTRY Device) {
    if (Device == NULL) return;

    for (PDEVICE_ENTRY Child = (PDEVICE_ENTRY)ArcGetChild(Device); Child != NULL; Child = (PDEVICE_ENTRY)ArcGetChild(Child)) {
        ArcPrintDevice(Child);
        for (PDEVICE_ENTRY This = (PDEVICE_ENTRY)ArcGetPeer(Child); This != NULL; This = (PDEVICE_ENTRY)ArcGetPeer(This)) {
            ArcPrintDevice(This);
            if (This->Child != NULL) ArcPrintTreeImpl(This);
        }
    }
}

static void ArcPrintTree(PDEVICE_ENTRY Device) {
    s_PrintTreeCount = 0;
    ArcPrintTreeImpl(Device);
}

static bool ArcConfigKeyEquals(PDEVICE_ENTRY Lhs, PDEVICE_ENTRY Rhs) {
    return (
        (Lhs->Parent == Rhs->Parent) &&
        (Lhs->Component.Type == Rhs->Component.Type) &&
        (Lhs->Component.Class == Rhs->Component.Class) &&
        (Lhs->Component.Key == Rhs->Component.Key)
        );
}

bool ArcConfigKeyExists(PDEVICE_ENTRY Device) {
    for (ULONG def = 0; def < sizeof(s_DefaultComponents) / sizeof(s_DefaultComponents[0]); def++) {
        PDEVICE_ENTRY This = s_DefaultComponents[def];
        if (This == Device) continue;
        if (!ArcConfigKeyEquals(Device, This)) continue;
        return true;
    }

    for (ULONG i = 0; i < g_AdditionalComponentsCount; i++) {
        PDEVICE_ENTRY This = &g_AdditionalComponents[i];
        if (This == Device) continue;
        if (!ArcConfigKeyEquals(Device, This)) continue;
        return true;
    }
    return false;
}

static ARC_RESOURCE_LIST(s_RamdiskResource,
    ARC_RESOURCE_DESCRIPTOR_MEMORY(0, 0, 0)
);
static CONFIGURATION_COMPONENT s_RamdiskController = ARC_MAKE_COMPONENT(AdapterClass, MultiFunctionAdapter, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 2, 0);
static CONFIGURATION_COMPONENT s_RamdiskDisk = ARC_MAKE_COMPONENT(ControllerClass, DiskController, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 0, 0);
static CONFIGURATION_COMPONENT s_RamdiskFdisk = ARC_MAKE_COMPONENT(PeripheralClass, FloppyDiskPeripheral, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 0, 0);


static ARC_STATUS RdRead(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
    //printf("Read %x => (%p) %x bytes\n", FileId, Buffer, Length);
    // Get the file table
    PARC_FILE_TABLE File = ArcIoGetFile(FileId);
    if (File == NULL) return _EBADF;

    ULONG Pos = 0;
    ULONG PosEnd = 0;
    if (Length != 0) {
        Pos = (ULONG)File->Position;
        PosEnd = Pos + Length;
        if (PosEnd >= s_RamdiskResource.Descriptors[0].Memory.Length) {
            Length = (ULONG)(s_RamdiskResource.Descriptors[0].Memory.Length - Pos);
            PosEnd = Pos + Length;
        }
    }

    if (Length == 0) {
        *Count = 0;
        return _ESUCCESS;
    }

    memcpy(Buffer, (PVOID)(s_RamdiskResource.Descriptors[0].Memory.Start.LowPart + Pos), Length);
    *Count = Length;
    File->Position = PosEnd;

    return _ESUCCESS;
}

static ARC_STATUS RdWrite(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
    return _EFAULT;
}

static ARC_STATUS RdSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode) {
    //printf("Seek %x => %llx (%d)\n", FileId, Offset->QuadPart, SeekMode);
    // Get the file table
    PARC_FILE_TABLE File = ArcIoGetFile(FileId);
    if (File == NULL) return _EBADF;

    int64_t OldPosition = File->Position;
    switch (SeekMode) {
    case SeekRelative:
        File->Position += Offset->QuadPart;
        break;
    case SeekAbsolute:
        File->Position = Offset->QuadPart;
        break;
    default:
        //printf("seek: Bad mode %d\n", SeekMode);
        return _EINVAL;
    }

    if (File->Position >= s_RamdiskResource.Descriptors[0].Memory.Length) {
        File->Position = OldPosition;
        return _EFAULT;
    }
    return _ESUCCESS;
}

static ARC_STATUS RdGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo) {
    PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
    if (FileEntry == NULL) return _EBADF;

    FileInfo->CurrentPosition.QuadPart = FileEntry->Position;
    FileInfo->StartingAddress.QuadPart = 0;
    FileInfo->EndingAddress.QuadPart = s_RamdiskResource.Descriptors[0].Memory.Length;
    FileInfo->Type = DiskPeripheral;

    return _ESUCCESS;
}

// Ramdisk device vectors.
static const DEVICE_VECTORS s_RdVectors = {
    .Open = StubOpen,
    .Close = StubClose,
    .Mount = StubMount,
    .Read = RdRead,
    .Write = RdWrite,
    .Seek = RdSeek,
    .GetReadStatus = NULL,
    .GetFileInformation = RdGetFileInformation,
    .SetFileInformation = NULL,
    .GetDirectoryEntry = NULL
};

bool ArcHasRamdiskLoaded(void);
void ArcInitRamDisk(ULONG ControllerKey, PVOID Pointer, ULONG Length);

ARC_STATUS ArcDiskInitRamdisk(void) {
    if (ArcHasRamdiskLoaded()) return _ESUCCESS;
    // Ensure there are enough spaces for 3 components (controller, disk, fdisk).
    if (g_AdditionalComponentsCount > (MAXIMUM_DEVICE_COUNT - 3)) return _ENOSPC;

    PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

    PVOID Ramdisk = NULL;
    ULONG FileSize32 = 0;

    ULONG CountCdrom;
    ArcDiskGetCounts(NULL, &CountCdrom);

    // Walk through all CDROMs
    ARC_STATUS Status = _EFAULT;
    for (ULONG i = 0; i < CountCdrom; i++) {
        char CdVar[6];
        char Path[1024];
        snprintf(CdVar, sizeof(CdVar), "cd%02d:", i);
        PCHAR DevicePath = ArcEnvGetDevice(CdVar);
        snprintf(Path, sizeof(Path), "%s\\drivers.img", DevicePath);

        // Open file
        U32LE FileId;
        Status = Api->OpenRoutine(Path, ArcOpenReadOnly, &FileId);
        if (ARC_FAIL(Status)) continue;

        do {
            // Get the actual file size.
            FILE_INFORMATION Info;
            Status = Api->GetFileInformationRoutine(FileId.v, &Info);
            if (ARC_FAIL(Status)) break;

            // if over 4MB don't bother
            if (Info.EndingAddress.QuadPart > 0x400000) break;

            FileSize32 = Info.EndingAddress.LowPart;

            // Allocate some RAM
            Ramdisk = ArcMemAllocDirect(FileSize32);
            if (Ramdisk == NULL) break;

            // Read image into RAM.
            U32LE Count;
            Status = Api->ReadRoutine(FileId.v, Ramdisk, FileSize32, &Count);
            if (ARC_FAIL(Status)) {
                FileSize32 = 0;
                break;
            }
            if (Count.v != FileSize32) {
                FileSize32 = 0;
                Status = _EIO;
                break;
            }


        } while (false);
        Api->CloseRoutine(FileId.v);
        break;
    }

    if (Ramdisk == NULL || FileSize32 == 0) return Status;

    // Grab the root device.
    PDEVICE_ENTRY Root = ArcGetChild(NULL);
    // Add the ramdisk controller and ensure it uses an unused key.
    PDEVICE_ENTRY RdControl = (PDEVICE_ENTRY)ArcAddChild(&Root->Component, &s_RamdiskController, NULL);
    if (RdControl == NULL) {
        printf("ArcAddChild(Root, RamdiskController) failed\r\n");
        return _EFAULT;
    }
    while (ArcConfigKeyExists(RdControl)) RdControl->Component.Key++;
    // Same, but for disk controller.
    // There is only one child of the rd controller, so no need to search for unused key.
    PDEVICE_ENTRY RdDisk = ArcAddChild(&RdControl->Component, &s_RamdiskDisk, NULL);
    if (RdDisk == NULL) {
        printf("ArcAddChild(RamdiskController, RamdiskDisk) failed\r\n");
        return _EFAULT;
    }
    // Same, but for fixed disk device.
    s_RamdiskResource.Descriptors[0].Memory.Length = FileSize32;
    s_RamdiskResource.Descriptors[0].Memory.Start.LowPart = (ULONG)Ramdisk;
    s_RamdiskFdisk.ConfigurationDataLength = sizeof(s_RamdiskResource);
    PDEVICE_ENTRY RdFdisk = ArcAddChild(&RdDisk->Component, &s_RamdiskFdisk, &s_RamdiskResource);
    if (RdFdisk == NULL) {
        printf("ArcAddChild(RamdiskDisk, RamdiskFdisk) failed\r\n");
        return _EFAULT;
    }

    // Set up the vectors for fixed disk device.
    RdFdisk->Vectors = &s_RdVectors;

    // Initialise the runtime descriptor for the NT driver.
    ArcInitRamDisk(RdControl->Component.Key, Ramdisk, FileSize32);
    return _ESUCCESS;
}

void ArcConfigInit(void) {
    // Initialise vectors.
    PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
    Api->AddChildRoutine = ArcAddChild;
    Api->DeleteComponentRoutine = ArcDeleteComponent;
    Api->GetComponentRoutine = ArcGetComponent;
    Api->GetChildRoutine = ArcGetChild;
    Api->GetParentRoutine = ArcGetParent;
    Api->GetPeerRoutine = ArcGetPeer;
    Api->GetDataRoutine = ArcGetData;
    Api->SaveConfigurationRoutine = ArcSaveConfiguration;
    Api->GetDisplayStatusRoutine = ArcGetDisplayStatus;
    Api->GetSystemIdRoutine = ArcGetSystemId;
    Api->TestUnicodeCharacterRoutine = ArcTestUnicodeCharacter;

    // Initialise the System ID structure.
    memset(&s_SystemId, 0, sizeof(s_SystemId));
    memcpy(&s_SystemId.VendorId, s_VendorId, sizeof(s_VendorId));
    memcpy(&s_SystemId.ProductId, s_UnknownProductId, sizeof(s_UnknownProductId));

    // Initialise environment variables.
    ArcEnvSetVarInMem("CONSOLEIN", KeyboardPath);
    ArcEnvSetVarInMem("CONSOLEOUT", MonitorPath);

    // Set up the display controller identifier, used by setupldr
    Video.Component.Identifier = (size_t)s_DisplayIdentifier;
    Video.Component.IdentifierLength = sizeof(s_DisplayIdentifier);

    // Set up the PCI bus identifier, used by NT kernel
    Pci.Component.Identifier = (size_t)s_PciIdentifier;
    Pci.Component.IdentifierLength = sizeof(s_PciIdentifier);
    Pci2.Component.Identifier = (size_t)s_PciIdentifier;
    Pci2.Component.IdentifierLength = sizeof(s_PciIdentifier);
    // ...and MIO identifier
    MacIO.Component.Identifier = (size_t)s_MioIdentifier;
    MacIO.Component.IdentifierLength = sizeof(s_MioIdentifier);
    // and memory range
    extern ULONG s_MacIoStart;
    s_MioResource.Descriptors[0].Memory.Start.LowPart = s_MacIoStart;
    s_MioResource.Descriptors[0].Memory.Length = 0x80000;


    // Set up the CPU identifier, if this is missing smss will terminate STATUS_OBJECT_NAME_NOT_FOUND
    Cpu.Component.Identifier = (size_t)s_CpuIdentifier;
    Cpu.Component.IdentifierLength = sizeof(s_CpuIdentifier);

    // Set up the system / chipset identifier
    Root.Component.Identifier = (size_t)s_RootIdentifier;
    Root.Component.IdentifierLength = sizeof(s_RootIdentifier);
}
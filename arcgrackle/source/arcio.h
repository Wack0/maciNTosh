#pragma once
#include <stdio.h>
#include "arcdevice.h"
#include "lib9660.h"
#include "pff.h"

enum {
    FILE_TABLE_SIZE = 16,
    FILE_IS_RAW_DEVICE = 0xffff,
    DCACHE_LINE_SIZE = 0x20
};

struct _USB_DEVICE_MOUNT_TABLE;
struct ide_drive;

// Define context for a disk.
typedef struct _DISK_CONTEXT {
    union {
        struct _USB_DEVICE_MOUNT_TABLE* DeviceMount; // USB device mount entry
        struct ide_drive* IdeDrive; // IDE drive entry
    };
    ULONG SectorStart; // Start sector of partition.
    ULONG SectorCount; // Number of sectors of partition.
    ULONG MaxSectorTransfer; // Number of sectors that can be transferred in one operation.
} DISK_CONTEXT, *PDISK_CONTEXT;

// Define context for a file.
typedef struct _FILE_CONTEXT {
    LARGE_INTEGER FileSize;
    union {
        l9660_file Iso9660;
        FATFS Fat;
    };
} FILE_CONTEXT, *PFILE_CONTEXT;

// Define file flags.
typedef struct _ARC_FILE_FLAGS {
    ULONG Open : 1;
    ULONG Read : 1;
    ULONG Write : 1;
    //ULONG Directory : 1; // set by FS driver by opening a directory // BUGBUG: dirs not implemented!
} ARC_FILE_FLAGS, * PARC_FILE_FLAGS;

#define MAXIMUM_FILE_NAME_LENGTH 32

// Define file table entry.

struct _ARC_FILE_TABLE;

typedef ARC_STATUS(*PARC_GET_SECTOR_SIZE) (ULONG DeviceId, PULONG SectorSize);
typedef ARC_STATUS(*PARC_TRANSFER_SECTOR) (struct _ARC_FILE_TABLE* FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer);

typedef struct _ARC_FILE_TABLE {
    ARC_FILE_FLAGS Flags;
    ULONG DeviceId;
    int64_t Position;
    PDEVICE_VECTORS DeviceEntryTable;
    PARC_GET_SECTOR_SIZE GetSectorSize; // Function pointer to get sector size.
    PARC_TRANSFER_SECTOR ReadSectors, WriteSectors;
    UCHAR FileNameLength;
    CHAR FileName[MAXIMUM_FILE_NAME_LENGTH];
    union {
        FILE_CONTEXT FileContext;
        DISK_CONTEXT DiskContext;
    } u;
} ARC_FILE_TABLE, * PARC_FILE_TABLE;

/// <summary>
/// Gets the file table entry by file ID.
/// </summary>
/// <param name="FileId">File ID to obtain.</param>
/// <returns>File table entry.</returns>
PARC_FILE_TABLE ArcIoGetFile(ULONG FileId);

/// <summary>
/// Gets the file table entry by file ID. (file ID must not be open)
/// </summary>
/// <param name="FileId">File ID to obtain.</param>
/// <returns>File table entry.</returns>
PARC_FILE_TABLE ArcIoGetFileForOpen(ULONG FileId);

void ArcIoInit();
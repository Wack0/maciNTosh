#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include "arc.h"
#include "arcdevice.h"
#include "arcconfig.h"
#include "arcio.h"
#include "arcfs.h"
#include "arcenv.h"
#include "usb.h"
#include "usbmsc.h"
#include "usbdisk.h"
#include "ide.h"

// ARC firmware support for disks:
// USB mass storage, and IDE
// (SCSI later if it ever happens)

enum {
	MAXIMUM_SECTOR_SIZE = 2048,
	USB_CLASS_MASS_STORAGE = 0x08
};

typedef struct _USB_DEVICE_MOUNT_TABLE {
	ULONG Address; // USB device address. Guaranteed by the USB stack to be non-zero.
	ULONG SectorSize; // Sector size, copy from Mount to avoid another couple of derefs.
	ULONG ReferenceCount; // Reference count as callers could mount same device multiple times, for ro/rw/wo.
	usbdev_t* Mount;
} USB_DEVICE_MOUNT_TABLE, *PUSB_DEVICE_MOUNT_ENTRY;

typedef struct _IDE_DEVICE_MOUNT_TABLE {
	ULONG Channel;
	UCHAR Unit; // 0 or 1
	bool Present;
} IDE_DEVICE_MOUNT_TABLE, *PIDE_DEVICE_MOUNT_ENTRY;

// Mount table.
static USB_DEVICE_MOUNT_TABLE s_MountTable[32] = { 0 };
static IDE_DEVICE_MOUNT_TABLE s_IdeTable[8] = { 0 };

// ARC path for the controller devices.
// disk(x)cdrom(x) is invalid except for x86 el torito, says osloader
// use scsi(0) for USB, because USB mass storage is SCSI lol
static char s_UsbControllerPath[] = "multi(0)scsi(0)";
static char s_IdeControllerPath[] = "multi(1)multi(0)";

static char s_UsbComponentName[] = "OHCI";
static char s_IdeComponentName[] = "MIO_IDE";

extern bool g_UsbInitialised;
extern bool g_IdeInitialised;

void ARC_WEAK usbdisk_create(usbdev_t* dev) {
	// USB disk has arrived
	PUSB_DEVICE_MOUNT_ENTRY MountEntry = NULL;
	for (ULONG i = 0; i < sizeof(s_MountTable) / sizeof(s_MountTable[0]); i++) {
		if (s_MountTable[i].Address == (ULONG)dev->address) {
			// ...already mounted?
			return;
		}
		if (MountEntry == NULL && s_MountTable[i].Address == 0) {
			MountEntry = &s_MountTable[i];
		}
	}

	MountEntry->Address = (ULONG)dev->address;
	MountEntry->ReferenceCount = 0;
	MountEntry->Mount = dev;
	MountEntry->SectorSize = MSC_INST(dev)->blocksize;
}

void ARC_WEAK usbdisk_remove(usbdev_t* dev) {
	// Find handle.
	for (ULONG i = 0; i < sizeof(s_MountTable) / sizeof(s_MountTable[0]); i++) {
		if (s_MountTable[i].Mount == dev) {
			// found it, wipe it
			if (s_MountTable[i].ReferenceCount != 0) {
				// something's using this. just wipe the pointer for now
				s_MountTable[i].Mount = NULL;
				return;
			}

			memset(&s_MountTable[i], 0, sizeof(s_MountTable[i]));
			return;
		}
	}
}

// Mount a usb device by vid/pid.
static ARC_STATUS UsbDiskMount(ULONG Address, PUSB_DEVICE_MOUNT_ENTRY* Handle) {
	// Check the mount table to see if this vid/pid is mounted.
	for (ULONG i = 0; i < sizeof(s_MountTable) / sizeof(s_MountTable[0]); i++) {
		if (s_MountTable[i].Address == Address) {
			// It's attached, return the existing handle.
			ULONG NewRefCount = s_MountTable[i].ReferenceCount + 1;
			if (NewRefCount == 0) {
				// Reference count overflow
				return _EBUSY;
			}
			s_MountTable[i].ReferenceCount = NewRefCount;
			*Handle = &s_MountTable[i];
			return _ESUCCESS;
		}
	}

	return _ENODEV;
}

// Unmount a usb device.
static ARC_STATUS UsbDiskUnMount(PUSB_DEVICE_MOUNT_ENTRY Handle) {
	// Ensure this handle looks good.
	ULONG i = (size_t)Handle - (size_t)&s_MountTable[0];
	if (i >= sizeof(s_MountTable)) return _EBADF;

	ULONG NewRefCount = Handle->ReferenceCount - 1;
	if (NewRefCount == 0) {
		// If the pointer is NULL, that means the device is gone, wipe the table entry.
		if (Handle->Mount == NULL) {
			memset(Handle, 0, sizeof(*Handle));
		}
	}
	else Handle->ReferenceCount = NewRefCount;
	return _ESUCCESS;
}

static ARC_STATUS DeblockerRead(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count);
static ARC_STATUS DeblockerWrite(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count);
static ARC_STATUS DeblockerSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode);

static ARC_STATUS UsbDiskOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PULONG FileId);
static ARC_STATUS UsbDiskClose(ULONG FileId);
static ARC_STATUS UsbDiskArcMount(PCHAR MountPath, MOUNT_OPERATION Operation);
static ARC_STATUS UsbDiskRead(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer);
static ARC_STATUS UsbDiskWrite(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer);
static ARC_STATUS UsbDiskGetReadStatus(ULONG FileId);
static ARC_STATUS UsbDiskGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo);

static ARC_STATUS IdeOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PULONG FileId);
static ARC_STATUS IdeClose(ULONG FileId);
static ARC_STATUS IdeMount(PCHAR MountPath, MOUNT_OPERATION Operation);
static ARC_STATUS IdeSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode);
static ARC_STATUS IdeRead(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer);
static ARC_STATUS IdeWrite(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer);
static ARC_STATUS IdeGetReadStatus(ULONG FileId);
static ARC_STATUS IdeGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo);

// USB controller device vectors.
static const DEVICE_VECTORS UsbDiskVectors = {
	.Open = UsbDiskOpen,
	.Close = UsbDiskClose,
	.Mount = UsbDiskArcMount,
	.Read = DeblockerRead,
	.Write = DeblockerWrite,
	.Seek = DeblockerSeek,
	.GetReadStatus = UsbDiskGetReadStatus,
	.GetFileInformation = UsbDiskGetFileInformation,
	.SetFileInformation = NULL,
	.GetDirectoryEntry = NULL
};

// USB controller device functions.

static ARC_STATUS UsbDiskGetSectorSize(ULONG FileId, PULONG SectorSize) {
	// Get the file table entry.
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EFAULT;

	// Get the mount table entry.
	PUSB_DEVICE_MOUNT_ENTRY MountEntry = FileEntry->u.DiskContext.DeviceMount;
	if (MountEntry == NULL) return _EFAULT;

	// Retrieve the sector size from it
	*SectorSize = MountEntry->SectorSize;
	return _ESUCCESS;
}

static ARC_STATUS UsbDiskOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PULONG FileId) {
	// Ensure the path starts with s_ControllerPath
	if (memcmp(OpenPath, s_UsbControllerPath, sizeof(s_UsbControllerPath) - 1) != 0) return _ENODEV;
	PCHAR DevicePath = &OpenPath[sizeof(s_UsbControllerPath) - 1];
	// Next device must be disk(x) or cdrom(x)
	ULONG UsbId = 0;
	bool IsCdRom = false;
	if (!ArcDeviceParse(&DevicePath, DiskController, &UsbId)) {
		// Not disk(x), check for cdrom(x)
		IsCdRom = true;
		if (!ArcDeviceParse(&DevicePath, CdromController, &UsbId)) {
			// Not cdrom either, can't handle this device path
			return _ENODEV;
		}
	}
	else {
		// Next device must be rdisk(0)
		ULONG MustBeZero = 0;
		if (!ArcDeviceParse(&DevicePath, DiskPeripheral, &MustBeZero)) {
			// not found
			return _ENODEV;
		}
		if (MustBeZero != 0) return _ENODEV;
	}

	// If this is a cdrom, it can only be mounted ro.
	if (IsCdRom && OpenMode != ArcOpenReadOnly) return _EACCES;

	bool IncludesPartition = false;
	ULONG PartitionNumber = 0;
	// Does the caller want a partition?
	if (*DevicePath != 0) {
		if (!ArcDeviceParse(&DevicePath, PartitionEntry, &PartitionNumber)) {
			// osloader expects fdisk(x)
			ULONG DontCare;
			if (!ArcDeviceParse(&DevicePath, FloppyDiskPeripheral, &DontCare))
				return _ENODEV;
			// which should be the last device, but...
			if (*DevicePath != 0) {
				// partition is still technically valid here.
				if (!ArcDeviceParse(&DevicePath, PartitionEntry, &PartitionNumber))
					return _ENODEV;

				// partition number here must be zero
				if (PartitionNumber != 0) return _ENODEV;
			}
		}
		else {
			// partition 0 means whole disk
			IncludesPartition = PartitionNumber != 0;
		}
	}

	// Get the file table entry.
	PARC_FILE_TABLE FileEntry = ArcIoGetFileForOpen(*FileId);
	if (FileEntry == NULL) return _EFAULT;
	// Zero the disk context.
	memset(&FileEntry->u.DiskContext, 0, sizeof(FileEntry->u.DiskContext));

	// It's now known if this is a disk or cdrom (ie, whether to use FAT or ISO9660 filesystem driver)
	// Mount the usb device, if required.
	PUSB_DEVICE_MOUNT_ENTRY Handle;
	ARC_STATUS Status = UsbDiskMount(UsbId, &Handle);
	if (ARC_FAIL(Status)) return Status;

	// Stash the mount handle into the file table.
	FileEntry->u.DiskContext.DeviceMount = Handle;
	FileEntry->u.DiskContext.MaxSectorTransfer = 0xFFFF;
	// Stash the GetSectorSize ptr into the file table.
	FileEntry->GetSectorSize = UsbDiskGetSectorSize;
	FileEntry->ReadSectors = UsbDiskRead;
	FileEntry->WriteSectors = UsbDiskWrite;

	ULONG PartitionSectors = Handle->SectorSize;
	ULONG PartitionSector = 0;
	// Set it up for the whole disk so ArcFsPartitionObtain can work.
	FileEntry->u.DiskContext.SectorStart = PartitionSector;
	FileEntry->u.DiskContext.SectorCount = PartitionSectors;
	if (IncludesPartition) {
		// Mark the file as open so ArcFsPartitionObtain can work.
		FileEntry->Flags.Open = 1;
		Status = ArcFsPartitionObtain(FileEntry->DeviceEntryTable, *FileId, PartitionNumber, Handle->SectorSize, &PartitionSector, &PartitionSectors);
		FileEntry->Flags.Open = 0;
		if (ARC_FAIL(Status)) {
			PartitionSector = 0;
			PartitionSectors = Handle->SectorSize;
		}
	}
	FileEntry->u.DiskContext.SectorStart = PartitionSector;
	FileEntry->u.DiskContext.SectorCount = PartitionSectors;
	return _ESUCCESS;
}
static ARC_STATUS UsbDiskClose(ULONG FileId) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	// Unmount the USB device.
	PUSB_DEVICE_MOUNT_ENTRY MountEntry = FileEntry->u.DiskContext.DeviceMount;
	return UsbDiskUnMount(MountEntry);
}
static ARC_STATUS UsbDiskArcMount(PCHAR MountPath, MOUNT_OPERATION Operation) { return _EINVAL; }
static ARC_STATUS DeblockerSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	
	ULONG SectorSize;
	ARC_STATUS Status = FileEntry->GetSectorSize(FileId, &SectorSize);
	if (ARC_FAIL(Status)) return Status;

	switch (SeekMode) {
	case SeekRelative:
		FileEntry->Position += Offset->QuadPart;
		break;
	case SeekAbsolute:
		FileEntry->Position = Offset->QuadPart;
		break;
	default:
		return _EINVAL;
	}

	LARGE_INTEGER SizeInBytes;
	SizeInBytes.QuadPart = FileEntry->u.DiskContext.SectorCount;
	SizeInBytes.QuadPart *= SectorSize;
	if (FileEntry->Position > SizeInBytes.QuadPart) FileEntry->Position = SizeInBytes.QuadPart;

	return _ESUCCESS;
}

static BYTE s_TemporaryBuffer[MAXIMUM_SECTOR_SIZE + 128];

static ARC_STATUS DeblockerRead(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	ULONG SectorSize;
	ARC_STATUS Status = FileEntry->GetSectorSize(FileId, &SectorSize);
	if (ARC_FAIL(Status)) return Status;
	ULONG SectorStart = FileEntry->u.DiskContext.SectorStart;
	ULONG SectorCount = FileEntry->u.DiskContext.SectorCount;
	ULONG TransferCount;

	PBYTE LocalPointer = (PVOID)(((ULONG)(&s_TemporaryBuffer[DCACHE_LINE_SIZE - 1])) & ~(DCACHE_LINE_SIZE - 1));

	*Count = 0;

	// If the current position is not at a sector boundary, read the first sector seperately.
	ULONG Offset = FileEntry->Position & (SectorSize - 1);
	while (Offset != 0) {
		int64_t OldPosition = FileEntry->Position;
		FileEntry->Position -= Offset;
		Status = DeblockerRead(FileId, LocalPointer, SectorSize, &TransferCount);
		if (ARC_SUCCESS(Status) && TransferCount != SectorSize) Status = _EIO;
		if (ARC_FAIL(Status)) {
			FileEntry->Position = OldPosition;
			return Status;
		}

		ULONG Limit;
		if ((SectorSize - Offset) > Length) Limit = Length;
		else Limit = SectorSize - Offset;
		memcpy(Buffer, &LocalPointer[Offset], Limit);
		Buffer = (PVOID)((size_t)Buffer + Limit);
		Length -= Limit;
		*Count += Limit;
		FileEntry->Position = OldPosition + Limit;
		Offset = FileEntry->Position & (SectorSize - 1);

		if (Length == 0) break;
	}

	// At a sector boundary, so read as many sectors as possible.
	ULONG BytesToTransfer = Length & (~(SectorSize - 1));
	while (BytesToTransfer != 0) {
		// Low-level driver only supports transfer of up to 64K sectors.
		ULONG SectorsToTransfer = BytesToTransfer / SectorSize;
		if (SectorsToTransfer > FileEntry->u.DiskContext.MaxSectorTransfer) SectorsToTransfer = FileEntry->u.DiskContext.MaxSectorTransfer;

		ULONG CurrentSector = FileEntry->Position / SectorSize;
		if ((CurrentSector + SectorsToTransfer) > SectorCount) {
			SectorsToTransfer = SectorCount - CurrentSector;
		}

		if (SectorsToTransfer == 0) break;

		CurrentSector += SectorStart;

		Status = FileEntry->ReadSectors(FileEntry, CurrentSector, SectorsToTransfer, Buffer);
		if (ARC_FAIL(Status)) return Status;

		ULONG Limit = SectorsToTransfer * SectorSize;
		*Count += Limit;
		Length -= Limit;
		Buffer = (PVOID)((size_t)Buffer + Limit);
		BytesToTransfer -= Limit;
		FileEntry->Position += Limit;
	}

	// If there's any data left to read, read the last sector.
	if (Length != 0) {
		int64_t OldPosition = FileEntry->Position;
		Status = DeblockerRead(FileId, LocalPointer, SectorSize, &TransferCount);
		if (ARC_SUCCESS(Status) && TransferCount != SectorSize) Status = _EIO;
		if (ARC_FAIL(Status)) {
			FileEntry->Position = OldPosition;
			return Status;
		}

		memcpy(Buffer, LocalPointer, Length);
		*Count += Length;
		FileEntry->Position += Length;
	}

	return _ESUCCESS;
}

static ARC_STATUS UsbDiskRead(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer) {
	PUSB_DEVICE_MOUNT_ENTRY MountEntry = FileEntry->u.DiskContext.DeviceMount;
	if (MountEntry == NULL) return _EBADF;

	int LowError = readwrite_blocks(MountEntry->Mount, StartSector, CountSectors, cbw_direction_data_in, Buffer);
	if (LowError != 0) return _EIO;
	return _ESUCCESS;
}

static ARC_STATUS DeblockerWrite(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	ULONG SectorSize;
	ARC_STATUS Status = FileEntry->GetSectorSize(FileId, &SectorSize);
	if (ARC_FAIL(Status)) return Status;
	ULONG SectorStart = FileEntry->u.DiskContext.SectorStart;
	ULONG SectorCount = FileEntry->u.DiskContext.SectorCount;
	ULONG TransferCount;

	PBYTE LocalPointer = (PVOID)(((ULONG)(&s_TemporaryBuffer[DCACHE_LINE_SIZE - 1])) & ~(DCACHE_LINE_SIZE - 1));

	*Count = 0;

	// If the current position is not at a sector boundary, read the first sector seperately, replace the data, and write back to disk
	ULONG Offset = FileEntry->Position & (SectorSize - 1);
	while (Offset != 0) {
		int64_t OldPosition = FileEntry->Position;
		FileEntry->Position -= Offset;
		int64_t WritePosition = FileEntry->Position;
		Status = DeblockerRead(FileId, LocalPointer, SectorSize, &TransferCount);
		if (ARC_SUCCESS(Status) && TransferCount != SectorSize) Status = _EIO;
		if (ARC_FAIL(Status)) {
			FileEntry->Position = OldPosition;
			return Status;
		}
		// Reset the position to before the read.
		FileEntry->Position = WritePosition;

		ULONG Limit;
		if ((SectorSize - Offset) > Length) Limit = Length;
		else Limit = SectorSize - Offset;
		memcpy(&LocalPointer[Offset], Buffer, Limit);

		// Write the sector.
		Status = DeblockerWrite(FileId, LocalPointer, SectorSize, &TransferCount);
		if (ARC_SUCCESS(Status) && TransferCount != SectorSize) Status = _EIO;
		if (ARC_FAIL(Status)) {
			FileEntry->Position = OldPosition;
			return Status;
		}

		Buffer = (PVOID)((size_t)Buffer + Limit);
		Length -= Limit;
		*Count += Limit;
		FileEntry->Position = OldPosition + Limit;
		Offset = FileEntry->Position & (SectorSize - 1);

		if (Length == 0) break;
	}

	// At a sector boundary, so write as many sectors as possible.
	ULONG BytesToTransfer = Length & (~(SectorSize - 1));
	while (BytesToTransfer != 0) {
		// Low-level driver only supports transfer of up to 64K sectors.
		ULONG SectorsToTransfer = BytesToTransfer / SectorSize;
		if (SectorsToTransfer > FileEntry->u.DiskContext.MaxSectorTransfer) SectorsToTransfer = FileEntry->u.DiskContext.MaxSectorTransfer;

		ULONG CurrentSector = FileEntry->Position / SectorSize;
		if ((CurrentSector + SectorsToTransfer) > SectorCount) {
			SectorsToTransfer = SectorCount - CurrentSector;
		}

		if (SectorsToTransfer == 0) break;

		CurrentSector += SectorStart;

		Status = FileEntry->WriteSectors(FileEntry, CurrentSector, SectorsToTransfer, Buffer);
		if (ARC_FAIL(Status)) return Status;

		ULONG Limit = SectorsToTransfer * SectorSize;
		*Count += Limit;
		Length -= Limit;
		Buffer = (PVOID)((size_t)Buffer + Limit);
		BytesToTransfer -= Limit;
		FileEntry->Position += Limit;
	}

	// If there's any data left to write, read the last sector seperately, replace the data, and write back to disk.
	if (Length != 0) {
		int64_t OldPosition = FileEntry->Position;
		ARC_STATUS Status = DeblockerRead(FileId, LocalPointer, SectorSize, &TransferCount);
		if (ARC_SUCCESS(Status) && TransferCount != SectorSize) Status = _EIO;
		if (ARC_FAIL(Status)) {
			FileEntry->Position = OldPosition;
			return Status;
		}
		FileEntry->Position = OldPosition;

		memcpy(LocalPointer, Buffer, Length);

		Status = DeblockerWrite(FileId, LocalPointer, SectorSize, &TransferCount);
		if (ARC_SUCCESS(Status) && TransferCount != SectorSize) Status = _EIO;
		FileEntry->Position = OldPosition;
		if (ARC_FAIL(Status)) return Status;

		*Count += Length;
		FileEntry->Position += Length;
	}

	return _ESUCCESS;
}

static ARC_STATUS UsbDiskWrite(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer) {
	PUSB_DEVICE_MOUNT_ENTRY MountEntry = FileEntry->u.DiskContext.DeviceMount;
	if (MountEntry == NULL) return _EBADF;

	int LowError = readwrite_blocks(MountEntry->Mount, StartSector, CountSectors, cbw_direction_data_out, Buffer);
	if (LowError != 0) return _EIO;
	return _ESUCCESS;
}

static ARC_STATUS UsbDiskGetReadStatus(ULONG FileId) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;

	PUSB_DEVICE_MOUNT_ENTRY MountEntry = FileEntry->u.DiskContext.DeviceMount;
	if (MountEntry == NULL) return _EBADF;

	int64_t LastByte = FileEntry->u.DiskContext.SectorCount;
	LastByte *= MountEntry->SectorSize;
	if (FileEntry->Position >= LastByte) return _EAGAIN;
	return _ESUCCESS;
}

static ARC_STATUS UsbDiskGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	PUSB_DEVICE_MOUNT_ENTRY MountEntry = FileEntry->u.DiskContext.DeviceMount;

	FileInfo->CurrentPosition.QuadPart = FileEntry->Position;
	int64_t Temp64 = FileEntry->u.DiskContext.SectorStart;
	Temp64 *= MountEntry->SectorSize;
	FileInfo->StartingAddress.QuadPart = Temp64;
	Temp64 = FileEntry->u.DiskContext.SectorCount;
	Temp64 *= MountEntry->SectorSize;
	FileInfo->EndingAddress.QuadPart = Temp64;
	FileInfo->Type = DiskPeripheral;

	return _ESUCCESS;
}

// IDE device vectors.
static const DEVICE_VECTORS IdeVectors = {
	.Open = IdeOpen,
	.Close = IdeClose,
	.Mount = IdeMount,
	.Read = DeblockerRead,
	.Write = DeblockerWrite,
	.Seek = DeblockerSeek,
	.GetReadStatus = IdeGetReadStatus,
	.GetFileInformation = IdeGetFileInformation,
	.SetFileInformation = NULL,
	.GetDirectoryEntry = NULL
};

// SD image device functions.

static ARC_STATUS IdeGetSectorSize(ULONG FileId, PULONG SectorSize) {
	// Get the file table entry.
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EFAULT;

	PIDE_DRIVE Drive = FileEntry->u.DiskContext.IdeDrive;
	if (Drive == NULL) return _EFAULT;

	*SectorSize = ob_ide_block_size(Drive);
	return _ESUCCESS;
}

static ARC_STATUS IdeOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PULONG FileId) {
	// Ensure the path starts with s_IdeControllerPath
	if (memcmp(OpenPath, s_IdeControllerPath, sizeof(s_IdeControllerPath) - 1) != 0) return _ENODEV;
	PCHAR DevicePath = &OpenPath[sizeof(s_IdeControllerPath) - 1];
	// Next device must be disk(x) or cdrom(x)
	ULONG Index = 0;
	bool IsCdRom = false;
	//bool IsFloppy = false;
	if (!ArcDeviceParse(&DevicePath, DiskController, &Index)) {
		// Not disk(x), check for cdrom(x)
		IsCdRom = true;
		if (!ArcDeviceParse(&DevicePath, CdromController, &Index)) {
			// Not cdrom either, can't handle this device path
			return _ENODEV;
		}
	}

	// Got the index, make sure this is good
	if (Index >= sizeof(s_IdeTable) / sizeof(s_IdeTable[0])) return _ENODEV;
	PIDE_DEVICE_MOUNT_ENTRY IdeDevice = &s_IdeTable[Index];
	if (!IdeDevice->Present) return _ENODEV;
	ULONG Channel = IdeDevice->Channel;
	ULONG Unit = IdeDevice->Unit;

	// For cdrom, next device must be fdisk(0)
	// For disk, next device must be rdisk(0)
	ULONG MustBeZero = 0;
	if (IsCdRom) {
		if (!ArcDeviceParse(&DevicePath, FloppyDiskPeripheral, &MustBeZero)) {
			return _ENODEV;
		}
		// should be the last device, but partition is still technically valid here.
	}
	else {
		if (!ArcDeviceParse(&DevicePath, DiskPeripheral, &MustBeZero)) {
			return _ENODEV;
		}
	}
	if (MustBeZero != 0) return _ENODEV;


	// If this is a cdrom, it can only be mounted ro.
	if (IsCdRom && OpenMode != ArcOpenReadOnly) return _EACCES;

	bool IncludesPartition = false;
	ULONG PartitionNumber = 0;
	// Does the caller want a partition?
	if (*DevicePath != 0) {
		if (!ArcDeviceParse(&DevicePath, PartitionEntry, &PartitionNumber)) {
			return _ENODEV;
		}
		else {
			// partition 0 means whole disk, and is the only valid partition for cdrom
			if (IsCdRom && PartitionNumber != 0) return _ENODEV;
			IncludesPartition = PartitionNumber != 0;
		}
	}

	// Get the file table entry.
	PARC_FILE_TABLE FileEntry = ArcIoGetFileForOpen(*FileId);
	if (FileEntry == NULL) return _EFAULT;
	// Zero the disk context.
	memset(&FileEntry->u.DiskContext, 0, sizeof(FileEntry->u.DiskContext));

	// Open the ide device.
	PIDE_DRIVE IdeDrive = ob_ide_open(Channel, Unit);
	if (IdeDrive == NULL) return _ENODEV;
	FileEntry->u.DiskContext.IdeDrive = IdeDrive;
	FileEntry->u.DiskContext.MaxSectorTransfer = IdeDrive->max_sectors;

	ULONG SectorSize = ob_ide_block_size(IdeDrive);

	// Stash the GetSectorSize ptr into the file table.
	FileEntry->GetSectorSize = IdeGetSectorSize;
	FileEntry->ReadSectors = IdeRead;
	FileEntry->WriteSectors = IdeWrite;

	ULONG DiskSectors = IdeDrive->sectors;
	ULONG PartitionSectors = DiskSectors;
	ULONG PartitionSector = 0;
	// Set it up for the whole disk so ArcFsPartitionObtain can work.
	FileEntry->u.DiskContext.SectorStart = PartitionSector;
	FileEntry->u.DiskContext.SectorCount = PartitionSectors;

	ARC_STATUS Status;
	if (IncludesPartition) {
		// Mark the file as open so ArcFsPartitionObtain can work.
		FileEntry->Flags.Open = 1;
		Status = ArcFsPartitionObtain(FileEntry->DeviceEntryTable, *FileId, PartitionNumber, SectorSize, &PartitionSector, &PartitionSectors);
		FileEntry->Flags.Open = 0;
		if (ARC_FAIL(Status)) {
#if 0
			// osloader uses partition 1 if it doesn't see any, so if the caller asks for that and it wasn't found, give them the whole disk
			if (PartitionNumber != 1) {
				fclose(f);
				return _ENODEV;
			}
#endif
			PartitionSector = 0;
			PartitionSectors = DiskSectors;
		}
	}
	FileEntry->u.DiskContext.SectorStart = PartitionSector;
	FileEntry->u.DiskContext.SectorCount = PartitionSectors;

	FileEntry->Position = 0;

	return _ESUCCESS;
}

static ARC_STATUS IdeClose(ULONG FileId) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	return _ESUCCESS;
}

static ARC_STATUS IdeMount(PCHAR MountPath, MOUNT_OPERATION Operation) { return _EINVAL; }


static ARC_STATUS IdeRead(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer) {
	bool Success = ob_ide_read_blocks(FileEntry->u.DiskContext.IdeDrive, Buffer, StartSector, CountSectors) == CountSectors;
	if (!Success) return _EIO;
	return _ESUCCESS;
}

static ARC_STATUS IdeWrite(PARC_FILE_TABLE FileEntry, ULONG StartSector, ULONG CountSectors, PVOID Buffer) {
	bool Success = ob_ide_write_blocks(FileEntry->u.DiskContext.IdeDrive, Buffer, StartSector, CountSectors) == CountSectors;
	if (!Success) return _EIO;
	return _ESUCCESS;
}

static ARC_STATUS IdeGetReadStatus(ULONG FileId) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	PIDE_DRIVE Drive = FileEntry->u.DiskContext.IdeDrive;
	if (Drive == NULL) return _EBADF;
	int64_t LastByte = FileEntry->u.DiskContext.SectorCount;
	LastByte *= ob_ide_block_size(Drive);
	if (FileEntry->Position >= LastByte) return _EAGAIN;
	return _ESUCCESS;
}

static ARC_STATUS IdeGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo) {
	PARC_FILE_TABLE FileEntry = ArcIoGetFile(FileId);
	if (FileEntry == NULL) return _EBADF;
	PIDE_DRIVE Drive = FileEntry->u.DiskContext.IdeDrive;
	if (Drive == NULL) return _EBADF;
	ULONG SectorSize = ob_ide_block_size(Drive);

	FileInfo->CurrentPosition.QuadPart = FileEntry->Position;
	int64_t Temp64 = FileEntry->u.DiskContext.SectorStart;
	Temp64 *= SectorSize;
	FileInfo->StartingAddress.QuadPart = Temp64;
	Temp64 = FileEntry->u.DiskContext.SectorCount;
	Temp64 *= SectorSize;
	FileInfo->EndingAddress.QuadPart = Temp64;
	FileInfo->Type = DiskPeripheral;

	return _ESUCCESS;
}

static bool ArcDiskUsbInit() {
	// Get the disk component.
	PCONFIGURATION_COMPONENT DiskComponent = ARC_VENDOR_VECTORS()->GetComponentRoutine(s_UsbControllerPath);
	// Ensure that the component obtained was really the disk component.
	if (DiskComponent->Class != AdapterClass) return false;
	if (DiskComponent->Type != ScsiAdapter) return false;
	if (DiskComponent->Key != 0) return false;

	// We really have a pointer to a device entry.
	PDEVICE_ENTRY DiskDevice = (PDEVICE_ENTRY)DiskComponent;

	DiskDevice->Vectors = &UsbDiskVectors;
	DiskDevice->Component.Identifier = (size_t)s_UsbComponentName;
	DiskDevice->Component.IdentifierLength = sizeof(s_UsbComponentName);
	return true;
}

static bool ArcDiskIdeInit() {
	// Get the disk component.
	PCONFIGURATION_COMPONENT DiskComponent = ARC_VENDOR_VECTORS()->GetComponentRoutine(s_IdeControllerPath);
	// Ensure that the component obtained was really the disk component.
	if (DiskComponent->Class != AdapterClass) return false;
	if (DiskComponent->Type != MultiFunctionAdapter) return false;
	if (DiskComponent->Key != 0) return false;

	// We really have a pointer to a device entry.
	PDEVICE_ENTRY DiskDevice = (PDEVICE_ENTRY)DiskComponent;

	DiskDevice->Vectors = &IdeVectors;
	
	DiskDevice->Component.Identifier = (size_t)s_IdeComponentName;
	DiskDevice->Component.IdentifierLength = sizeof(s_IdeComponentName);
	return true;
}

static bool ArcDiskAddDevice(PVENDOR_VECTOR_TABLE Api, PDEVICE_ENTRY BaseController, CONFIGURATION_TYPE Controller, CONFIGURATION_TYPE Disk, ULONG Key) {
	CONFIGURATION_COMPONENT ControllerConfig = ARC_MAKE_COMPONENT(ControllerClass, Controller, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, Key, 0);
	PDEVICE_ENTRY ControllerDevice = (PDEVICE_ENTRY)Api->AddChildRoutine(&BaseController->Component, &ControllerConfig, NULL);
	if (ControllerDevice == NULL) return false; // can't do anything if AddChild did fail
	ControllerDevice->Vectors = BaseController->Vectors;

	CONFIGURATION_COMPONENT DiskConfig = ARC_MAKE_COMPONENT(PeripheralClass, Disk, ARC_DEVICE_INPUT | ARC_DEVICE_OUTPUT, 0, 0);
	PDEVICE_ENTRY DiskDevice = (PDEVICE_ENTRY)Api->AddChildRoutine(&ControllerDevice->Component, &DiskConfig, NULL);
	if (DiskDevice == NULL) return false; // can't do anything if AddChild did fail
	DiskDevice->Vectors = BaseController->Vectors;
	return true;
}

static ULONG s_CountCdrom = 0;
static ULONG s_CountDisk = 0;
static ULONG s_CountPartitions[100] = { 0 };
static ULONG s_SizeDiskMb[100] = { 0 };

void ArcDiskGetCounts(PULONG Disk, PULONG Cdrom) {
	if (Disk != NULL) *Disk = s_CountDisk;
	if (Cdrom != NULL) *Cdrom = s_CountCdrom;
}

ULONG ArcDiskGetPartitionCount(ULONG Disk) {
	if (Disk >= s_CountDisk) return 0;
	return s_CountPartitions[Disk];
}

ULONG ArcDiskGetSizeMb(ULONG Disk) {
	if (Disk >= s_CountDisk) return 0;
	return s_SizeDiskMb[Disk];
}

void ArcDiskInitRamdisk(void);

void ArcDiskInit() {
	ArcDiskIdeInit();
	ArcDiskUsbInit();

	printf("Scanning disk devices...\r\n");

	char EnvKeyCd[] = "cd00:";
	char EnvKeyHd[] = "hd00p0:";
	char EnvKeyHdRaw[] = "hd00:";
	char DeviceName[64];

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	PDEVICE_ENTRY UsbController = (PDEVICE_ENTRY) Api->GetComponentRoutine(s_UsbControllerPath);
	PDEVICE_ENTRY IdeController = (PDEVICE_ENTRY) Api->GetComponentRoutine(s_IdeControllerPath);

	// IDE devices first.
	memset(s_IdeTable, 0, sizeof(s_IdeTable));
	ULONG IdeIndex = 0;

	ULONG CdromCount = 0;
	ULONG DiskCount = 0;

	for (const IDE_CHANNEL* IdeChannel = ob_ide_get_first_channel(); IdeChannel != NULL; IdeChannel = (const IDE_CHANNEL*)IdeChannel->next) {
		if (!IdeChannel->present) continue; // should never happen, but...
		ULONG ChannelNumber = IdeChannel->channel;

		for (ULONG DiskNumber = 0; DiskNumber < sizeof(IdeChannel->drives) / sizeof(IdeChannel->drives[0]); DiskNumber++) {
			const IDE_DRIVE* IdeDrive = (const IDE_DRIVE*)&IdeChannel->drives[DiskNumber];
			if (!IdeDrive->present) continue;
			if (IdeIndex > sizeof(s_IdeTable) / sizeof(s_IdeTable[0])) break;

			s_IdeTable[IdeIndex].Present = true;
			s_IdeTable[IdeIndex].Channel = ChannelNumber;
			s_IdeTable[IdeIndex].Unit = DiskNumber;

			// No length checks here, we are guaranteed to have a device id < 100!
			
			// what kind of drive is this?
			if (IdeDrive->media == ide_media_cdrom) {
				// Add the cdrom device.
				if (!ArcDiskAddDevice(Api, IdeController, CdromController, FloppyDiskPeripheral, IdeIndex)) continue;

				EnvKeyCd[3] = (CdromCount % 10) + '0';
				EnvKeyCd[2] = ((CdromCount / 10) % 10) + '0';
				CdromCount++;
				snprintf(DeviceName, sizeof(DeviceName), "%scdrom(%u)fdisk(0)", s_IdeControllerPath, IdeIndex);
				IdeIndex++;
				ArcEnvSetDevice(EnvKeyCd, DeviceName);
				printf("%s - IDE channel %d disk %d (raw disk)\r\n", EnvKeyCd, ChannelNumber, DiskNumber);
				continue;
			}

			// might not be hd, but do that anyway (BUGBUG)

			// Add the raw drive.
			if (!ArcDiskAddDevice(Api, IdeController, DiskController, DiskPeripheral, IdeIndex)) continue;
			EnvKeyHdRaw[3] = (DiskCount % 10) + '0';
			EnvKeyHdRaw[2] = ((DiskCount / 10) % 10) + '0';
			EnvKeyHd[3] = EnvKeyHdRaw[3];
			EnvKeyHd[2] = EnvKeyHdRaw[2];
			ULONG DiskIndex = DiskCount;
			DiskCount++;
			snprintf(DeviceName, sizeof(DeviceName), "%sdisk(%u)rdisk(0)", s_IdeControllerPath, IdeIndex);
			ULONG IdeHdIndex = IdeIndex;
			IdeIndex++;
			ArcEnvSetDevice(EnvKeyHdRaw, DeviceName);
			printf("%s - IDE channel %d disk %d\r\n", EnvKeyHdRaw, ChannelNumber, DiskNumber);

			s_CountPartitions[DiskIndex] = 0;
			s_SizeDiskMb[DiskIndex] = 0;

			// Attempt to fix the MBR now. Will fail if it doesn't look like the expected partition table layout.
			// We just added the device, we can open it:
			U32LE DeviceId;
			if (ARC_FAIL(Api->OpenRoutine(DeviceName, ArcOpenReadWrite, &DeviceId))) continue;
			ArcFsRestoreMbr(DeviceId.v);
			// Attempt to get the number of MBR partitions.
			if (ARC_FAIL(ArcFsPartitionCount(DeviceId.v, &s_CountPartitions[DiskIndex]))) s_CountPartitions[DiskIndex] = 0;
			// And the size of the disk.
			{
				FILE_INFORMATION Info;
				if (ARC_SUCCESS(Api->GetFileInformationRoutine(DeviceId.v, &Info))) s_SizeDiskMb[DiskIndex] = (ULONG)(Info.EndingAddress.QuadPart / 0x100000);
			}
			Api->CloseRoutine(DeviceId.v);

			for (ULONG part = 1; part <= s_CountPartitions[DiskIndex]; part++) {
				snprintf(DeviceName, sizeof(DeviceName), "%sdisk(%u)rdisk(0)partition(%d)", s_IdeControllerPath, IdeHdIndex, part);
				if (part < 10) {
					EnvKeyHd[5] = part + '0';
					EnvKeyHd[6] = ':';
					EnvKeyHd[7] = 0;
				}
				else {
					EnvKeyHd[6] = (part % 10) + '0';
					EnvKeyHd[5] = ((part / 10) % 10) + '0';
					EnvKeyHd[7] = ':';
				}
				ArcEnvSetDevice(EnvKeyHd, DeviceName);
				printf("%s - IDE channel %d disk %d (partition %d)\r\n", EnvKeyHd, ChannelNumber, DiskNumber, part);
			}
		}
	}

	// Next USB mass storage devices.
	for (int i = 0; i < sizeof(s_MountTable) / sizeof(s_MountTable[0]); i++) {
		// Mount the usb disk and get the sector size.
		PUSB_DEVICE_MOUNT_ENTRY Entry = &s_MountTable[i];
		if (Entry->Mount == NULL) continue;
		ULONG Key = Entry->Address;
		device_descriptor_t* devdesc = (device_descriptor_t*)Entry->Mount->descriptor;
		if (Entry->SectorSize == 2048) {
			// This looks like an optical drive.
			if (CdromCount < 100) {
				// Add the cdrom device.
				if (!ArcDiskAddDevice(Api, UsbController, CdromController, FloppyDiskPeripheral, Key)) continue;

				EnvKeyCd[3] = (CdromCount % 10) + '0';
				EnvKeyCd[2] = ((CdromCount / 10) % 10) + '0';
				CdromCount++;
				snprintf(DeviceName, sizeof(DeviceName), "%scdrom(%u)fdisk(0)", s_UsbControllerPath, Key);
				ArcEnvSetDevice(EnvKeyCd, DeviceName);
				printf("%s - USB device VID=%04x,PID=%04x\r\n", EnvKeyCd, devdesc->idVendor, devdesc->idProduct);
			}
		}
		else if (Entry->SectorSize == 0x200) {
			// This looks like a hard drive.
			if (DiskCount < 100) {
				// Add the raw drive device.
				if (!ArcDiskAddDevice(Api, UsbController, DiskController, DiskPeripheral, Key)) continue;
				EnvKeyHdRaw[3] = (DiskCount % 10) + '0';
				EnvKeyHdRaw[2] = ((DiskCount / 10) % 10) + '0';
				EnvKeyHd[3] = EnvKeyHdRaw[3];
				EnvKeyHd[2] = EnvKeyHdRaw[2];
				ULONG DiskIndex = DiskCount;
				DiskCount++;
				snprintf(DeviceName, sizeof(DeviceName), "%sdisk(%u)rdisk(0)", s_UsbControllerPath, Key);
				ArcEnvSetDevice(EnvKeyHdRaw, DeviceName);
				printf("%s - USB device VID=%04x,PID=%04x (raw disk)\r\n", EnvKeyHdRaw, devdesc->idVendor, devdesc->idProduct);

				U32LE DeviceId;
				if (ARC_FAIL(Api->OpenRoutine(DeviceName, ArcOpenReadWrite, &DeviceId))) continue;
				// Attempt to get the number of MBR partitions.
				if (ARC_FAIL(ArcFsPartitionCount(DeviceId.v, &s_CountPartitions[DiskIndex]))) s_CountPartitions[DiskIndex] = 0;
				// And the size of the disk.
				{
					FILE_INFORMATION Info;
					if (ARC_SUCCESS(Api->GetFileInformationRoutine(DeviceId.v, &Info))) s_SizeDiskMb[DiskIndex] = (ULONG)(Info.EndingAddress.QuadPart / 0x100000);
				}
				Api->CloseRoutine(DeviceId.v);

				for (ULONG part = 1; part <= s_CountPartitions[DiskIndex]; part++) {
					snprintf(DeviceName, sizeof(DeviceName), "%sdisk(%u)rdisk(0)partition(%d)", s_UsbControllerPath, Key, part);
					if (part < 10) {
						EnvKeyHd[5] = part + '0';
						EnvKeyHd[6] = ':';
						EnvKeyHd[7] = 0;
					}
					else {
						EnvKeyHd[6] = (part % 10) + '0';
						EnvKeyHd[5] = ((part / 10) % 10) + '0';
						EnvKeyHd[7] = ':';
					}
					ArcEnvSetDevice(EnvKeyHd, DeviceName);
					printf("%s - USB device VID=%04x,PID=%04x (partition %d)\r\n", EnvKeyHd, devdesc->idVendor, devdesc->idProduct, part);
				}
			}
		}
	}

	s_CountCdrom = CdromCount;
	s_CountDisk = DiskCount;

	// Now all drives are known, initialise the driver ramdisk if present.
	ArcDiskInitRamdisk();

	printf("Complete, found %d HDs, %d optical drives\r\n", DiskCount, CdromCount);
}
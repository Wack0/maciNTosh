#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "arc.h"
#include "arcdevice.h"
#include "arcconfig.h"
#include "arcio.h"
#include "arcfs.h"
#include "coff.h"

enum {
	ARC_DEVICE_PATH_SIZE = 64
};

typedef struct _OPENED_PATHNAME_ENTRY {
	ULONG   ReferenceCounter;
	CHAR    DeviceName[ARC_DEVICE_PATH_SIZE];
} OPENED_PATHNAME_ENTRY, * POPENED_PATHNAME_ENTRY;

static ARC_FILE_TABLE s_FileTable[FILE_TABLE_SIZE] = { 0 };
static OPENED_PATHNAME_ENTRY s_OpenedFiles[FILE_TABLE_SIZE] = { 0 };
_Static_assert((sizeof(s_OpenedFiles) / sizeof(*s_OpenedFiles)) == (sizeof(s_FileTable) / sizeof(*s_FileTable)), "Number of file table entries must equal number of opened pathname entries");

/// <summary>
/// Gets the file table entry by file ID.
/// </summary>
/// <param name="FileId">File ID to obtain.</param>
/// <returns>File table entry.</returns>
PARC_FILE_TABLE ArcIoGetFile(ULONG FileId) {
	// Length check.
	if (FileId >= FILE_TABLE_SIZE) return NULL;
	PARC_FILE_TABLE Entry = &s_FileTable[FileId];

	// Any external caller needs an open file.
	if (!Entry->Flags.Open) return NULL;

	return Entry;
}

/// <summary>
/// Gets the file table entry by file ID. (file ID must not be open)
/// </summary>
/// <param name="FileId">File ID to obtain.</param>
/// <returns>File table entry.</returns>
PARC_FILE_TABLE ArcIoGetFileForOpen(ULONG FileId) {
	// Length check.
	if (FileId >= FILE_TABLE_SIZE) return NULL;
	PARC_FILE_TABLE Entry = &s_FileTable[FileId];

	// External caller wants a closed file.
	if (Entry->Flags.Open) return NULL;

	return Entry;
}

/// <summary>
/// Gets a free file table entry (the first that isn't open)
/// </summary>
/// <param name="FileId">Out pointer that obtains the free file table index.</param>
/// <returns>ARC status</returns>
static ARC_STATUS ArcIoGetFreeFile(PULONG FileId) {
	for (ULONG i = 0; i < sizeof(s_FileTable) / sizeof(*s_FileTable); i++) {
		if (s_FileTable[i].Flags.Open) continue;
		*FileId = i;
		return _ESUCCESS;
	}
	return _EMFILE;
}

// Ensure file table entry refers to an open file, with the specified permissions.
static ARC_STATUS ArcIoEnsurePermissions(PARC_FILE_TABLE File, bool Read, bool Write) {
	if (File == NULL) return _EACCES;
	if (Read && !File->Flags.Read) return _EACCES;
	if (Write && !File->Flags.Write) return _EACCES;
	return _ESUCCESS;
}


static ARC_STATUS ArcGetFileInformation(ULONG FileId, PFILE_INFORMATION Info) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, false, false);
	if (ARC_FAIL(Status)) return Status;

	// There was an ABI break here; FILE_INFORMATION structure had FileName element added after ~build 297.
	// However, those early builds were only for MIPS and x86, ARC port to PPC happened with the stable ABI.
	
#ifdef ARC_MIPS_FIX_ABI_BREAK
	FILE_INFORMATION LocalInfo;
	Status = File->DeviceEntryTable->GetFileInformation(FileId, &LocalInfo);
	if (ARC_FAIL(Status)) return Status;
	memcpy(Info, &LocalInfo, offsetof(FILE_INFORMATION, FileName));
	return Status;
#else
	return File->DeviceEntryTable->GetFileInformation(FileId, Info);
#endif
}

static ARC_STATUS ArcSetFileInformation(ULONG FileId, ULONG AttributeFlags, ULONG AttributeMask) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, false, false);
	if (ARC_FAIL(Status)) return Status;

	if (File->DeviceId == FILE_IS_RAW_DEVICE) return _EACCES;

	return File->DeviceEntryTable->SetFileInformation(FileId, AttributeFlags, AttributeMask);
}

static ARC_STATUS ArcMount(PCHAR MountPath, MOUNT_OPERATION Operation) {
	// Not implemented.
	return _ESUCCESS;
}

static ARC_STATUS ArcRead(ULONG FileId, PVOID Buffer, ULONG Length, PU32LE Count) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, true, false);
	if (ARC_FAIL(Status)) return Status;

	ULONG LocalCount;
	Status = File->DeviceEntryTable->Read(FileId, Buffer, Length, &LocalCount);
	if (ARC_SUCCESS(Status)) Count->v = LocalCount;
	return Status;
}

static ARC_STATUS ArcGetReadStatus(ULONG FileId) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, true, false);
	if (ARC_FAIL(Status)) return Status;

	if (File->DeviceEntryTable->GetReadStatus == NULL) return _EACCES;
	return File->DeviceEntryTable->GetReadStatus(FileId);
}

static ARC_STATUS ArcSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, false, false);
	if (ARC_FAIL(Status)) return Status;

	return File->DeviceEntryTable->Seek(FileId, Offset, SeekMode);
}

static ARC_STATUS ArcWrite(ULONG FileId, PVOID Buffer, ULONG Length, PU32LE Count) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, false, true);
	if (ARC_FAIL(Status)) return Status;

	ULONG LocalCount;
	Status = File->DeviceEntryTable->Write(FileId, Buffer, Length, &LocalCount);
	if (ARC_SUCCESS(Status)) Count->v = LocalCount;
	return Status;
}

static ARC_STATUS ArcGetDirectoryEntry(ULONG FileId, PDIRECTORY_ENTRY Buffer, ULONG Length, PU32LE Count) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, true, false);
	if (ARC_FAIL(Status)) return Status;

	if (File->DeviceId == FILE_IS_RAW_DEVICE) return _EBADF;

	if (File->DeviceEntryTable->GetDirectoryEntry == NULL) return _EBADF;
	ULONG LocalCount;
	Status = File->DeviceEntryTable->GetDirectoryEntry(FileId, Buffer, Length, &LocalCount);
	if (ARC_SUCCESS(Status)) Count->v = LocalCount;
	return Status;
}

static ARC_STATUS ArcCloseDeviceImpl(PARC_FILE_TABLE File, ULONG FileId) {
	// Decrement the refcount.
	ULONG NewRefCount = s_OpenedFiles[FileId].ReferenceCounter - 1;
	if (NewRefCount == 0) {
		// Unmount the filesystem if needed.
		FsUnmountForDevice(FileId);
		// No more references, so close the device.
		ARC_STATUS Status = File->DeviceEntryTable->Close(FileId);
		if (ARC_FAIL(Status)) return Status;
		// Close succeeded, so clean up the opened file.
		// Clear the device name.
		s_OpenedFiles[FileId].DeviceName[0] = 0;
		// Zero the flags.
		memset(&File->Flags, 0, sizeof(File->Flags));
	}
	// Store the new reference count.
	s_OpenedFiles[FileId].ReferenceCounter = NewRefCount;
	// All done.
	return _ESUCCESS;
}

static ARC_STATUS ArcClose(ULONG FileId) {
	// Get the file table entry.
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	ARC_STATUS Status = ArcIoEnsurePermissions(File, false, false);
	if (ARC_FAIL(Status)) return Status;

	// Closing a device?
	if (File->DeviceId == FILE_IS_RAW_DEVICE) {
		return ArcCloseDeviceImpl(File, FileId);
	}

	// Closing a file.
	// Get the device id so we can close it later.
	ULONG DeviceId = File->DeviceId;
	// Close the file.
	Status = File->DeviceEntryTable->Close(FileId);
	if (ARC_FAIL(Status)) return Status;
	// Zero the flags.
	memset(&File->Flags, 0, sizeof(File->Flags));
	// Close the device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	Status = ArcIoEnsurePermissions(Device, false, false);
	if (ARC_FAIL(Status)) return Status;
	if (Device->DeviceId != FILE_IS_RAW_DEVICE) return _EACCES;
	return ArcCloseDeviceImpl(Device, DeviceId);
}

static PCHAR ArcOpenGetFileName(PCHAR OpenPath) {
	PCHAR ptr = OpenPath;
	PCHAR FileName = OpenPath;
	for (; *ptr != 0; ptr++) {
		// Search for ')' character indicating end of device part
		if (*ptr == ')') {
			FileName = ptr + 1;
			// If next character is '\\' indicating root path then this is the correct place to cut
			if (*FileName == '\\') return FileName;
			// If next character is null terminator then this is the correct place to cut
			if (*FileName == 0) return FileName;
		}
	}
	// There's data after the last device part but it's not the start of a file path.
	// Device might implement stuff wanting it, the end of the device parts are known anyway
	return FileName;
}

static ARC_STATUS ArcOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PU32LE FileId) {
	// Get the device name and file name from the specified path.
	PCHAR FileName = ArcOpenGetFileName(OpenPath);

	// Canonicalise the device name.
	char CanonicalisedDevice[ARC_DEVICE_PATH_SIZE];
	ULONG CanonicalisedDeviceLength = 0;
	{
		PCHAR pCanon = CanonicalisedDevice;
		ULONG lenCanon = 0;
		char LastChar = 0;
		for (PCHAR pDevice = OpenPath; pDevice != FileName; pDevice++) {
			char ThisChar = *pDevice;
			// If this char is ')', and last char was '(', add a zero.
			// Such that: "()" becomes "(0)"
			if (ThisChar == ')' && LastChar == '(') {
				*pCanon = '0';
				pCanon++;
				lenCanon++;
				if (lenCanon == sizeof(CanonicalisedDevice)) return _E2BIG;
			}

			// Copy this char, and lowercase it if required
			if (ThisChar >= 'A' && ThisChar <= 'Z') ThisChar |= 0x20;
			*pCanon = ThisChar;
			pCanon++;
			lenCanon++;
			if (lenCanon == sizeof(CanonicalisedDevice)) return _E2BIG;
		}
		*pCanon = 0;
		CanonicalisedDeviceLength = lenCanon + 1;
	}

	//printf("ArcOpen: %s\r\n", CanonicalisedDevice);

	// Convert file open mode to device open mode.
	OPEN_MODE DeviceOpenMode = OpenMode;
	if (*FileName == 0) {
		// A device is being opened, OpenMode must be a device open mode
		if (DeviceOpenMode > ArcOpenReadWrite) return _EINVAL;
	}
	else {
		// A file is being opened, if it's anything other than open read-only then open the device read-write
		if (DeviceOpenMode > ArcOpenReadOnly) DeviceOpenMode = ArcOpenReadWrite;
	}

	// Is this device already open?
	ULONG DeviceId = 0;
	PARC_FILE_TABLE Device = NULL;
	for (; DeviceId < sizeof(s_OpenedFiles) / sizeof(*s_OpenedFiles); DeviceId++) {
		if (s_OpenedFiles[DeviceId].ReferenceCounter == 0) continue;
		if (strcmp(CanonicalisedDevice, s_OpenedFiles[DeviceId].DeviceName) != 0) continue;
		Device = ArcIoGetFile(DeviceId);
		// Check if it's opened in the same mode.
		if (ARC_FAIL(ArcIoEnsurePermissions(Device, DeviceOpenMode != ArcOpenWriteOnly, DeviceOpenMode != ArcOpenReadOnly))) continue;
		// Increment the reference counter.
		ULONG NewRefCount = s_OpenedFiles[DeviceId].ReferenceCounter + 1;
		if (NewRefCount == 0) continue; // Can't increment the refcount, it'll overflow.
		s_OpenedFiles[DeviceId].ReferenceCounter = NewRefCount;
		break;
	}

	ARC_STATUS Status = _ESUCCESS;

	if (DeviceId == sizeof(s_OpenedFiles) / sizeof(*s_OpenedFiles)) {
		// Device is not yet opened.
		// Find the nearest config entry for this device.
		PCONFIGURATION_COMPONENT ConfigEntry = ARC_VENDOR_VECTORS()->GetComponentRoutine(CanonicalisedDevice);
		if (ConfigEntry == NULL) return _ENODEV;

		// We've really got a PDEVICE_ENTRY, so cast it and check the vector table
		PDEVICE_ENTRY DeviceEntry = (PDEVICE_ENTRY)ConfigEntry;
		if (DeviceEntry->Vectors == NULL) return _ENODEV;

		// Get a free file table entry.
		Status = ArcIoGetFreeFile(&DeviceId);
		if (ARC_FAIL(Status)) return Status;

		// Get the file table entry.
		Device = &s_FileTable[DeviceId];

		// Set the vector table in the file.
		Device->DeviceEntryTable = DeviceEntry->Vectors;
		// Open the device.
		Status = Device->DeviceEntryTable->Open(CanonicalisedDevice, DeviceOpenMode, &DeviceId);
		if (ARC_FAIL(Status)) return Status;
		// Mark the device as open, and as a raw device.
		Device->Flags.Open = 1;
		Device->DeviceId = FILE_IS_RAW_DEVICE;
		if (DeviceOpenMode == ArcOpenReadOnly || DeviceOpenMode == ArcOpenReadWrite) Device->Flags.Read = 1;
		if (DeviceOpenMode == ArcOpenWriteOnly || DeviceOpenMode == ArcOpenReadWrite) Device->Flags.Write = 1;
		// Initialise the device name and reference count.
		memcpy(s_OpenedFiles[DeviceId].DeviceName, CanonicalisedDevice, CanonicalisedDeviceLength);
		s_OpenedFiles[DeviceId].ReferenceCounter = 1;
	}

	// Device is open now.
	if (*FileName == 0) {
		// Caller wants the raw device, so give it to them.
		FileId->v = DeviceId;
		return _ESUCCESS;
	}

	// Open the file.

	// Get a free file table entry.
	ULONG LocalFileId;
	Status = ArcIoGetFreeFile(&LocalFileId);
	if (ARC_FAIL(Status)) {
		ArcCloseDeviceImpl(Device, DeviceId);
		return Status;
	}

	// Mount the filesystem.
	Status = FsInitialiseForDevice(DeviceId);
	if (ARC_FAIL(Status)) {
		ArcCloseDeviceImpl(Device, DeviceId);
		return Status;
	}

	// Set the device ID in the file table.
	PARC_FILE_TABLE FileEntry = &s_FileTable[LocalFileId];

	FileEntry->DeviceId = DeviceId;
	FsInitialiseTable(FileEntry);

	// Open the file.
	Status = FileEntry->DeviceEntryTable->Open(FileName, OpenMode, &LocalFileId);
	if (ARC_FAIL(Status)) {
		//FsUnmountForDevice(DeviceId);
		ArcCloseDeviceImpl(Device, DeviceId);
		return Status;
	}
	FileEntry->Flags.Open = 1;

	FileId->v = LocalFileId;
	return Status;
}

void ArcIoInit() {
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	Api->CloseRoutine = ArcClose;
	Api->MountRoutine = ArcMount;
	Api->OpenRoutine = ArcOpen;
	Api->ReadRoutine = ArcRead;
	Api->WriteRoutine = ArcWrite;
	Api->ReadStatusRoutine = ArcGetReadStatus;
	Api->SeekRoutine = ArcSeek;
	Api->GetFileInformationRoutine = ArcGetFileInformation;
	Api->SetFileInformationRoutine = ArcSetFileInformation;
	Api->GetDirectoryEntryRoutine = ArcGetDirectoryEntry;
}
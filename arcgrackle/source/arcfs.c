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
#include "arcdisk.h"
#include "arcenv.h"
#include "arcio.h"
#include "arcfs.h"
#include "coff.h"
#include "lib9660.h"
#include "diskio.h"
#include "pff.h"

// Partition table parsing stuff.
typedef struct ARC_LE ARC_PACKED _PARTITION_ENTRY {
	BYTE Active;
	BYTE StartChs[3];
	BYTE Type;
	BYTE EndChs[3];
	ULONG SectorStart;
	ULONG SectorCount;
} PARTITION_ENTRY, *PPARTITION_ENTRY;

// Driver descriptor entry
typedef struct ARC_BE ARC_PACKED _DDT_DRIVER_ENTRY {
	ULONG SectorStart;
	USHORT SectorCount;
	USHORT OsType; // classic MacOS = 1, ptDR = 0x0701, wiki = 0xF8FF
} DDT_DRIVER_ENTRY, *PDDT_DRIVER_ENTRY;

enum {
	// This is very specific.
	// This is: (sizeof(MBR_SECTOR.MbrCode) - offsetof(DDT_ENTRY, Drivers)) / sizeof(DDT_DRIVER_ENTRY).
	// that is, enough DDT_DRIVER_ENTRY structures such that DDT_ENTRY structure can fit into MBR_SECTOR.MbrCode.
	DDT_DRIVER_COUNT_FOR_MBR = 0x34
};

// Driver descriptor table for APM, at sector 0.
typedef struct ARC_BE ARC_PACKED _DDT_ENTRY {
	USHORT Signature;
	USHORT SectorSize;
	ULONG SectorCount; // of whole disk
	USHORT DeviceType;
	USHORT DeviceId;
	ULONG Data;
	USHORT DriverCount;
	DDT_DRIVER_ENTRY Drivers[DDT_DRIVER_COUNT_FOR_MBR];
} DDT_ENTRY, *PDDT_ENTRY;

// Ensure at compile time that DDT_ENTRY can fit into MbrCode.
_Static_assert(sizeof(DDT_ENTRY) <= 0x1B8);

// MBR (sector 0)
typedef struct ARC_LE ARC_PACKED _MBR_SECTOR {
	union {
		BYTE MbrCode[0x1B8];
		DDT_ENTRY ApmDdt;
	};
	ULONG Signature;
	USHORT Reserved;
	PARTITION_ENTRY Partitions[4];
	USHORT ValidMbr;
} MBR_SECTOR, *PMBR_SECTOR;
_Static_assert(sizeof(MBR_SECTOR) == 0x200);

// APM partition table. (each entry starts at sector 1)
typedef struct ARC_BE ARC_PACKED _APM_SECTOR {
	ULONG Signature;
	ULONG ApmTableSectors; // aka partition count
	ULONG SectorStart;
	ULONG SectorCount;
	char Name[32];
	char Type[32];
	ULONG RelativeSectorStartData;
	ULONG SectorCountData;
	ULONG Status; // "only used by A/UX"
	ULONG RelativeSectorStartBoot;
	ULONG LengthBoot;
	ULONG BootCodeBaseAddress[2];
	ULONG BootCodeEntryPoint[2];
	ULONG BootCodeChecksum;
	char BootCodeArchitecture[16];
	BYTE UnusedData[0x178];
} APM_SECTOR, *PAPM_SECTOR;
_Static_assert(sizeof(APM_SECTOR) == 0x200);

enum {
	MBR_VALID_SIGNATURE = 0xAA55,
	APM_VALID_SIGNATURE = 0x504D0000,
	DDT_VALID_SIGNATURE = 0x4552,
	PARTITION_TYPE_FREE = 0,
	PARTITION_TYPE_EXTENDED_CHS = 5,
	PARTITION_TYPE_EXTENDED_LBA = 0xF
};

// Boyer-Moore Horspool algorithm adapted from http://www-igm.univ-mlv.fr/~lecroq/string/node18.html#SECTION00180
static PBYTE mem_mem(PBYTE startPos, const void* pattern, size_t size, size_t patternSize)
{
	const BYTE* patternc = (const BYTE*)pattern;
	size_t table[256];

	// Preprocessing
	for (ULONG i = 0; i < 256; i++)
		table[i] = patternSize;
	for (size_t i = 0; i < patternSize - 1; i++)
		table[patternc[i]] = patternSize - i - 1;

	// Searching
	size_t j = 0;
	while (j <= size - patternSize)
	{
		BYTE c = startPos[j + patternSize - 1];
		if (patternc[patternSize - 1] == c && memcmp(pattern, startPos + j, patternSize - 1) == 0)
			return startPos + j;
		j += table[c];
	}

	return NULL;
}

static void ApmpInit(PAPM_SECTOR Apm, ULONG ApmPartitionCount, ULONG SectorStart, ULONG SectorCount, bool BootCode) {
	Apm->Signature = APM_VALID_SIGNATURE;
	Apm->ApmTableSectors = ApmPartitionCount;
	Apm->SectorStart = SectorStart;
	Apm->SectorCount = SectorCount;
	Apm->SectorCountData = SectorCount;
	// osx disk utility does this for some reason??
	memset(&Apm->UnusedData[0x14C], 0xFF, 4);
	Apm->Status = 0x7f;
	if (BootCode) {
		Apm->Status |= 0x300;
	}
}

/// <summary>
/// Gets the number of partitions in the Apple Partition Map.
/// </summary>
/// <param name="DeviceVectors">Device function table.</param>
/// <param name="DeviceId">Device ID.</param>
/// <param name="SectorSize">Sector size for the device.</param>
/// <returns>Partition count, 0 if error occurred or disk has no Apple Partition Map.</returns>
ULONG ArcFsApmPartitionCount(PDEVICE_VECTORS DeviceVectors, ULONG DeviceId, ULONG SectorSize) {
	ULONG CheckedPartitionCount = 0;

	do {
		// Read sector 1.
		int64_t Position64 = SectorSize;
		LARGE_INTEGER Position = INT64_TO_LARGE_INTEGER(Position64);

		ARC_STATUS Status = DeviceVectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) {
			//printf("Seek(1) fail %s\r\n", ArcGetErrorString(Status));
			break;
		}

		APM_SECTOR Apm = { 0 };
		ULONG Count;

		Status = DeviceVectors->Read(DeviceId, &Apm, sizeof(Apm), &Count);
		if (ARC_FAIL(Status)) {
			//printf("Read(1) fail %s\r\n", ArcGetErrorString(Status));
			break;
		}
		if (Count != sizeof(Apm)) {
			//printf("Read(1) fail %x!=%x\r\n", Count, sizeof(Apm));
			break;
		}
		if (Apm.Signature != APM_VALID_SIGNATURE) {
			//printf("Not APM(1), %x!=%x\r\n", Apm.Signature, APM_VALID_SIGNATURE);
			break;
		}

		ULONG PartitionCount = Apm.ApmTableSectors;

		// Double check the partition count we were provided.
		CheckedPartitionCount++;
		for (ULONG i = 1; i < PartitionCount; i++, CheckedPartitionCount++) {
			// Seek to sector.
			Position64 = SectorSize;
			Position64 *= (i + 1);
			Position = INT64_TO_LARGE_INTEGER(Position64);
			Status = DeviceVectors->Seek(DeviceId, &Position, SeekAbsolute);
			if (ARC_FAIL(Status)) {
				//printf("Seek(%d) fail %s\r\n", i+1, ArcGetErrorString(Status));
				break;
			}

			Status = DeviceVectors->Read(DeviceId, &Apm, sizeof(Apm), &Count); if (ARC_FAIL(Status)) {
				//printf("Read(%d) fail %s\r\n", i+1, ArcGetErrorString(Status));
				break;
			}
			if (Count != sizeof(Apm)) {
				//printf("Read(%d) fail %x!=%x\r\n", i+1, Count, sizeof(Apm));
				break;
			}
			if (Apm.Signature != APM_VALID_SIGNATURE) {
				//printf("Not APM(%d), %x!=%x\r\n", i+1, Apm.Signature, APM_VALID_SIGNATURE);
				break;
			}
			if (Apm.ApmTableSectors != PartitionCount) {
				//printf("Bad APM(%d), %x!=%x\r\n", i + 1, Apm.ApmTableSectors, PartitionCount);
				break;
			}

			// ok this partition looks good
		}
	} while (false);

	return CheckedPartitionCount;
}

/// <summary>
/// Gets the start and length of a partition.
/// </summary>
/// <param name="DeviceVectors">Device function table.</param>
/// <param name="DeviceId">Device ID.</param>
/// <param name="PartitionId">Partition number to obtain (1-indexed)</param>
/// <param name="SectorSize">Sector size for the device.</param>
/// <param name="SectorStart">On success obtains the start sector for the partition</param>
/// <param name="SectorCount">On success obtains the number of sectors of the partition</param>
/// <returns>ARC status code.</returns>
ARC_STATUS ArcFsPartitionObtain(PDEVICE_VECTORS DeviceVectors, ULONG DeviceId, ULONG PartitionId, ULONG SectorSize, PULONG SectorStart, PULONG SectorCount) {
	ULONG CurrentPartition = 0;

	// Seek to sector zero.
	ULONG PositionSector = 0;

	do {
		// Seek to MBR or extended partition position.
		int64_t Position64 = PositionSector;
		Position64 *= SectorSize;
		LARGE_INTEGER Position = INT64_TO_LARGE_INTEGER(Position64);
		ARC_STATUS Status = DeviceVectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) return Status;

		// Read single sector.
		MBR_SECTOR Mbr;
		ULONG Count;
		Status = DeviceVectors->Read(DeviceId, &Mbr, sizeof(Mbr), &Count);
		if (ARC_FAIL(Status)) return Status;
		if (Count != sizeof(Mbr)) return _EBADF;

		// Ensure MBR looks valid.
		if (Mbr.ValidMbr != MBR_VALID_SIGNATURE) return _EBADF;

		// Walk through all partitions in the MBR
		// Save off a pointer to the extended partition, if needed.
		PPARTITION_ENTRY ExtendedPartition = NULL;
		for (BYTE i = 0; i < sizeof(Mbr.Partitions) / sizeof(Mbr.Partitions[0]); i++) {
			BYTE Type = Mbr.Partitions[i].Type;
			if (Type == PARTITION_TYPE_EXTENDED_CHS || Type == PARTITION_TYPE_EXTENDED_LBA) {
				if (ExtendedPartition == NULL) ExtendedPartition = &Mbr.Partitions[i];
				else {
					// More than one extended partition is invalid.
					// No operation, for now, just only use the first extended partition.
				}
				continue;
			}
			if (Type == PARTITION_TYPE_FREE) continue;
			CurrentPartition++;
			if (CurrentPartition == PartitionId) {
				// Found the wanted partition.
				*SectorStart = PositionSector + Mbr.Partitions[i].SectorStart;
				*SectorCount = Mbr.Partitions[i].SectorCount;
				return _ESUCCESS;
			}
		}

		// Didn't find the partition.
		// If there's no extended partition, then error.
		if (ExtendedPartition == NULL) return _ENODEV;
		if (ExtendedPartition->SectorCount == 0) return _ENODEV;

		// Seek to extended partition.
		PositionSector += ExtendedPartition->SectorStart;
	} while (true);
	// should never reach here
	return _ENODEV;
}

/// <summary>
/// Gets the number of partitions on a disk (MBR only).
/// </summary>
/// <param name="DeviceId">Device ID.</param>
/// <param name="PartitionCount">On success obtains the number of partitions</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsPartitionCount(ULONG DeviceId, PULONG PartitionCount) {
	if (PartitionCount == NULL) return _EINVAL;
	// Get the device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;
	// Can't be a file.
	if (Device->DeviceId != FILE_IS_RAW_DEVICE) return _EBADF;
	if (Device->GetSectorSize == NULL) return _EBADF;
	ULONG SectorSize;
	if (ARC_FAIL(Device->GetSectorSize(DeviceId, &SectorSize))) return _EBADF;
	// Must have a sector size at least 512 bytes.
	if (SectorSize < sizeof(MBR_SECTOR)) return _EBADF;
	PDEVICE_VECTORS DeviceVectors = Device->DeviceEntryTable;

	//*PartitionCount = ArcFsApmPartitionCount(DeviceVectors, DeviceId, Device->u.DiskContext.SectorSize);
	*PartitionCount = ArcFsMbrPartitionCount(DeviceVectors, DeviceId, SectorSize);
	return _ESUCCESS;
}

/// <summary>
/// Gets the number of MBR partitions on a disk.
/// </summary>
/// <param name="DeviceVectors">Device function table.</param>
/// <param name="DeviceId">Device ID.</param>
/// <param name="SectorSize">Sector size for the device.</param>
/// <returns>Number of partitions or 0 on failure</returns>
ULONG ArcFsMbrPartitionCount(PDEVICE_VECTORS DeviceVectors, ULONG DeviceId, ULONG SectorSize) {
	ULONG MbrPartitionCount = 0;

	// Seek to sector zero.
	ULONG PositionSector = 0;

	do {
		// Seek to MBR or extended partition position.
		int64_t Position64 = PositionSector;
		Position64 *= SectorSize;
		LARGE_INTEGER Position = INT64_TO_LARGE_INTEGER(Position64);
		ARC_STATUS Status = DeviceVectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) break;

		// Read single sector.
		MBR_SECTOR Mbr;
		ULONG Count;
		Status = DeviceVectors->Read(DeviceId, &Mbr, sizeof(Mbr), &Count);
		if (ARC_FAIL(Status)) break;
		if (Count != sizeof(Mbr)) break;

		// Ensure MBR looks valid.
		if (Mbr.ValidMbr != MBR_VALID_SIGNATURE) break;

		// Walk through all partitions in the MBR
		// Save off a pointer to the extended partition, if needed.
		PPARTITION_ENTRY ExtendedPartition = NULL;
		for (BYTE i = 0; i < sizeof(Mbr.Partitions) / sizeof(Mbr.Partitions[0]); i++) {
			BYTE Type = Mbr.Partitions[i].Type;
			if (Type == PARTITION_TYPE_EXTENDED_CHS || Type == PARTITION_TYPE_EXTENDED_LBA) {
				if (ExtendedPartition == NULL) ExtendedPartition = &Mbr.Partitions[i];
				else {
					// More than one extended partition is invalid.
					// No operation, for now, just only use the first extended partition.
				}
				continue;
			}
			if (Type == PARTITION_TYPE_FREE) continue;
			MbrPartitionCount++;
		}

		// Didn't find the partition.
		// If there's no extended partition, then MBR has been successfully enumerated.
		if (ExtendedPartition == NULL) break;
		if (ExtendedPartition->SectorCount == 0) break;

		// Seek to extended partition.
		PositionSector += ExtendedPartition->SectorStart;
	} while (true);
	return MbrPartitionCount;
}

static ULONG ApmpRolw1(USHORT Value) {
	return (Value << 1) | (Value >> 15);
}

// same algorithm as in OSX MediaKit
static ULONG ApmpDriverChecksum(PUCHAR Buffer, ULONG Length) {
	ULONG Checksum = 0;
	for (ULONG i = 0; i < Length; i++) {
		Checksum = ApmpRolw1((USHORT)(Buffer[i] + Checksum));
	}
	if (Checksum == 0) return 0xFFFF;
	return (USHORT)Checksum;
}

static unsigned int Crc32(PVOID Buffer, ULONG Length) {
	ULONG i;
	int j;
	unsigned int byte, crc, mask;

	PUCHAR Message = (PUCHAR)Buffer;

	i = 0;
	crc = 0xFFFFFFFF;
	while (i < Length) {
		byte = Message[i];            // Get next byte.
		crc = crc ^ byte;
		for (j = 7; j >= 0; j--) {    // Do eight times.
			mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
		i = i + 1;
	}
	return ~crc;
}

static USHORT RppUcs2UpperCaseByTable(USHORT Char) {
	static const USHORT sc_UnicodeTables[] = {
		// This table is the data from l_intl.nls, but without the 2-element header, and only the uppercase half of the data.
#include "ucs2tbl.inc"
	};

	if (Char < 'a') return Char;
	if (Char <= 'z') return (Char - 'a' + 'A');

	USHORT Offset = Char >> 8;
	Offset = sc_UnicodeTables[Offset];
	Offset += (Char >> 4) & 0xF;
	Offset = sc_UnicodeTables[Offset];
	Offset += (Char & 0xF);
	Offset = sc_UnicodeTables[Offset];

	return Char + (SHORT)Offset;
}

static ARC_STATUS RppWriteUpCaseTable(ULONG DeviceId, PDEVICE_VECTORS Vectors) {
	// Writes the $UpCase table to disk at current location.
	// We compute the table and write a sector at a time.
	BYTE Sector[0x200];
	PU16LE pLittle = (PU16LE)(ULONG)Sector;

	ULONG Char = 0;
	while (Char <= 0xFFFF) {
		// Fill in this sector
		for (ULONG i = 0; i < sizeof(Sector) / sizeof(*pLittle); i++, Char++) {
			pLittle[i].v = RppUcs2UpperCaseByTable((USHORT)Char);
		}
		// And write it to disk
		ULONG Count = 0;
		ARC_STATUS Status = Vectors->Write(DeviceId, Sector, sizeof(Sector), &Count);
		if (ARC_FAIL(Status) || Count != sizeof(Sector)) {
			if (ARC_SUCCESS(Status)) Status = _EIO;
			return Status;
		}
	}
	return _ESUCCESS;
}

static ARC_STATUS RppWriteAttrDef(ULONG DeviceId, PDEVICE_VECTORS Vectors) {
	// Writes the AttrDef file to disk at current location.
	static const BYTE sc_AttrDef[] = {
#include "attrdef.inc"
	};

	ULONG Count = 0;
	ARC_STATUS Status = Vectors->Write(DeviceId, sc_AttrDef, sizeof(sc_AttrDef), &Count);
	if (ARC_SUCCESS(Status) && Count != sizeof(sc_AttrDef)) Status = _EIO;
	if (ARC_FAIL(Status)) return Status;

	// Write the remaining data (all zero) a cluster at a time.
	BYTE Cluster[0x1000] = { 0 };
	for (ULONG i = 0; i < 0x8000 / sizeof(Cluster); i++) {
		Status = Vectors->Write(DeviceId, Cluster, sizeof(Cluster), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(Cluster)) Status = _EIO;
		if (ARC_FAIL(Status)) return Status;
	}
	return Status;
}

static void RppDecodeRle(PBYTE Rle, ULONG Length, PBYTE Decoded, ULONG LengthOut) {
	ULONG itOut = 0;
	for (ULONG i = 0; i < Length && itOut < LengthOut; i++) {
		BYTE Value = Rle[i];
		if (Value != 0xFF) {
			Decoded[itOut] = Value;
			itOut++;
			continue;
		}
		i++;
		Value = Rle[i];
		if (Value == 0xFF) {
			Decoded[itOut] = 0xFF;
			itOut++;
			continue;
		}

		BYTE Length = Value;
		i++;
		Value = Rle[i];
		if ((itOut + Length) > LengthOut) break;
		memset(&Decoded[itOut], Value, Length);
		itOut += Length;
	}
}

static bool RppClusterFitsIn24Bits(int64_t ClusterOffset) {
	int value = 64 - __builtin_clzll(ClusterOffset);
	return value <= 24;
}

static bool RppMftWriteCluster24(PBYTE pMft, ULONG Offset, int64_t ClusterOffset) {
	if (!RppClusterFitsIn24Bits(ClusterOffset)) return false;
	U32LE ClusterValue = { .v = (ULONG)ClusterOffset };
	BYTE LengthSize = pMft[Offset] & 0xF;
	memcpy(&pMft[Offset + LengthSize + 1], (PBYTE)(ULONG)&ClusterValue, 3);
	return true;
}

static bool RppMftWriteBadCluster24(PBYTE pMft, ULONG Offset, int64_t ClusterOffset) {
	if (!RppClusterFitsIn24Bits(ClusterOffset)) return false;
	U32LE ClusterValue = { .v = (ULONG)ClusterOffset };
	memcpy(&pMft[Offset + 1], (PBYTE)(ULONG)&ClusterValue, 3);
	return true;
}

static ARC_STATUS RpFormatNtfs(ULONG DeviceId, PDEVICE_VECTORS Vectors, ULONG StartSector, ULONG SizeMb, ULONG MbrSig) {
	// Formats a partition with size SizeMb as NTFS 1.1

	// This is the most minimialist of NTFS formatters. We hardcode boot sectors and MFTs and AttrDef and UpCase, and patch the offsets/lengths appropriately.
	// Then we write to disk boot sectors, MFT, MFTMirror, LogFile (FF-filled), AttrDef, Bitmap (make sure to calculate this correctly, with correct length!), UpCase.
	// Finally, seek to last sector and write backup boot sector.

	// BUGBUG: this is technically not 100% correct, but NT 4 autochk will recognise and fix errors anyway.

	// Given the MFT compression, this will only work for partitions up to 64GB
	// (all cluster offsets must fit into 24 bits, $BadClus has a "length" of the whole disk)
	if (SizeMb > (0x1000000 / (0x100000 / 0x1000))) {
		return _E2BIG;
	}

	static const BYTE sc_NtfsBoot[] = {
#include "ntfsboot.inc"
	};

	static const BYTE sc_NtfsRootDir[] = {
#include "ntfsroot.inc"
	};

	static const BYTE sc_NtfsMftRle[] = {
#include "ntfsmft.inc"
	};

	// Allocate 16KB from heap for MFT decompression, etc
	PBYTE pMft = (PBYTE)malloc(0x4000);
	if (pMft == NULL) return _ENOMEM;

	// Decompress RLE compressed MFT to allocated buffer
	RppDecodeRle(sc_NtfsMftRle, sizeof(sc_NtfsMftRle), pMft, 0x4000);

	// Allocate space for empty cluster
	BYTE EmptyCluster[0x1000] = { 0 };

	// Allocate space from stack for boot sector and copy from ntfsboot
	BYTE BootSector[0x200];
	memcpy(BootSector, sc_NtfsBoot, sizeof(BootSector));

	// Allocate space from stack for root directory
	BYTE RootDir[0x1000];
	memcpy(RootDir, sc_NtfsRootDir, sizeof(sc_NtfsRootDir));

	enum {
		NTFSBOOT_OFFSET_SIZE = 0x28,
		NTFSBOOT_OFFSET_BACKUP_MFT = 0x38,
		NTFSBOOT_OFFSET_VOLUME_SERIAL = 0x48
	};

	// ntfsboot.inc hardcodes a cluster size of 4KB, that is, 
	int64_t PartitionSizeInSectors = ( ((int64_t)SizeMb) * REPART_MB_SECTORS) - 1;

	{
		LARGE_INTEGER PartitionSizeInSectorsLi = { .QuadPart = PartitionSizeInSectors };
		memcpy(&BootSector[NTFSBOOT_OFFSET_SIZE], (void*)(ULONG)&PartitionSizeInSectorsLi, sizeof(PartitionSizeInSectors));
	}

	// Calculate the offset to the backup MFT in clusters. That is: sector count / (2 * 8), where 8 is number of sectors per cluster.
	int64_t BackupMftCluster = PartitionSizeInSectors / (2 * 8);
	// The partition size in clusters is exactly two times the length of this.
	int64_t PartitionSizeInClusters = BackupMftCluster * 2;

	{
		LARGE_INTEGER BackupMftClusterLi = { .QuadPart = BackupMftCluster };
		memcpy(&BootSector[NTFSBOOT_OFFSET_BACKUP_MFT], (void*)(ULONG)&BackupMftClusterLi, sizeof(BackupMftClusterLi));
	}

	// Calculate the new volume serial.
	// Use the same hashing algorithm to calculate the upper half as NT itself does; but for the low part use the bitwise NOT of the MBR signature.
	{
		LARGE_INTEGER NewVolumeSerial;
		NewVolumeSerial.LowPart = ~MbrSig;

		U32LE volid = { .v = MbrSig };

		PUCHAR pNvs = (PUCHAR)(ULONG)&volid;
		for (ULONG i = 0; i < sizeof(ULONG); i++) {
			volid.v += *pNvs++;
			volid.v = (volid.v >> 2) + (volid .v<< 30);
		}
		NewVolumeSerial.HighPart = volid.v;
		memcpy(&BootSector[NTFSBOOT_OFFSET_VOLUME_SERIAL], (void*)(ULONG)&NewVolumeSerial, sizeof(NewVolumeSerial));
	}

	// Calculate the offsets for each file.
	enum {
		MFT_MFTMIRR_OFFSET = 0x5A8,
		MFT_LOGFILE_OFFSET = 0x9A8,
		MFT_ATTRDEF_OFFSET = 0x11A8,
		MFT_ROOTDIR_INDEX_OFFSET = 0x15E0,
		MFT_BITMAP_DISKLEN_OFFSET = 0x18B8,
		MFT_BITMAP_REALLEN_OFFSET = 0x18C0,
		MFT_BITMAP_CLUSLEN_OFFSET = 0x1978,
		MFT_BITMAP_REALSIZE_OFFSET = 0x1988,
		MFT_BITMAP_FILESIZE_OFFSET = 0x1990,
		MFT_BITMAP_VALIDLEN_OFFSET = 0x1998,
		MFT_BITMAP_OFFSET = 0x19A0,
		MFT_BADCLUS_CLUS64_OFFSET = 0x2198,
		MFT_BADCLUS_OFFSET = 0x21C8,
		MFT_UPCASE_OFFSET = 0x29A0,

		ROOTDIR_DISKLEN_OFFSET = 0x160,
		ROOTDIR_REALLEN_OFFSET = 0x168,
	};

	ARC_STATUS Status = _ESUCCESS;

	// Calculate the number of clusters for $Bitmap.
	// PartitionSizeInClusters / 8.
	int64_t BitmapCountBytes = (PartitionSizeInClusters / 8);
	if ((PartitionSizeInClusters & 7) != 0) BitmapCountBytes++;
	int32_t BitmapCountClusters = (ULONG)(BitmapCountBytes / 0x1000);
	if ((BitmapCountBytes & 0xFFF) != 0) BitmapCountClusters++;
	LARGE_INTEGER BitmapRealSize = { .QuadPart = BitmapCountClusters };
	BitmapRealSize.QuadPart *= 0x1000;
	LARGE_INTEGER BitmapDiskSize = { .QuadPart = BitmapCountBytes };
	do {
		// MftMirr is at BackupMftCluster.
		if (!RppMftWriteCluster24(pMft, MFT_MFTMIRR_OFFSET, BackupMftCluster)) {
			//printf("bad backup: %08x\r\n", (ULONG)BackupMftCluster);
			Status = _E2BIG;
			break;
		}

		int64_t CurrentCluster = BackupMftCluster + 1;

		// LogFile is after BackupMft.
		if (!RppMftWriteCluster24(pMft, MFT_LOGFILE_OFFSET, CurrentCluster)) {
			//printf("bad logfile: %08x\r\n", (ULONG)CurrentCluster);
			Status = _E2BIG;
			break;
		}

		// AttrDef is after LogFile
		CurrentCluster += 1024;
		if (!RppMftWriteCluster24(pMft, MFT_ATTRDEF_OFFSET, CurrentCluster)) {
			//printf("bad attrdef: %08x\r\n", (ULONG)CurrentCluster);
			Status = _E2BIG;
			break;
		}

		// Root directory index is after AttrDef
		CurrentCluster += 9;
		if (!RppMftWriteCluster24(pMft, MFT_ROOTDIR_INDEX_OFFSET, CurrentCluster)) {
			//printf("bad rootdir: %08x\r\n", (ULONG)CurrentCluster);
			Status = _E2BIG;
			break;
		}

		// Bitmap is after root directory index
		CurrentCluster += 1;
		if (!RppMftWriteCluster24(pMft, MFT_BITMAP_OFFSET, CurrentCluster)) {
			//printf("bad bitmap: %08x\r\n", (ULONG)CurrentCluster);
			Status = _E2BIG;
			break;
		}

		// Also copy in the new filesizes for the bitmap file, into the MFT and root directory
		// note: BitmapDiskSize is size, BitmapRealSize is "size on disk" (ie, multiple of clusters)
		memcpy(&pMft[MFT_BITMAP_DISKLEN_OFFSET], (PVOID)(ULONG)&BitmapDiskSize, sizeof(BitmapDiskSize));
		memcpy(&pMft[MFT_BITMAP_REALLEN_OFFSET], (PVOID)(ULONG)&BitmapRealSize, sizeof(BitmapRealSize));
		memcpy(&pMft[MFT_BITMAP_REALSIZE_OFFSET], (PVOID)(ULONG)&BitmapRealSize, sizeof(BitmapRealSize));
		memcpy(&pMft[MFT_BITMAP_FILESIZE_OFFSET], (PVOID)(ULONG)&BitmapDiskSize, sizeof(BitmapDiskSize));
		memcpy(&pMft[MFT_BITMAP_VALIDLEN_OFFSET], (PVOID)(ULONG)&BitmapDiskSize, sizeof(BitmapDiskSize));
		memcpy(&RootDir[ROOTDIR_DISKLEN_OFFSET], (PVOID)(ULONG)&BitmapDiskSize, sizeof(BitmapDiskSize));
		memcpy(&RootDir[ROOTDIR_REALLEN_OFFSET], (PVOID)(ULONG)&BitmapRealSize, sizeof(BitmapRealSize));

		// Need to also specify the new length in MFT for bitmap.
		if (BitmapCountClusters >= 0x100) {
			// This is for a single byte, which implies ~32GB partition limit.
			// Just don't bother supporting this for now as we have 8GB limit on NT partition anyway:
			// To support this, we would have to increase the header byte and move the other bytes away enough space.
			Status = _E2BIG;
			break;
		}
		pMft[MFT_BITMAP_OFFSET + 1] = (BYTE)BitmapCountClusters;
		// BUGBUG: this is u64, but cluster count fits in a byte, that was just checked.
		pMft[MFT_BITMAP_CLUSLEN_OFFSET] = (BYTE)BitmapCountClusters - 1;

		// Bad clusters which specifies the whole disk.
		if (!RppMftWriteBadCluster24(pMft, MFT_BADCLUS_OFFSET, PartitionSizeInClusters + 1)) {
			//printf("bad badclus: %08x\r\n", (ULONG)CurrentCluster);
			Status = _E2BIG;
			break;
		}

		// Also copy in the partition size in clusters to the other place in that MFT entry.
		{
			LARGE_INTEGER PartSizeClus = { .QuadPart = PartitionSizeInClusters };
			memcpy(&pMft[MFT_BADCLUS_CLUS64_OFFSET], (PVOID)(ULONG)&PartSizeClus, sizeof(PartSizeClus));
		}

		// Uppercase table is after bitmap.
		CurrentCluster += BitmapCountClusters;
		if (!RppMftWriteCluster24(pMft, MFT_UPCASE_OFFSET, CurrentCluster)) {
			//printf("bad upcase: %08x\r\n", (ULONG)CurrentCluster);
			Status = _E2BIG;
			break;
		}
	} while (false);

	if (ARC_FAIL(Status)) {
		free(pMft);
		return Status;
	}

	// MFT has been created for this partition.

	// Now we start writing.
	int64_t SectorOffset64 = StartSector;
	SectorOffset64 *= REPART_SECTOR_SIZE;

	do {
		// Seek to the partition start.
		LARGE_INTEGER SectorOffset = { .QuadPart = SectorOffset64 };
		Status = Vectors->Seek(DeviceId, &SectorOffset, SeekAbsolute);
		if (ARC_FAIL(Status)) break;

		// Write the boot sector, followed by the remainder of the boot code
		ULONG Count = 0;
		Status = Vectors->Write(DeviceId, BootSector, sizeof(BootSector), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(BootSector)) Status = _EIO;
		if (ARC_FAIL(Status)) break;
		Status = Vectors->Write(DeviceId, &sc_NtfsBoot[sizeof(BootSector)], sizeof(sc_NtfsBoot) - sizeof(BootSector), &Count);
		if (ARC_SUCCESS(Status) && Count != (sizeof(sc_NtfsBoot) - sizeof(BootSector))) Status = _EIO;
		if (ARC_FAIL(Status)) break;
		Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// Write the used bitmap for the primary MFT. This is 64k clusters (256MB), with the first 16 clusters being used
		EmptyCluster[0] = EmptyCluster[1] = 0xFF;
		Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
		if (ARC_FAIL(Status)) break;
		EmptyCluster[0] = EmptyCluster[1] = 0x00;
		Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
		if (ARC_FAIL(Status)) break;
		
		// Write the primary MFT.
		Status = Vectors->Write(DeviceId, pMft, 0x4000, &Count);
		if (ARC_SUCCESS(Status) && Count != 0x4000) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// Seek to the backup MFT location.
		SectorOffset.QuadPart += (BackupMftCluster * 0x1000);
		Status = Vectors->Seek(DeviceId, &SectorOffset, SeekAbsolute);
		if (ARC_FAIL(Status)) break;

		// Write the backup MFT, which is only the first cluster of the primary MFT.
		Status = Vectors->Write(DeviceId, pMft, 0x1000, &Count);
		if (ARC_SUCCESS(Status) && Count != 0x1000) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// Next up is logfile which is 4MB FF-filled
		memset(EmptyCluster, 0xFF, sizeof(EmptyCluster));
		for (ULONG i = 0; i < 0x400000 / 0x1000; i++) {
			Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
			if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
			if (ARC_FAIL(Status)) break;
		}
		memset(EmptyCluster, 0, sizeof(EmptyCluster));

		// Next up is AttrDef
		Status = RppWriteAttrDef(DeviceId, Vectors);
		if (ARC_FAIL(Status)) break;

		// Next up is root directory index
		Status = Vectors->Write(DeviceId, RootDir, sizeof(RootDir), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(RootDir)) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// Next up is bitmap.
		// This bitmap starts at the MFT cluster (cluster 4)
		// One bit set per used cluster.

		{
			// First, write the initial bitmap cluster. This represents 32768 clusters.
			// Cluster 0 "unused" (really bootdata, nothing will write to that), then next 7 clusters are used by the MFT so set that.
			EmptyCluster[0] = 0xF7;
			Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
			if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
			if (ARC_FAIL(Status)) break;
			EmptyCluster[0] = 0;

			// Now we need to loop until we hit BackupMftCluster.
			ULONG BitmapCurrentCluster = (sizeof(EmptyCluster) * 8);
			ULONG Offset = ((ULONG)BackupMftCluster) - BitmapCurrentCluster;
			while (Offset > (sizeof(EmptyCluster) * 8)) {

				Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
				if (ARC_FAIL(Status)) break;

				BitmapCurrentCluster += (sizeof(EmptyCluster) * 8);
				Offset = ((ULONG)BackupMftCluster) - BitmapCurrentCluster;
			}

			if (ARC_FAIL(Status)) break;

			// At some point in the following cluster, is the BackupMftCluster.
			// Calculate the total used clusters.
			ULONG UsedClusters = 1 + 1024 + 9 + 1 + BitmapCountClusters + 32;

			// Find the bit offset inside EmptyCluster to start setting bits.
			ULONG ByteOffset = Offset / 8;
			ULONG BitOffset = Offset & 7;

			// Start setting bits.
			while (UsedClusters != 0) {
				while (UsedClusters != 0 && ByteOffset < sizeof(EmptyCluster)) {
					EmptyCluster[ByteOffset] |= (1 << BitOffset);

					BitOffset++;
					if (BitOffset == 8) {
						BitOffset = 0;
						ByteOffset++;
					}
					UsedClusters--;
				}
				// Set as many bits as possible in this loop.
				// Write this cluster to disk.
				Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
				if (ARC_FAIL(Status)) break;
				// And set back to zero.
				memset(EmptyCluster, 0, sizeof(EmptyCluster));
				BitmapCurrentCluster += (sizeof(EmptyCluster) * 8);
				ByteOffset = 0;
				BitOffset = 0;
			}

			if (ARC_FAIL(Status)) break;

			// Written out the used clusters that have been used halfway through the disk.
			// Now loop until we hit PartitionSizeInClusters.
			Offset = ((ULONG)PartitionSizeInClusters) - BitmapCurrentCluster;
			while (Offset > (sizeof(EmptyCluster) * 8)) {

				Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
				if (ARC_FAIL(Status)) break;

				BitmapCurrentCluster += (sizeof(EmptyCluster) * 8);
				Offset = ((ULONG)PartitionSizeInClusters) - BitmapCurrentCluster;
			}

			// Reached the final cluster.
			// Find the bit offset inside EmptyCluster to start setting bits.
			ByteOffset = Offset / 8;
			BitOffset = Offset & 7;

			// Every bit starting from this offset needs to be set.
			if (BitOffset != 0) {
				while (BitOffset < 8) {
					EmptyCluster[ByteOffset] |= (1 << BitOffset);
					BitOffset++;
				}
				ByteOffset++;
			}
			// And set the remainder of the bits in the bitmap.
			memset(&EmptyCluster[ByteOffset], 0xFF, sizeof(EmptyCluster) - ByteOffset);
			// And write out the final cluster.
			Status = Vectors->Write(DeviceId, EmptyCluster, sizeof(EmptyCluster), &Count);
			if (ARC_SUCCESS(Status) && Count != sizeof(EmptyCluster)) Status = _EIO;
			if (ARC_FAIL(Status)) break;
			// And set back to zero.
			memset(EmptyCluster, 0, sizeof(EmptyCluster));
		}

		// Write the uppercase table.
		Status = RppWriteUpCaseTable(DeviceId, Vectors);
		if (ARC_FAIL(Status)) break;

		// Seek to the final sector of this partition.
		SectorOffset.QuadPart = SectorOffset64;
		SectorOffset.QuadPart += ((int64_t)SizeMb * 0x100000) - REPART_SECTOR_SIZE;
		Status = Vectors->Seek(DeviceId, &SectorOffset, SeekAbsolute);
		if (ARC_FAIL(Status)) break;

		// Write the backup boot sector there.
		Status = Vectors->Write(DeviceId, BootSector, sizeof(BootSector), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(BootSector)) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// All done!
	} while (false);

	free(pMft);
	return Status;
}

static ULONG RepartGetFileSize(PVENDOR_VECTOR_TABLE Api, ULONG FileId) {
	FILE_INFORMATION Information;
	ULONG Length = 0;
	if (ARC_SUCCESS(Api->GetFileInformationRoutine(FileId, &Information))) {
		if (Information.EndingAddress.HighPart == 0) {
			return Information.EndingAddress.LowPart;
		}
	}

	Api->CloseRoutine(FileId);
	return Length;
}

#define REPART_INIT_FILE(var) \
	ULONG h##var = 0;\
	ULONG len##var = 0;\
	do {\
		U32LE _h##var; \
		Status = Api->OpenRoutine(s_BootPath, ArcOpenReadOnly, &_h##var);\
		if (ARC_FAIL(Status)) break;\
		h##var = _h##var.v; \
		len##var = RepartGetFileSize(Api, h##var); \
	} while (0)

#define REPART_CHECK_FILE(var, _max) REPART_INIT_FILE(var); Api->CloseRoutine(h##var); if (len##var == 0 || len##var > _max) return _EFAULT
#define REPART_CHECK_FILE_EXACT(var, size) REPART_INIT_FILE(var); Api->CloseRoutine(h##var); if (len##var != size) return _EFAULT

/// <summary>
/// Check if the files required to repartition a disk exists on a source device
/// </summary>
/// <param name="SourceDevice">ARC path source device.</param>
/// <returns>ARC status code.</returns>
ARC_STATUS ArcFsRepartFilesOnDisk(const char* SourceDevice) {
	if (SourceDevice == NULL) return _EINVAL;

	char s_BootPath[1024];
	ULONG BootPathIdx = snprintf(s_BootPath, sizeof(s_BootPath), "%s\\", SourceDevice);

	ARC_STATUS Status = _ESUCCESS;
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

	// stage1
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "stage1.elf");
	REPART_CHECK_FILE(Stage1, REPART_STAGE1_MAX);
	
	// stage2
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "stage2.elf");
	REPART_CHECK_FILE(Stage2, REPART_STAGE2_MAX);

	// bootimg
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "boot.img");
	REPART_CHECK_FILE_EXACT(Bootimg, REPART_BOOTIMG_SIZE);

	// driver ptDR
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "Drivers\\Apple_Driver_ATA.ptDR.drvr");
	REPART_CHECK_FILE(DrvptDR, REPART_DRIVER_MAX);

	// driver wiki
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "Drivers\\Apple_Driver_ATA.wiki.drvr");
	REPART_CHECK_FILE(Drvwiki, REPART_DRIVER_MAX);

	return _ESUCCESS;
}

/// <summary>
/// Repartitions a disk, writing APM and MBR partition tables for Mac and NT operating systems.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <param name="SourceDevice">Source device path where files get copied from</param>
/// <param name="NtPartMb">Disk space in MB for the NT partition</param>
/// <param name="MacPartsMb">Array of disk space in MB for the Mac partitions</param>
/// <param name="CountMacParts">Number of Mac partitions</param>
/// <param name="DataWritten">If failed, will be set to true after data has been written to disk.</param>
/// <returns>ARC status code.</returns>
ARC_STATUS ArcFsRepartitionDisk(ULONG DeviceId, const char* SourceDevice, ULONG NtPartMb, PULONG MacPartsMb, ULONG CountMacParts, bool* DataWritten) {
	// This does repartition the disk for NT.
	// The disk is split up into an NT part and a Mac part.
	// DataWritten is set to true when writing to disk starts.
	// After partitioning, the disk will look like this:
	// 32KB partition tables
	// 1 sector (512 bytes) fake-ISO (to ensure OSX 10.4+ does not use the MBR) and MBR backup
	// 256KB HFS partition for booting - this is enough for 16KB stage1 and 224KB stage2
	// 64KB OS9 driver partition 1 (empty, zero out first sector to ensure this)
	// 64KB OS9 driver partition 2 (empty, zero out first sector to ensure this)
	// Nothing up to 1MB
	// 32MB ARC system partition (probably overkill but whatever)
	// "NtPartMb"MB empty space (will zero out first sector to ensure that)
	// For CountMacParts partitions:
	// "MacPartsMb[i]"MB marked as HFS (zero out sector 2 and sector (CountSectors-2) to ensure it being considered empty)

	// That's the main high-level structure of the disk
	// The partition tables are as follows:
	// APM partition tables: - all dummy partitions are of type "CD_partition_scheme" so OSX disk utility doesn't show them
	// Partition 1 - partition tables themselves, as usual for APM
	// Partition 2 - apple_void single sector, this is the fake-ISO and MBR backup. "CD001" at offset 1, and MBR backup at end of sector
	// Partition 3 - OS9 driver partition 1 (ptDR) (64KB)
	// Partition 4 - OS9 driver partition 2 (wiki) (64KB)
	// Partition 5 - OS9 patch partition (256KB)
	// Partition 6 - Apple_Boot HFS boot partition (256KB)
	// Partition 7 - dummy partition, this covers up to 1MB + 32MB ARC system partition + "NtPartMb" MB NT OS partition
	// Partition 8+ - user-specified Mac partitions.
	// MBR partition table:
	// Partition 1 - type 0x41, start 32KB, up to 1MB
	// Partition 2 - FAT16, OS partition, start 33MB, size "NtPartMb"MB
	// Partition 3 - FAT16, ARC system partition, start after OS partition, size 32MB
	// Partition 4 - type 0xEE, active byte 0x7F, covers remainder of disk or 8GB, whichever comes first
	// The first partition won't be touched by NT, the last partition is defined in such a way that modern OSes (linux, BSDs...) will ignore the MBR entirely
	// We have to have the OS partition before the ARC system partition, otherwise ARC system partition becomes C: and pagefile gets put there.

	if (DataWritten == NULL || SourceDevice == NULL) return _EINVAL;
	*DataWritten = false;
	// If number of APM partitions is over the maximum allowed, do nothing
	if ((CountMacParts + REPART_APM_MINIMUM_PARTITIONS) > REPART_APM_MAXIMUM_PARTITIONS) return _E2BIG;
	// If the NT partition is over the maximum allowed size, do nothing
	if (NtPartMb > REPART_MAX_NT_PART_IN_MB) return _E2BIG;

	// Get the device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;
	// Can't be a file.
	if (Device->DeviceId != FILE_IS_RAW_DEVICE) return _EBADF;
	// Sector size must be 512 bytes. atapi.sys expects this anyway!
	if (Device->GetSectorSize == NULL) return _EBADF;
	ULONG SectorSize;
	if (ARC_FAIL(Device->GetSectorSize(DeviceId, &SectorSize))) return _EBADF;
	if (SectorSize != REPART_SECTOR_SIZE) return _EBADF;
	PDEVICE_VECTORS Vectors = Device->DeviceEntryTable;
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

	// Size of disk must be able to fit the partition table that we're attempting to write out
	ULONG TotalSizeMb = 1 // MBR partition 1, APM partitions 1-5 + start of 6
		+ 32 // MBR partition 2, next part of APM partition 6
		+ NtPartMb // MBR partition 3, remainder of APM partition 6
		;

	// MBR partition 4; APM partitions 7 and beyond
	for (ULONG i = 0; i < CountMacParts; i++) {
		// no single partition can be beyond 2TB
		if (MacPartsMb[i] > REPART_U32_MAX_SECTORS_IN_MB) return _E2BIG;
		TotalSizeMb += MacPartsMb[i];
	}
	
	// Calculate the disk size in MB, if the total partitions size is greater than that, error.
	FILE_INFORMATION FileInfo;
	if (ARC_FAIL(Vectors->GetFileInformation(DeviceId, &FileInfo))) return _EBADF;
	ULONG DiskSizeMb = (ULONG)(FileInfo.EndingAddress.QuadPart / 0x100000);
	if (TotalSizeMb > DiskSizeMb) return _E2BIG;

	// Calculate the path to the files we need and open them and get their file sizes
	char s_BootPath[1024];
	ULONG BootPathIdx = snprintf(s_BootPath, sizeof(s_BootPath), "%s\\", SourceDevice);

	printf("Loading files...\r\n");
	ARC_STATUS Status = _ESUCCESS;
	// stage1
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "stage1.elf");
	REPART_INIT_FILE(Stage1);

	if (lenStage1 == 0) {
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenStage1 > REPART_STAGE1_MAX) {
		Api->CloseRoutine(hStage1);
		printf("stage1.elf is too big, size=%dKB, maximum=%dKB\r\n", lenStage1 / 1024, REPART_STAGE1_MAX / 1024);
		return _EFAULT;
	}

	// stage2
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "stage2.elf");
	REPART_INIT_FILE(Stage2);
	if (lenStage2 == 0) {
		Api->CloseRoutine(hStage1);
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenStage2 > REPART_STAGE2_MAX) {
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("stage2.elf is too big, size=%dKB, maximum=%dKB\r\n", lenStage2 / 1024, REPART_STAGE2_MAX / 1024);
		return _EFAULT;
	}

	// bootimg
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "boot.img");
	REPART_INIT_FILE(BootImg);
	if (lenBootImg == 0) {
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenBootImg != REPART_BOOTIMG_SIZE) {
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("boot.img size incorrect, size=%dKB, expected=%dKB\r\n", lenBootImg / 1024, REPART_BOOTIMG_SIZE / 1024);
		return _EFAULT;
	}

	// driver ptDR
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "Drivers\\Apple_Driver_ATA.ptDR.drvr");
	REPART_INIT_FILE(DrvptDR);
	if (lenDrvptDR == 0) {
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenDrvptDR > REPART_DRIVER_MAX) {
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Apple_Driver_ATA.ptDR is too big, size=%dKB, maximum=%dKB\r\n", lenDrvptDR / 1024, REPART_DRIVER_MAX / 1024);
		return _EFAULT;
	}

	// driver wiki
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "Drivers\\Apple_Driver_ATA.wiki.drvr");
	REPART_INIT_FILE(Drvwiki);
	if (lenDrvwiki == 0) {
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenDrvwiki > REPART_DRIVER_MAX) {
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Apple_Driver_ATA.wiki is too big, size=%dKB, maximum=%dKB\r\n", lenDrvwiki / 1024, REPART_DRIVER_MAX / 1024);
		return _EFAULT;
	}

	// Only need to read one at a time, so calculate which of the files is the largest
	ULONG lenMax = lenStage1;
	if (lenStage2 > lenMax) lenMax = lenStage2;
	if (lenBootImg > lenMax) lenMax = lenBootImg;
	if (lenDrvptDR > lenMax) lenMax = lenDrvptDR;
	if (lenDrvwiki > lenMax) lenMax = lenDrvwiki;

	// For the MBR disk signature, use CRC32 of TIME_FIELDS structure. Accurate to the second, but that's the best platform-independent entropy source we have right now...
	PTIME_FIELDS Time = Api->GetTimeRoutine();

	// Find some free memory that is at least the length we need.
	ULONG PageLen = lenMax / PAGE_SIZE;
	if ((lenMax & (PAGE_SIZE - 1)) != 0) PageLen++;
	PVOID Buffer = NULL;
	{
		for (PMEMORY_DESCRIPTOR MemDesc = Api->MemoryRoutine(NULL); MemDesc != NULL; MemDesc = Api->MemoryRoutine(MemDesc)) {
			if (MemDesc->MemoryType != MemoryFree) continue;
			if (MemDesc->PageCount < PageLen) continue;
			Buffer = (PVOID)((MemDesc->BasePage * PAGE_SIZE) | 0x80000000);
			break;
		}
	}

	if (Buffer == NULL) {
		Api->CloseRoutine(hDrvwiki);
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not find a free memory chunk of at least %dKB\r\n", (PageLen * PAGE_SIZE) / 1024);
		return _ENOMEM;
	}

	// Ensure boot.img looks good
	printf("Reading boot.img...\r\n");
	// Read bootimg
	LARGE_INTEGER FileSeekZero = INT32_TO_LARGE_INTEGER(0);
	U32LE CountLe;
	ULONG Count;
	Status = Api->ReadRoutine(hBootImg, Buffer, lenBootImg, &CountLe);
	if (ARC_SUCCESS(Status)) {
		if (CountLe.v != lenBootImg) Status = _EIO;
		else Status = Api->SeekRoutine(hBootImg, &FileSeekZero, SeekAbsolute);
	}
	if (ARC_FAIL(Status)) {
		Api->CloseRoutine(hDrvwiki);
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not read boot.img\r\n");
		if (ARC_SUCCESS(Status)) Status = _EBADF;

		return Status;
	}
	// Find where stage1 and stage2 goes
	PBYTE BufferStage1 = mem_mem(Buffer, "*STAGE1*", lenBootImg, sizeof("*STAGE1*") - 1);
	PBYTE BufferStage2 = mem_mem(Buffer, "*STAGE2*", lenBootImg, sizeof("*STAGE2*") - 1);

	if (BufferStage1 == NULL || BufferStage2 == NULL) {
		Api->CloseRoutine(hDrvwiki);
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not read boot.img\r\n");
		return _EBADF;
	}

	// Initialise the MBR sector.
	printf("Initialising partition tables...\r\n");
	MBR_SECTOR Mbr = { 0 };
	// DDT (except drivers, fill that in later)
	Mbr.ApmDdt.Signature = DDT_VALID_SIGNATURE;
	Mbr.ApmDdt.SectorSize = REPART_SECTOR_SIZE;
	Mbr.ApmDdt.SectorCount = (FileInfo.EndingAddress.QuadPart / REPART_SECTOR_SIZE);
	if (Mbr.ApmDdt.SectorCount == 0) {
		Api->CloseRoutine(hDrvwiki);
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not obtain sector count for disk\r\n");
		return _EBADF; // we need to know size of disk here
	}
	Mbr.ApmDdt.DriverCount = 2;

	// MBR itself
	bool NtPartitionIsExtended = (NtPartMb > 2048); // if NT partition is over 2GB in size, format as NTFS
	Mbr.ValidMbr = MBR_VALID_SIGNATURE;
	Mbr.Partitions[0].Type = 0x41;
	Mbr.Partitions[0].SectorStart = REPART_APM_SECTORS;
	Mbr.Partitions[0].SectorCount = REPART_MBR_PART1_SIZE;
	Mbr.Partitions[1].Type = NtPartitionIsExtended ? 0x07 : 0x06;
	Mbr.Partitions[1].SectorStart = REPART_MBR_PART2_START;
	Mbr.Partitions[1].SectorCount = NtPartMb * REPART_MB_SECTORS;
	Mbr.Partitions[2].Type = 0x06;
	Mbr.Partitions[2].SectorStart = Mbr.Partitions[1].SectorStart + Mbr.Partitions[1].SectorCount;
	Mbr.Partitions[2].SectorCount = REPART_MBR_PART3_SIZE;
	Mbr.Partitions[3].Type = 0xEE;
	Mbr.Partitions[3].Active = 0x7F;
	Mbr.Partitions[3].SectorStart = Mbr.Partitions[2].SectorStart + REPART_MBR_PART3_SIZE;
	Mbr.Partitions[3].SectorCount = Mbr.ApmDdt.SectorCount - Mbr.Partitions[3].SectorStart;
	if (Mbr.ApmDdt.SectorCount > REPART_MBR_CHS_LIMIT)
		Mbr.Partitions[3].SectorCount = REPART_MBR_CHS_LIMIT - Mbr.Partitions[3].SectorStart;
	ULONG MbrSignature = Crc32(Time, sizeof(*Time));
	// Disallow an all-zero MBR signature.
	if (MbrSignature == 0) MbrSignature = 0xFFFFFFFFul;
	Mbr.Signature = MbrSignature;
	ULONG ArcSystemPartitionSectorOffset = (NtPartMb * REPART_MB_SECTORS) + REPART_MBR_PART2_START;

	// Allocate heap space for the APM partitions (maximum : 32KB)
	ULONG ApmPartitionsCount = CountMacParts + REPART_APM_MINIMUM_PARTITIONS;
	if (CountMacParts != 0) ApmPartitionsCount++;
	PAPM_SECTOR Apm = (PAPM_SECTOR)malloc(sizeof(APM_SECTOR) * REPART_APM_MAXIMUM_PARTITIONS);
	if (Apm == NULL) {
		Api->CloseRoutine(hDrvwiki);
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not allocate memory for APM\r\n");
		return _ENOMEM;
	}
	memset(Apm, 0, sizeof(APM_SECTOR) * REPART_APM_MAXIMUM_PARTITIONS);

	// Allocate heap space for laying out the FAT FS for the ARC system partition
	enum {
		SIZE_OF_SYS_PART_FAT_FS = 0x14200
	};
	PUCHAR SysPartFatFs = (PUCHAR)malloc(SIZE_OF_SYS_PART_FAT_FS);
	if (SysPartFatFs == NULL) {
		free(Apm);
		Api->CloseRoutine(hDrvwiki);
		Api->CloseRoutine(hDrvptDR);
		Api->CloseRoutine(hBootImg);
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not allocate memory for FAT filesystem\r\n");
		return _ENOMEM;
	}
	memset(SysPartFatFs, 0, SIZE_OF_SYS_PART_FAT_FS);


	Status = _ESUCCESS;
	do {
		// Partition 1: partition tables themselves
		ApmpInit(&Apm[0], ApmPartitionsCount, REPART_APM_PART1_START, REPART_APM_PART1_SIZE, false);
		strncpy(Apm[0].Name, "Apple", sizeof(Apm[0].Name));
		strncpy(Apm[0].Type, "Apple_partition_map", sizeof(Apm[0].Type));
		Apm[0].Status = 3;

		// Partition 2: dummy partition single sector
		ApmpInit(&Apm[1], ApmPartitionsCount, REPART_APM_PART2_START, REPART_APM_PART2_SIZE, true);
		strncpy(Apm[1].Name, "Extra", sizeof(Apm[1].Name));
		strncpy(Apm[1].Type, "CD_partition_scheme", sizeof(Apm[1].Type));
		Apm[1].Status = 0;

		// Partition 3: Apple_Driver_ATA ptDR
		ApmpInit(&Apm[2], ApmPartitionsCount, REPART_APM_PART3_START, REPART_APM_PART3_SIZE, true);
		strncpy(Apm[2].Name, "Macintosh", sizeof(Apm[2].Name));
		strncpy(Apm[2].Type, "Apple_Driver_ATA", sizeof(Apm[2].Type));
		strncpy(Apm[2].BootCodeArchitecture, "68000", sizeof(Apm[2].BootCodeArchitecture));
		memcpy(Apm[2].UnusedData, "ptDR", sizeof("ptDR"));
		Apm[2].LengthBoot = lenDrvptDR;
		Apm[2].Status |= 0x400;

		// Read the whole ptDR driver into buffer
		printf("Reading Apple_Driver_ATA.ptDR...\r\n");
		Status = Api->ReadRoutine(hDrvptDR, Buffer, lenDrvptDR, &CountLe);
		if (ARC_SUCCESS(Status)) {
			if (CountLe.v != lenDrvptDR) Status = _EBADF;
			else Status = Api->SeekRoutine(hDrvptDR, &FileSeekZero, SeekAbsolute);
		}
		if (ARC_FAIL(Status)) {
			printf("Could not read Apple_Driver_ATA.ptDR\r\n");
			break;
		}

		// Fill in the checksum
		Apm[2].BootCodeChecksum = ApmpDriverChecksum(Buffer, lenDrvptDR);
		// Fill in the DDT driver info for this driver. sector start = partition start, sector count = LengthBoot in sectors
		Mbr.ApmDdt.Drivers[0].SectorStart = REPART_APM_PART3_START;
		Mbr.ApmDdt.Drivers[0].SectorCount = lenDrvptDR / REPART_SECTOR_SIZE;
		if ((lenDrvptDR & (REPART_SECTOR_SIZE - 1)) != 0) Mbr.ApmDdt.Drivers[0].SectorCount++;
		//Mbr.ApmDdt.Drivers[0].OsType = 0x0701;

		// Partition 4: Apple_Driver_ATA wiki
		ApmpInit(&Apm[3], ApmPartitionsCount, REPART_APM_PART4_START, REPART_APM_PART4_SIZE, true);
		strncpy(Apm[3].Name, "Macintosh", sizeof(Apm[3].Name));
		strncpy(Apm[3].Type, "Apple_Driver_ATA", sizeof(Apm[3].Type));
		strncpy(Apm[3].BootCodeArchitecture, "68000", sizeof(Apm[3].BootCodeArchitecture));
		memcpy(Apm[3].UnusedData, "wiki", sizeof("wiki"));
		Apm[3].LengthBoot = lenDrvwiki;

		// Read the whole wiki driver into buffer
		printf("Reading Apple_Driver_ATA.wiki...\r\n");
		Status = Api->ReadRoutine(hDrvwiki, Buffer, lenDrvwiki, &CountLe);
		if (ARC_SUCCESS(Status)) {
			if (CountLe.v != lenDrvwiki) Status = _EBADF;
			else Status = Api->SeekRoutine(hDrvwiki, &FileSeekZero, SeekAbsolute);
		}
		if (ARC_FAIL(Status)) {
			printf("Could not read Apple_Driver_ATA.wiki\r\n");
			break;
		}

		// Fill in the checksum
		Apm[3].BootCodeChecksum = ApmpDriverChecksum(Buffer, lenDrvwiki);
		// Fill in the DDT driver info for this driver. sector start = partition start, sector count = LengthBoot in sectors
		Mbr.ApmDdt.Drivers[1].SectorStart = REPART_APM_PART4_START;
		Mbr.ApmDdt.Drivers[1].SectorCount = lenDrvwiki / REPART_SECTOR_SIZE;
		if ((lenDrvwiki & (REPART_SECTOR_SIZE - 1)) != 0) Mbr.ApmDdt.Drivers[1].SectorCount++;
		//Mbr.ApmDdt.Drivers[1].OsType = 0xF8FF;

		// Partition 5: apple patch partition
		ApmpInit(&Apm[4], ApmPartitionsCount, REPART_APM_PART5_START, REPART_APM_PART5_SIZE, false);
		strncpy(Apm[4].Name, "Patch Partition", sizeof(Apm[4].Name));
		strncpy(Apm[4].Type, "Apple_Patches", sizeof(Apm[4].Type));
		Apm[4].Status = 1;

		// Partition 6: boot partition
		ApmpInit(&Apm[5], ApmPartitionsCount, REPART_APM_PART6_START, REPART_APM_PART6_SIZE, false);
		strncpy(Apm[5].Name, "Windows NT", sizeof(Apm[5].Name));
		strncpy(Apm[5].Type, "Apple_Boot", sizeof(Apm[5].Type));

		// Partition 7: dummy partition for covering up to the end of the OS partition in MBR for NT
		// Calculate the length for this
		ULONG Part7SectorSize = (ArcSystemPartitionSectorOffset + REPART_MBR_PART3_SIZE - REPART_APM_PART7_START);
		ApmpInit(&Apm[6], ApmPartitionsCount, REPART_APM_PART7_START, Part7SectorSize, false);
		strncpy(Apm[6].Name, "Extra", sizeof(Apm[6].Name));
		strncpy(Apm[6].Type, "CD_partition_scheme", sizeof(Apm[6].Type));
		Apm[6].Status = 0;

		// Partition 8 and above: the Mac partitions
		ULONG MacPartStart = REPART_APM_PART7_START + Part7SectorSize;
		for (ULONG i = 0; i < CountMacParts; i++) {
			ULONG MacPartSectors = MacPartsMb[i] * REPART_MB_SECTORS;

			// For the last partition, we need to leave 4 sectors free at the end of the disk for some reason.
			// Obviously only if we create mac partitions.
			if (i == (CountMacParts - 1)) {
				ULONG RemainingSpace = (DiskSizeMb * REPART_MB_SECTORS) - (MacPartStart + MacPartSectors);
				if (RemainingSpace == 0) {
					MacPartSectors -= 4;
				}
			}

			ApmpInit(&Apm[7 + i], ApmPartitionsCount, MacPartStart, MacPartSectors, false);
			strncpy(Apm[7 + i].Type, "Apple_HFS", sizeof(Apm[7 + i].Type)); // this type will allow OSX Disk Utility to format the Mac partitions
			static const char s_MacPartName[] = "Mac Partition ";
			strncpy(Apm[7 + i].Name, s_MacPartName, sizeof(Apm[7 + i].Name));
			ULONG CountIndex = sizeof(s_MacPartName) - 1;
			ULONG IndexTens = (i / 10);
			Apm[7 + i].Name[CountIndex + 0] = IndexTens + (IndexTens >= 10 ? ('A' - 10) : '0');
			Apm[7 + i].Name[CountIndex + 1] = (i % 10) + '0';
			Apm[7 + i].Name[CountIndex + 2] = 0;

			Apm[7 + i].Status = 0x40000033;

			// calculate the start of the next partition
			MacPartStart += MacPartSectors;
		}

		// Final partition: free space
		ULONG EmptySpace = (DiskSizeMb * REPART_MB_SECTORS) - MacPartStart;
		if (EmptySpace > 0) {
			ApmpInit(&Apm[7 + CountMacParts], ApmPartitionsCount, MacPartStart, EmptySpace, false);
			strncpy(Apm[7 + CountMacParts].Name, "Extra", sizeof(Apm[7 + CountMacParts].Name));
			strncpy(Apm[7 + CountMacParts].Type, "Apple_Free", sizeof(Apm[7 + CountMacParts].Type));
			Apm[7 + CountMacParts].Status = 0;
		}

		// All partition tables have now been computed in memory.
		// Write everything to disk.
		// This is where overwriting existing data on the disk starts!
		LARGE_INTEGER SeekOffset = INT32_TO_LARGE_INTEGER(0);
		Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
		if (ARC_FAIL(Status)) break;
		// Seek successful so perform the write. Print progress now (after seek), as we are about to start committing to writing this disk layout out:
		printf("Writing partition tables...\r\n");
		*DataWritten = true;
		// MBR first
		Count = 0;
		Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
		if (ARC_FAIL(Status) || Count != sizeof(Mbr)) {
			printf("Could not write DDT and MBR partition table\r\n");
			if (ARC_SUCCESS(Status)) Status = _EIO;
			break;
		}
		// then APM
		Count = 0;
		Status = Vectors->Write(DeviceId, Apm, sizeof(APM_SECTOR) * REPART_APM_MAXIMUM_PARTITIONS, &Count);
		if (ARC_FAIL(Status) || Count != sizeof(APM_SECTOR) * REPART_APM_MAXIMUM_PARTITIONS) {
			printf("Could not write APM partition table\r\n");
			if (ARC_SUCCESS(Status)) Status = _EIO;
			break;
		}
		// Current position: backup MBR, so calculate that:
		// Wipe the DDT out of the in-memory MBR
		memset(Mbr.MbrCode, 0, sizeof(Mbr.MbrCode));
		// and write the ISO magic at offset 1
		memcpy(&Mbr.MbrCode[1], "CD001", sizeof("CD001") - 1);
		// and write this backup MBR to the sector:
		Count = 0;
		Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
		if (ARC_FAIL(Status) || Count != sizeof(Mbr)) {
			printf("Could not write backup MBR partition table\r\n");
			if (ARC_SUCCESS(Status)) Status = _EIO;
			break;
		}

		// Current position: partition 3 (Apple_Driver_ATA ptDR)
		printf("Writing Apple_Driver_ATA.ptDR...\r\n");
		// Read the file again and ensure checksum matches
		Status = Api->ReadRoutine(hDrvptDR, Buffer, lenDrvptDR, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenDrvptDR || Apm[2].BootCodeChecksum != ApmpDriverChecksum(Buffer, lenDrvptDR)) {
			printf("Could not read Apple_Driver_ATA.ptDR\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}
		// Write to disk
		Count = 0;
		Status = Vectors->Write(DeviceId, Buffer, lenDrvptDR, &Count);
		if (ARC_FAIL(Status) || Count != lenDrvptDR) {
			printf("Could not write Apple_Driver_ATA.ptDR\r\n");
			if (ARC_SUCCESS(Status)) Status = _EIO;
			break;
		}

		// Current position: probably somewhere inside partition 3
		printf("Writing Apple_Driver_ATA.wiki...\r\n");

		// Read the file again and ensure checksum matches
		Status = Api->ReadRoutine(hDrvwiki, Buffer, lenDrvwiki, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenDrvwiki || Apm[3].BootCodeChecksum != ApmpDriverChecksum(Buffer, lenDrvwiki)) {
			printf("Could not read Apple_Driver_ATA.wiki\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}

		// Seek to partition 4
		SeekOffset = INT32_TO_LARGE_INTEGER(REPART_APM_PART4_START * REPART_SECTOR_SIZE);
		Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
		if (ARC_SUCCESS(Status)) {
			// Write to disk
			Count = 0;
			Status = Vectors->Write(DeviceId, Buffer, lenDrvwiki, &Count);
			if (ARC_SUCCESS(Status) && Count != lenDrvwiki) Status = _EIO;
		}
		if (ARC_FAIL(Status)) {
			printf("Could not write Apple_Driver_ATA.wiki\r\n");
			break;
		}

		// Seek to partition 5
		SeekOffset = INT32_TO_LARGE_INTEGER(REPART_APM_PART5_START * REPART_SECTOR_SIZE);
		Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
		if (ARC_SUCCESS(Status)) {
			// Write to disk
			static const UCHAR sc_ApplePatches[] = {
#include "applepatch.inc"
			};
			memset(&Mbr, 0, sizeof(Mbr));
			memcpy(&Mbr, sc_ApplePatches, sizeof(sc_ApplePatches));

			Count = 0;
			Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
			if (ARC_SUCCESS(Status) && Count != sizeof(Mbr)) Status = _EIO;
		}
		if (ARC_FAIL(Status)) {
			printf("Could not write Apple_Patches\r\n");
			break;
		}


		// Seek to partition 6 (HFS boot partition)
		SeekOffset = INT32_TO_LARGE_INTEGER(REPART_APM_PART6_START * REPART_SECTOR_SIZE);
		Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
		if (ARC_FAIL(Status)) {
			printf("Could not seek to boot partition\r\n");
			break;
		}
		printf("Writing boot partition...\r\n");
		// Read bootimg
		Status = Api->ReadRoutine(hBootImg, Buffer, lenBootImg, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenBootImg) {
			printf("Could not read boot.img\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}
		// Read stage1 into the location calculated previously
		Status = Api->ReadRoutine(hStage1, BufferStage1, lenStage1, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenStage1) {
			printf("Could not read stage1.elf\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}
		// Read stage2 into the location calculated previously
		Status = Api->ReadRoutine(hStage2, BufferStage2, lenStage2, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenStage2) {
			printf("Could not read stage2.elf\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}
		// Everything has been read into place, write to disk.
		Count = 0;
		Status = Vectors->Write(DeviceId, Buffer, lenBootImg, &Count);
		if (ARC_FAIL(Status) || Count != lenBootImg) {
			printf("Could not write boot partition\r\n");
			if (ARC_SUCCESS(Status)) Status = _EIO;
			break;
		}

		// Seek to partition 7 and format the ARC NV area
		printf("Formatting ARC non-volatile storage...\r\n");
		SeekOffset = INT32_TO_LARGE_INTEGER(REPART_APM_PART7_START * REPART_SECTOR_SIZE);
		Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
		if (ARC_SUCCESS(Status)) {
			// Write to disk
			memset(&Mbr, 0, sizeof(Mbr));
			for (ULONG Sector = 0; ARC_SUCCESS(Status) && Sector < 2; Sector++) {
				Count = 0;
				Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(Mbr)) Status = _EIO;
			}
		}
		if (ARC_FAIL(Status)) {
			printf("Could not format ARC non-volatile storage\r\n");
			break;
		}

		// Seek to MBR partition 3 and write the empty FAT filesystem there
		printf("Formatting FAT16 ARC system partition...\r\n");
		static UCHAR s_Bpb32M[] = {
			  0xEB, 0xFE, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E,
			  0x30, 0x00, 0x02, 0x04, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00,
			  0x00, 0xF8, 0x40, 0x00, 0x20, 0x00, 0x40, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x80, 0x00, 0x29, 0x15,
			  0x45, 0x14, 0x2B, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
			  0x20, 0x20, 0x20, 0x20, 0x46, 0x41, 0x54, 0x31, 0x36, 0x20,
			  0x20, 0x20
		};
		static UCHAR s_FatEmpty[] = { 0xF8, 0xFF, 0xFF, 0xFF };
		_Static_assert(__builtin_offsetof(MBR_SECTOR, MbrCode) == 0);
		memset(&Mbr, 0, sizeof(Mbr));
		memcpy(Mbr.MbrCode, s_Bpb32M, sizeof(s_Bpb32M));
		Mbr.ValidMbr = MBR_VALID_SIGNATURE;
		// Copy the boot sector
		memcpy(SysPartFatFs, &Mbr, sizeof(Mbr));
		// Copy the two copies of the FAT
		memset(&Mbr, 0, sizeof(Mbr));
		memcpy(Mbr.MbrCode, s_FatEmpty, sizeof(s_FatEmpty));
		memcpy(&SysPartFatFs[0x200], &Mbr, sizeof(Mbr));
		memcpy(&SysPartFatFs[0x8200], &Mbr, sizeof(Mbr));


		SeekOffset = INT32_TO_LARGE_INTEGER(ArcSystemPartitionSectorOffset);
		SeekOffset.QuadPart *= REPART_SECTOR_SIZE;
		Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
		if (ARC_SUCCESS(Status)) {
			// Write to disk
			Count = 0;
			Status = Vectors->Write(DeviceId, SysPartFatFs, SIZE_OF_SYS_PART_FAT_FS, &Count);
			if (ARC_SUCCESS(Status) && Count != SIZE_OF_SYS_PART_FAT_FS) Status = _EIO;
		}
		if (ARC_FAIL(Status)) {
			printf("Could not format ARC system partition\r\n");
			break;
		}

		// Seek to MBR partition 2 and write zeroes over its first sector (this is enough to ensure no existing FAT/NTFS partition is there, right?)
		memset(&Mbr, 0, sizeof(Mbr));
		ULONG PartitionBeingWiped = 0;
		if (NtPartitionIsExtended) {
			printf("Formatting NT OS partition as NTFS...\r\n");
			// Format as NTFS!
			Status = RpFormatNtfs(DeviceId, Vectors, REPART_MBR_PART2_START, NtPartMb, MbrSignature);
		}
		else {
			printf("Ensuring user partitions are all considered unformatted...\r\n");
			SeekOffset = INT32_TO_LARGE_INTEGER(REPART_MBR_PART2_START * REPART_SECTOR_SIZE);
			Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
			if (ARC_SUCCESS(Status)) {
				// Write to disk
				Count = 0;
				Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(Mbr)) Status = _EIO;
			}
		}
		if (ARC_SUCCESS(Status)) {
			if (NtPartitionIsExtended) printf("Ensuring Mac partitions are all considered unformatted...\r\n");
			// For each user APM partition, erase the third sector and second from last sector
			for (ULONG i = 0; i < CountMacParts; i++) {
				PartitionBeingWiped++;
				// third sector
				int64_t SeekOffset64 = Apm[7 + i].SectorStart;
				SeekOffset64 += 2;
				SeekOffset64 *= REPART_SECTOR_SIZE;
				SeekOffset = INT64_TO_LARGE_INTEGER(SeekOffset64);
				Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
				if (ARC_FAIL(Status)) break;
				Count = 0;
				Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(Mbr)) Status = _EIO;
				if (ARC_FAIL(Status)) break;

				// second from last sector
				SeekOffset64 = Apm[7 + i].SectorStart;
				SeekOffset64 += Apm[7 + i].SectorCount - 2;
				SeekOffset = INT64_TO_LARGE_INTEGER(SeekOffset64);
				Status = Vectors->Seek(DeviceId, &SeekOffset, SeekAbsolute);
				if (ARC_FAIL(Status)) break;
				Count = 0;
				Status = Vectors->Write(DeviceId, &Mbr, sizeof(Mbr), &Count);
				if (ARC_SUCCESS(Status) && Count != sizeof(Mbr)) Status = _EIO;
				if (ARC_FAIL(Status)) break;
			}
		}
		if (ARC_FAIL(Status)) {
			if (PartitionBeingWiped == 0) printf("Could not clear initial sectors of NT partition\r\n");
			else printf("Could not clear initial sectors of Mac partition %d\r\n", PartitionBeingWiped);
			break;
		}

		// Everything is done
		printf("Successfully written partition tables and installed ARC firmware.\r\n");
	} while (false);

	free(SysPartFatFs);
	free(Apm);

	Api->CloseRoutine(hDrvwiki);
	Api->CloseRoutine(hDrvptDR);
	Api->CloseRoutine(hBootImg);
	Api->CloseRoutine(hStage2);
	Api->CloseRoutine(hStage1);
	return Status;
}

/// <summary>
/// Updates the boot partition to the ARC firmware version located on the boot media.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <param name="SourceDevice">Source device path where files get copied from</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsUpdateBootPartition(ULONG DeviceId, const char* SourceDevice) {
	// Get the device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;
	// Can't be a file.
	if (Device->DeviceId != FILE_IS_RAW_DEVICE) return _EBADF;
	// Sector size must be 512 bytes. atapi.sys expects this anyway!
	if (Device->GetSectorSize == NULL) return _EBADF;
	ULONG SectorSize;
	if (ARC_FAIL(Device->GetSectorSize(DeviceId, &SectorSize))) return _EBADF;
	if (SectorSize != REPART_SECTOR_SIZE) return _EBADF;
	PDEVICE_VECTORS Vectors = Device->DeviceEntryTable;

	// Ensure APM partition 6 is our boot partition.
	APM_SECTOR ApmEntry;
	ULONG Count = 0;
	LARGE_INTEGER Offset = INT32_TO_LARGE_INTEGER(6 * REPART_SECTOR_SIZE);
	ARC_STATUS Status = Vectors->Seek(DeviceId, &Offset, SeekAbsolute);
	if (ARC_FAIL(Status)) return Status;
	Status = Vectors->Read(DeviceId, &ApmEntry, sizeof(ApmEntry), &Count);
	if (ARC_SUCCESS(Status) && Count != sizeof(ApmEntry)) Status = _EIO;
	if (ARC_FAIL(Status)) return Status;
	if (ApmEntry.Signature != APM_VALID_SIGNATURE) return _EBADF;
	if (ApmEntry.ApmTableSectors < 7) return _EBADF;
	if (ApmEntry.ApmTableSectors > 63) return _EBADF;
	if (ApmEntry.SectorStart != REPART_APM_PART6_START) return _EBADF;
	if (ApmEntry.SectorCount != REPART_APM_PART6_SIZE) return _EBADF;
	if (strcmp(ApmEntry.Type, "Apple_Boot") != 0) return _EBADF;

	// APM partition entry looks ok
	// craft new image in memory:

	// Find some free memory that is at least the length we need.
	ULONG PageLen = REPART_BOOTIMG_SIZE / PAGE_SIZE;
	if ((REPART_BOOTIMG_SIZE & (PAGE_SIZE - 1)) != 0) PageLen++;
	PVOID Buffer = NULL;
	{
		PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

		for (PMEMORY_DESCRIPTOR MemDesc = Api->MemoryRoutine(NULL); MemDesc != NULL; MemDesc = Api->MemoryRoutine(MemDesc)) {
			if (MemDesc->MemoryType != MemoryFree) continue;
			if (MemDesc->PageCount < PageLen) continue;
			Buffer = (PVOID)((MemDesc->BasePage * PAGE_SIZE) | 0x80000000);
			break;
		}
	}
	if (Buffer == NULL) return _ENOMEM;

	// Calculate the path to the files we need and open them and get their file sizes
	char s_BootPath[1024];
	ULONG BootPathIdx = snprintf(s_BootPath, sizeof(s_BootPath), "%s\\", SourceDevice);

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

	printf("Loading files...\r\n");
	// stage1
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "stage1.elf");
	REPART_INIT_FILE(Stage1);
	if (lenStage1 == 0) {
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenStage1 > REPART_STAGE1_MAX) {
		printf("stage1.elf is too big, size=%dKB, maximum=%dKB\r\n", lenStage1 / 1024, REPART_STAGE1_MAX / 1024);
		return _EFAULT;
	}

	// stage2
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "stage2.elf");
	REPART_INIT_FILE(Stage2);
	if (lenStage2 == 0) {
		Api->CloseRoutine(hStage1);
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenStage2 > REPART_STAGE2_MAX) {
		Api->CloseRoutine(hStage1);
		printf("stage2.elf is too big, size=%dKB, maximum=%dKB\r\n", lenStage2 / 1024, REPART_STAGE2_MAX / 1024);
		return _EFAULT;
	}

	// bootimg
	snprintf(&s_BootPath[BootPathIdx], sizeof(s_BootPath) - BootPathIdx, "boot.img");
	REPART_INIT_FILE(BootImg);
	if (lenBootImg == 0) {
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not open %s\r\n", s_BootPath);
		return _EFAULT;
	}
	if (lenBootImg != REPART_BOOTIMG_SIZE) {
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("boot.img size incorrect, size=%dKB, expected=%dKB\r\n", lenBootImg / 1024, REPART_BOOTIMG_SIZE / 1024);
		return _EFAULT;
	}

	// Read bootimg
	U32LE CountLe;
	CountLe.v = 0;
	Status = Api->ReadRoutine(hBootImg, Buffer, lenBootImg, &CountLe);
	Api->CloseRoutine(hBootImg);
	if (ARC_FAIL(Status) || CountLe.v != lenBootImg) {
		Api->CloseRoutine(hStage2);
		Api->CloseRoutine(hStage1);
		printf("Could not read boot.img\r\n");
		if (ARC_SUCCESS(Status)) Status = _EBADF;

		return Status;
	}
	// Find where stage1 and stage2 goes
	PBYTE BufferStage1 = mem_mem(Buffer, "*STAGE1*", lenBootImg, sizeof("*STAGE1*") - 1);
	PBYTE BufferStage2 = mem_mem(Buffer, "*STAGE2*", lenBootImg, sizeof("*STAGE2*") - 1);

	if (BufferStage1 == NULL || BufferStage2 == NULL) {
		printf("Could not read boot.img\r\n");
		return _EBADF;
	}

	do {
		// Read stage1 into the location calculated previously
		CountLe.v = 0;
		Status = Api->ReadRoutine(hStage1, BufferStage1, lenStage1, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenStage1) {
			printf("Could not read stage1.elf\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}
		// Read stage2 into the location calculated previously
		CountLe.v = 0;
		Status = Api->ReadRoutine(hStage2, BufferStage2, lenStage2, &CountLe);
		if (ARC_FAIL(Status) || CountLe.v != lenStage2) {
			printf("Could not read stage2.elf\r\n");
			if (ARC_SUCCESS(Status)) Status = _EBADF;
			break;
		}
		printf("Writing boot partition...\r\n");
		// Seek to correct location
		Offset = INT32_TO_LARGE_INTEGER(REPART_APM_PART6_START * REPART_SECTOR_SIZE);
		Status = Vectors->Seek(DeviceId, &Offset, SeekAbsolute);
		if (ARC_FAIL(Status)) {
			printf("Could not write boot partition\r\n");
			break;
		}
		// Everything has been read into place, write to disk.
		Count = 0;
		Status = Vectors->Write(DeviceId, Buffer, lenBootImg, &Count);
		if (ARC_FAIL(Status) || Count != lenBootImg) {
			printf("Could not write boot partition\r\n");
			if (ARC_SUCCESS(Status)) Status = _EIO;
			break;
		}
		printf("Successfully updated ARC firmware boot partition.\r\n");
	} while (false);

	Api->CloseRoutine(hStage2);
	Api->CloseRoutine(hStage1);
	return Status;
}

static void MbrpCopyTables(PMBR_SECTOR Destination, PMBR_SECTOR Source) {
	memcpy(
		&Destination->MbrCode[sizeof(Destination->MbrCode)],
		&Source->MbrCode[sizeof(Source->MbrCode)],
		sizeof(*Destination) - sizeof(Destination->MbrCode)
	);
}

/// <summary>
/// Corrupt the MBR signature on the boot disk, so OS9 can boot and OSX install can work. Booting back into ARC firmware will fix this.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsCorruptMbr(ULONG DeviceId) {
	// Get the device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;
	// Can't be a file.
	if (Device->DeviceId != FILE_IS_RAW_DEVICE) return _EBADF;
	PDEVICE_VECTORS Vectors = Device->DeviceEntryTable;

	ARC_STATUS Status = _ESUCCESS;

	// double check some things first
	{
		// Sector size must match.
		if (Device->GetSectorSize == NULL) return _EBADF;
		ULONG SectorSize;
		Status = Device->GetSectorSize(DeviceId, &SectorSize);
		if (ARC_FAIL(Status)) return Status;
		if (SectorSize != REPART_SECTOR_SIZE) return _EBADF;

		// Ensure backup-MBR partition is valid.
		APM_SECTOR ApmEntry;
		ULONG Count = 0;
		LARGE_INTEGER Offset = INT32_TO_LARGE_INTEGER(2 * REPART_SECTOR_SIZE);
		ARC_STATUS Status = Vectors->Seek(DeviceId, &Offset, SeekAbsolute);
		if (ARC_FAIL(Status)) return Status;
		Status = Vectors->Read(DeviceId, &ApmEntry, sizeof(ApmEntry), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(ApmEntry)) Status = _EIO;
		if (ARC_FAIL(Status)) return Status;
		if (ApmEntry.Signature != APM_VALID_SIGNATURE) return _EBADF;
		if (ApmEntry.ApmTableSectors < 7) return _EBADF;
		if (ApmEntry.ApmTableSectors > 63) return _EBADF;
		if (ApmEntry.SectorStart != REPART_APM_PART2_START) return _EBADF;
		if (ApmEntry.SectorCount != REPART_APM_PART2_SIZE) return _EBADF;
		if (strcmp(ApmEntry.Type, "CD_partition_scheme") != 0) return _EBADF;
	}

	MBR_SECTOR MbrPrimary;
	ULONG Count;
	do {
		LARGE_INTEGER Position;
		Position.QuadPart = 0;
		// Try to read the MBR.
		Status = Vectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) break;
		Status = Vectors->Read(DeviceId, &MbrPrimary, sizeof(MbrPrimary), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(MbrPrimary)) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// If neither MBR is valid, then it's fine lol
		if (
			(MbrPrimary.ValidMbr != MBR_VALID_SIGNATURE) ||
			(MbrPrimary.Signature == 0)
		) {
			break;
		}

		// Corrupt the MBR signature now
		MbrPrimary.ValidMbr = 0x5555;

		// And write it back to disk
		Position.QuadPart = 0;
		Status = Vectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) break;

		Count = 0;
		Status = Vectors->Write(DeviceId, &MbrPrimary, sizeof(MbrPrimary), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(MbrPrimary)) Status = _EIO;
	} while (false);

	return Status;
}

/// <summary>
/// Ensures the primary and backup MBRs match in the boot disk. If primary MBR is invalid, write backup MBR to primary MBR, otherwise copy primary MBR to backup MBR if needed.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsRestoreMbr(ULONG DeviceId) {
	// Get the device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;
	// Can't be a file.
	if (Device->DeviceId != FILE_IS_RAW_DEVICE) return _EBADF;
	PDEVICE_VECTORS Vectors = Device->DeviceEntryTable;

	ARC_STATUS Status = _ESUCCESS;

	// double check some things first
	{
		// Sector size must match.
		if (Device->GetSectorSize == NULL) return _EBADF;
		ULONG SectorSize;
		Status = Device->GetSectorSize(DeviceId, &SectorSize);
		if (ARC_FAIL(Status)) return Status;
		if (SectorSize != REPART_SECTOR_SIZE) return _EBADF;

		// Ensure backup-MBR partition is valid.
		APM_SECTOR ApmEntry;
		ULONG Count = 0;
		LARGE_INTEGER Offset = INT32_TO_LARGE_INTEGER(2 * REPART_SECTOR_SIZE);
		ARC_STATUS Status = Vectors->Seek(DeviceId, &Offset, SeekAbsolute);
		if (ARC_FAIL(Status)) return Status;
		Status = Vectors->Read(DeviceId, &ApmEntry, sizeof(ApmEntry), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(ApmEntry)) Status = _EIO;
		if (ARC_FAIL(Status)) return Status;
		if (ApmEntry.Signature != APM_VALID_SIGNATURE) return _EBADF;
		if (ApmEntry.ApmTableSectors < 7) return _EBADF;
		if (ApmEntry.ApmTableSectors > 63) return _EBADF;
		if (ApmEntry.SectorStart != REPART_APM_PART2_START) return _EBADF;
		if (ApmEntry.SectorCount != REPART_APM_PART2_SIZE) return _EBADF;
		if (strcmp(ApmEntry.Type, "CD_partition_scheme") != 0) return _EBADF;
	}

	MBR_SECTOR MbrPrimary, MbrBackup;
	ULONG Count;
	do {
		LARGE_INTEGER Position;
		Position.QuadPart = 0;
		// Try to read both MBRs:
		Status = Vectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) break;
		Status = Vectors->Read(DeviceId, &MbrPrimary, sizeof(MbrPrimary), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(MbrPrimary)) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		Position.QuadPart = REPART_APM_PART2_START * REPART_SECTOR_SIZE;
		Status = Vectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) break;
		Count = 0;
		Status = Vectors->Read(DeviceId, &MbrBackup, sizeof(MbrBackup), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(MbrBackup)) Status = _EIO;
		if (ARC_FAIL(Status)) break;

		// If neither MBR is valid, then nothing can be done
		if (
			(MbrPrimary.ValidMbr != MBR_VALID_SIGNATURE && MbrBackup.ValidMbr != MBR_VALID_SIGNATURE) ||
			(MbrPrimary.Signature == 0 && MbrBackup.Signature == 0)
		) {
			Status = _EBADF;
			break;
		}


		// If MBRs both match then everything is fine
		if (
			memcmp(
				&MbrPrimary.MbrCode[sizeof(MbrPrimary.MbrCode)],
				&MbrBackup.MbrCode[sizeof(MbrBackup.MbrCode)],
				sizeof(MbrPrimary) - sizeof(MbrPrimary.MbrCode)
			) == 0
		) {
			break;
		}


		// MBRs do not match.
		// If primary is invalid, copy from backup
		if (MbrPrimary.ValidMbr != MBR_VALID_SIGNATURE || MbrPrimary.Signature == 0) {
			MbrpCopyTables(&MbrPrimary, &MbrBackup);

			Position.QuadPart = 0;
			Status = Vectors->Seek(DeviceId, &Position, SeekAbsolute);
			if (ARC_FAIL(Status)) break;

			Count = 0;
			Status = Vectors->Write(DeviceId, &MbrPrimary, sizeof(MbrPrimary), &Count);
			if (ARC_SUCCESS(Status) && Count != sizeof(MbrPrimary)) Status = _EIO;
			break;
		}


		// Either backup is invalid, or both are valid.
		// Assume that primary got modified (by NT), and copy it to backup.
		// Don't assume anything about the backup sector. Write it out from scratch using the primary MBR.

		memset(MbrBackup.MbrCode, 0, sizeof(MbrBackup.MbrCode));
		memcpy(&MbrBackup.MbrCode[1], "CD001", sizeof("CD001") - 1);
		MbrpCopyTables(&MbrBackup, &MbrPrimary);

		Position.QuadPart = REPART_APM_PART2_START * REPART_SECTOR_SIZE;
		Status = Vectors->Seek(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) break;
		Status = Vectors->Write(DeviceId, &MbrBackup, sizeof(MbrBackup), &Count);
		if (ARC_SUCCESS(Status) && Count != sizeof(MbrBackup)) Status = _EIO;
	} while (false);

	return Status;
}

// Implement wrappers around libfat/libiso9660.

typedef enum {
	FS_UNKNOWN,
	FS_ISO9660,
	FS_FAT
} FS_TYPE;

enum {
	ISO9660_SECTOR_SIZE = 2048,
	FAT_SECTOR_SIZE = 512
};

typedef struct _FS_METADATA {
	union {
		struct {
			l9660_fs Iso9660;
			ULONG DeviceId;
		};
		struct {
			FATFS Fat;
			ULONG SectorPresent;
			UCHAR Sector[FAT_SECTOR_SIZE];
			USHORT WritePosition;
			bool InWrite;
		};
	};
	FS_TYPE Type;
	ULONG SectorSize;
} FS_METADATA, *PFS_METADATA;
_Static_assert(FILE_TABLE_SIZE < 100);

static FS_METADATA s_Metadata[FILE_TABLE_SIZE] = { 0 };

//static ULONG s_CurrentDeviceId = FILE_IS_RAW_DEVICE;

DSTATUS disk_initialize(void) { return RES_OK; }

DRESULT disk_readp(ULONG DeviceId, BYTE* buff, DWORD sector, UINT offset, UINT count) {
	if (DeviceId >= FILE_TABLE_SIZE) return RES_PARERR;
	PFS_METADATA Meta = &s_Metadata[DeviceId];
	if (Meta->Type != FS_FAT) return RES_PARERR;
	// If a write transaction in progress, read will fail
	if (Meta->InWrite) return RES_ERROR;

	if (Meta->SectorPresent != sector) {
		PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
		// Seek to requested sector.
		int64_t Position64 = sector;
		Position64 *= FAT_SECTOR_SIZE;
		LARGE_INTEGER Position = Int64ToLargeInteger(Position64);

		ARC_STATUS Status = Api->SeekRoutine(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) {
			//printf("FAT: could not seek to sector %08x\n", sector);
			return RES_ERROR;
		}

		// Read to buffer.
		ULONG Length = sizeof(Meta->Sector);

		U32LE Count;
		Status = Api->ReadRoutine(DeviceId, Meta->Sector, Length, &Count);
		if (ARC_FAIL(Status)) {
			//printf("FAT: could not read sector %08x\n", sector);
			return RES_ERROR;
		}

		if (Count.v != sizeof(Meta->Sector)) return RES_ERROR;
		Meta->SectorPresent = sector;
	}

	memcpy(buff, &Meta->Sector[offset], count);
	return RES_OK;
}

DRESULT disk_writep(ULONG DeviceId, const BYTE* buff, DWORD sc) {
	if (DeviceId >= FILE_TABLE_SIZE) return RES_PARERR;
	PFS_METADATA Meta = &s_Metadata[DeviceId];
	if (Meta->Type != FS_FAT) return RES_PARERR;

	if (buff == NULL) {
		if (!Meta->InWrite) {
			// Start a write to sector sc
			memset(Meta->Sector, 0, sizeof(Meta->Sector));
			Meta->SectorPresent = sc;
			Meta->InWrite = true;
			Meta->WritePosition = 0;
			return RES_OK;
		}

		if (sc == 0) return RES_PARERR; // sc must be 0 to finish write
		// Finish write, actually perform the write here:

		// Seek to requested sector.
		PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
		int64_t Position64 = Meta->SectorPresent;
		Position64 *= FAT_SECTOR_SIZE;

		LARGE_INTEGER Position = Int64ToLargeInteger(Position64);

		// Ensure any future reads will succeed
		Meta->SectorPresent = 0xffffffff;
		Meta->InWrite = false;

		ARC_STATUS Status = Api->SeekRoutine(DeviceId, &Position, SeekAbsolute);
		if (ARC_FAIL(Status)) {
			return RES_ERROR;
		}

		// Write to sector.
		ULONG Length = sizeof(Meta->Sector);

		U32LE Count;
		Status = Api->WriteRoutine(DeviceId, Meta->Sector, Length, &Count);
		if (ARC_FAIL(Status)) {
			return RES_ERROR;
		}

		if (Count.v != sizeof(Meta->Sector)) return RES_ERROR;
		return RES_OK;
	}

	// Write data into buffer
	DWORD afterOff = sc + Meta->WritePosition;
	if (afterOff > FAT_SECTOR_SIZE) return RES_PARERR;

	memcpy(&Meta->Sector[Meta->WritePosition], buff, sc);
	Meta->WritePosition += sc;
	return RES_OK;
}

static ARC_STATUS IsoErrorToArc(l9660_status status) {
	switch (status) {
	case L9660_OK:
		return _ESUCCESS;
	case L9660_EBADFS:
		return _EBADF;
	case L9660_EIO:
		return _EIO;
	case L9660_ENOENT:
		return _ENOENT;
	case L9660_ENOTDIR:
		return _ENOTDIR;
	case L9660_ENOTFILE:
		return _EISDIR;
	default:
		return _EFAULT;
	}
}

static ARC_STATUS FatErrorToArc(FRESULT fr) {
	switch (fr) {
	case FR_OK:
		return _ESUCCESS;
	case FR_DISK_ERR:
		return _EIO;
	case FR_NOT_READY:
		return _ENXIO;
	case FR_NO_FILE:
		return _ENOENT;
	case FR_NOT_OPENED:
		return _EINVAL;
	case FR_NOT_ENABLED:
		return _ENXIO;
	case FR_NO_FILESYSTEM:
		return _EBADF;
	default:
		return _EFAULT;
	}
}

static bool FsMediumIsoReadSectors(l9660_fs* fs, void* buffer, ULONG sector) {
	PFS_METADATA Metadata = (PFS_METADATA)fs;
	if (Metadata->Type != FS_ISO9660) return false;
	
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	// Seek to requested sector.
	int64_t Position64 = sector;
	Position64 *= ISO9660_SECTOR_SIZE;
	LARGE_INTEGER Position = Int64ToLargeInteger(Position64);

	ARC_STATUS Status = Api->SeekRoutine(Metadata->DeviceId, &Position, SeekAbsolute);
	if (ARC_FAIL(Status)) {
		printf("ISO: could not seek to sector %x\r\n", sector);
		return false;
	}

	// Read to buffer.
	ULONG Length = ISO9660_SECTOR_SIZE;

	U32LE Count;
	Status = Api->ReadRoutine(Metadata->DeviceId, buffer, Length, &Count);
	if (ARC_FAIL(Status)) {
		printf("ISO: could not read sector %x\r\n", sector);
		return false;
	}

	return Length == Count.v;
}

ARC_STATUS FsInitialiseForDevice(ULONG DeviceId) {
	if (DeviceId >= FILE_TABLE_SIZE) return _EBADF;
	PFS_METADATA FsMeta = &s_Metadata[DeviceId];

	// Obtain sector size for device.
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;
	if (Device->GetSectorSize == NULL) return _EBADF;
	ULONG SectorSize;
	ARC_STATUS Status = Device->GetSectorSize(DeviceId, &SectorSize);
	if (ARC_FAIL(Status)) return _EBADF;

	if (FsMeta->SectorSize == SectorSize) return _ESUCCESS;

	FsMeta->SectorSize = SectorSize;

	bool Mounted = false;
	if (SectorSize <= ISO9660_SECTOR_SIZE) {
		FsMeta->Type = FS_ISO9660;
		FsMeta->DeviceId = DeviceId;
		l9660_status IsoStatus = l9660_openfs(&FsMeta->Iso9660, FsMediumIsoReadSectors);
		Mounted = IsoStatus == L9660_OK;
	}
	if (!Mounted && SectorSize <= FAT_SECTOR_SIZE) {
		// ISO9660 mount failed, attempt FAT
		// Zero out everything used by the ISO9660 part
		memset(&FsMeta->Iso9660, 0, sizeof(FsMeta->Iso9660));
		FsMeta->DeviceId = 0;
		FsMeta->Type = FS_FAT;
		FsMeta->Fat.DeviceId = DeviceId;
		FsMeta->SectorPresent = 0xFFFFFFFF;
		Mounted = pf_mount(&FsMeta->Fat) == FR_OK;
	}

	if (!Mounted) {
		memset(FsMeta, 0, sizeof(*FsMeta));
		return _EBADF;
	}

	return _ESUCCESS;
}

ARC_STATUS FsUnmountForDevice(ULONG DeviceId) {
	if (DeviceId >= FILE_TABLE_SIZE) return _EBADF;
	PFS_METADATA FsMeta = &s_Metadata[DeviceId];
	if (FsMeta->SectorSize == 0) return _EBADF;

	memset(FsMeta, 0, sizeof(*FsMeta));
	return _ESUCCESS;
}



// Filesystem device functions.
static ARC_STATUS FsOpen(PCHAR OpenPath, OPEN_MODE OpenMode, PULONG FileId) {
	// Only allow to open existing files.
	switch (OpenMode) {
	case ArcCreateDirectory:
	case ArcOpenDirectory:
	case ArcCreateReadWrite:
	case ArcCreateWriteOnly:
	case ArcSupersedeWriteOnly:
	case ArcSupersedeReadWrite:
		return _EINVAL;
	default: break;
	}

	//printf("Open %s(%d) [%x]\n", OpenPath, OpenMode, *FileId);
	// Get the file table, we need the device ID.
	PARC_FILE_TABLE File = ArcIoGetFileForOpen(*FileId);
	if (File == NULL) return _EBADF;
	ULONG DeviceId = File->DeviceId;
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	if (Device == NULL) return _EBADF;

	//s_CurrentDeviceId = DeviceId;
	PFS_METADATA Meta = &s_Metadata[DeviceId];

	ARC_STATUS Status = _ESUCCESS;

	//PCHAR EndOfPath = strrchr(OpenPath, '\\');
	File->u.FileContext.FileSize.QuadPart = 0;

	switch (Meta->Type) {
	case FS_ISO9660:
		// ISO9660 open file.
	{
		// Open root directory
		l9660_dir root;
		Status = IsoErrorToArc(l9660_fs_open_root(&root, &Meta->Iso9660));
		//printf("l9660_fs_open_root %d\r\n", Status);
		if (ARC_FAIL(Status)) return Status;

		// Open file.
		Status = IsoErrorToArc(l9660_openat(&File->u.FileContext.Iso9660, &root, &OpenPath[1]));
		//printf("l9660_fs_openat %d\r\n", Status);
		if (ARC_FAIL(Status)) return Status;

		File->u.FileContext.FileSize.LowPart = File->u.FileContext.Iso9660.length;

		break;
	}

	case FS_FAT:
		// FATFS open file.
		File->u.FileContext.Fat = Meta->Fat;
		Status = FatErrorToArc(pf_open(&File->u.FileContext.Fat, OpenPath));
		if (ARC_FAIL(Status)) return Status;

		File->u.FileContext.FileSize.LowPart = File->u.FileContext.Fat.fsize;
		break;

	default:
		return _EBADF;
	}

	// Set flags.
	switch (OpenMode) {
	case ArcOpenReadOnly:
		File->Flags.Read = 1;
		break;
	case ArcOpenWriteOnly:
		File->Flags.Write = 1;
		break;
	case ArcOpenReadWrite:
		File->Flags.Read = 1;
		File->Flags.Write = 1;
		break;
	default: break;
	}

	return Status;
}

static ARC_STATUS FsClose(ULONG FileId) {
	// Get the file table
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	if (File == NULL) return _EBADF;

	PFS_METADATA Meta = &s_Metadata[File->DeviceId];
	
	switch (Meta->Type) {
	case FS_ISO9660:
		break;
	case FS_FAT:
		break;
	default:
		return _EBADF;
	}
	return _ESUCCESS;
}

static ARC_STATUS FsMount(PCHAR MountPath, MOUNT_OPERATION Operation) { return _EINVAL; }

static ARC_STATUS FsRead(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
	//printf("Read %x => (%08x) %x bytes\r\n", FileId, Buffer, Length);
	// Get the file table
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	if (File == NULL) return _EBADF;
	if (Length == 0) {
		*Count = 0;
		return _ESUCCESS;
	}

	PFS_METADATA Meta = &s_Metadata[File->DeviceId];

	switch (Meta->Type) {
	case FS_ISO9660:
		return IsoErrorToArc(l9660_read(&File->u.FileContext.Iso9660, Buffer, Length, Count));
	case FS_FAT:
		return FatErrorToArc(pf_read(&File->u.FileContext.Fat, Buffer, Length, Count));
	default:
		return _EBADF;
	}
}

static ARC_STATUS FsWrite(ULONG FileId, PVOID Buffer, ULONG Length, PULONG Count) {
	//printf("Write %x => %x bytes\n", FileId, Length);
	// Get the file table
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	if (File == NULL) return _EBADF;

	PFS_METADATA Meta = &s_Metadata[File->DeviceId];
	switch (Meta->Type) {
	case FS_ISO9660:
		return _EBADF; // no writing to iso fs
	case FS_FAT:
		return FatErrorToArc(pf_write(&File->u.FileContext.Fat, Buffer, Length, Count));
	default:
		return _EBADF;
	}
}

static ARC_STATUS FsSeek(ULONG FileId, PLARGE_INTEGER Offset, SEEK_MODE SeekMode) {
	//printf("Seek %x => %llx (%d)\n", FileId, Offset->QuadPart, SeekMode);
	// Get the file table
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	if (File == NULL) return _EBADF;
	// only support s32 offsets
	// positive
	if (Offset->QuadPart > INT32_MAX || Offset->QuadPart < INT32_MIN) {
		//printf("seek: Bad offset %llx\n", Offset->QuadPart);
		return _EINVAL;
	}

	int Origin;
	switch (SeekMode) {
	case SeekRelative:
		File->Position += Offset->QuadPart;
		if (File->Position > INT32_MAX) {
			//printf("seek: Bad offset %llx\n", Offset->QuadPart);
			return _EINVAL;
		}
		Origin = SEEK_CUR;
		break;
	case SeekAbsolute:
		File->Position = Offset->QuadPart;
		Origin = SEEK_SET;
		break;
	default:
		//printf("seek: Bad mode %d\n", SeekMode);
		return _EINVAL;
	}

	PFS_METADATA Meta = &s_Metadata[File->DeviceId];

	switch (Meta->Type) {
	case FS_ISO9660:
		return IsoErrorToArc(l9660_seek(&File->u.FileContext.Iso9660, Origin == SEEK_CUR ? L9660_SEEK_CUR : L9660_SEEK_SET, Offset->LowPart));
	case FS_FAT:
		return FatErrorToArc(pf_lseek(&File->u.FileContext.Fat, (ULONG)File->Position));
	default:
		return _EBADF;
	}
}

static ARC_STATUS FsGetFileInformation(ULONG FileId, PFILE_INFORMATION FileInfo) {
	// Get the file table
	PARC_FILE_TABLE File = ArcIoGetFile(FileId);
	if (File == NULL) return _EBADF;

	FileInfo->EndingAddress.QuadPart = File->u.FileContext.FileSize.QuadPart;
	FileInfo->CurrentPosition.QuadPart = File->Position;

	FileInfo->FileNameLength = 0; // TODO: fix?
	return _ESUCCESS;
}

static ARC_STATUS FsSetFileInformation(ULONG FileId, ULONG AttributeFlags, ULONG AttributeMask) {
	return _EACCES;
}

static ARC_STATUS FsGetDirectoryEntry(ULONG FileId, PDIRECTORY_ENTRY DirEntry, ULONG NumberDir, PULONG CountDir) {
	return _EBADF;
}

// Filesystem device vectors.
static const DEVICE_VECTORS FsVectors = {
	.Open = FsOpen,
	.Close = FsClose,
	.Mount = FsMount,
	.Read = FsRead,
	.Write = FsWrite,
	.Seek = FsSeek,
	.GetReadStatus = NULL,
	.GetFileInformation = FsGetFileInformation,
	.SetFileInformation = FsSetFileInformation,
	.GetDirectoryEntry = FsGetDirectoryEntry
};



void FsInitialiseTable(PARC_FILE_TABLE File) {
	ULONG DeviceId = File->DeviceId;
	PARC_FILE_TABLE Device = ArcIoGetFile(DeviceId);
	File->DeviceEntryTable = &FsVectors;
}
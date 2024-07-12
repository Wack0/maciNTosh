#pragma once

enum {
	REPART_SECTOR_SIZE = 0x200,
	REPART_APM_SECTORS = 0x8000 / REPART_SECTOR_SIZE,
	REPART_KB_SECTORS = 0x400 / REPART_SECTOR_SIZE,
	REPART_MB_SECTORS = 0x100000 / REPART_SECTOR_SIZE,
	REPART_U32_MAX_SECTORS_IN_MB = 0x1FFFFF,
	REPART_MAX_NT_PART_IN_MB = 8063 - 33, // CHS limit - 32MB for ARC system partition - 1MB for initial partitions + partition table

	REPART_STAGE1_MAX = 16 * 1024,
	REPART_STAGE2_MAX = 224 * 1024,
	REPART_BOOTIMG_SIZE = 256 * 1024,
	REPART_DRIVER_MAX = 64 * 1024,

	REPART_MBR_PART1_SIZE = (0x100000 - 0x8000) / REPART_SECTOR_SIZE,
	REPART_MBR_PART2_START = REPART_APM_SECTORS + REPART_MBR_PART1_SIZE,
	REPART_MBR_PART3_SIZE = REPART_MB_SECTORS * 32,
	REPART_MBR_CHS_LIMIT = 8 * 1024 * REPART_MB_SECTORS,

	REPART_APM_MINIMUM_PARTITIONS = 7,
	REPART_APM_MAXIMUM_PARTITIONS = 63,

	REPART_APM_PART1_START = 1,
	REPART_APM_PART1_SIZE = REPART_APM_SECTORS - 1,
	REPART_APM_PART2_START = REPART_APM_PART1_START + REPART_APM_PART1_SIZE,
	REPART_APM_PART2_SIZE = 1,
	REPART_APM_PART3_START = REPART_APM_PART2_START + REPART_APM_PART2_SIZE,
	REPART_APM_PART3_SIZE = 64 * REPART_KB_SECTORS,
	REPART_APM_PART4_START = REPART_APM_PART3_START + REPART_APM_PART3_SIZE,
	REPART_APM_PART4_SIZE = 64 * REPART_KB_SECTORS,
	REPART_APM_PART5_START = REPART_APM_PART4_START + REPART_APM_PART4_SIZE,
	REPART_APM_PART5_SIZE = 256 * REPART_KB_SECTORS,
	REPART_APM_PART6_START = REPART_APM_PART5_START + REPART_APM_PART5_SIZE,
	REPART_APM_PART6_SIZE = 256 * REPART_KB_SECTORS,
	REPART_APM_PART7_START = REPART_APM_PART6_START + REPART_APM_PART6_SIZE
};

/// <summary>
/// Gets the number of partitions in the Apple Partition Map.
/// </summary>
/// <param name="DeviceVectors">Device function table.</param>
/// <param name="DeviceId">Device ID.</param>
/// <param name="SectorSize">Sector size for the device.</param>
/// <returns>Partition count, 0 if error occurred or disk has no Apple Partition Map.</returns>
ULONG ArcFsApmPartitionCount(PDEVICE_VECTORS DeviceVectors, ULONG DeviceId, ULONG SectorSize);

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
ARC_STATUS ArcFsPartitionObtain(PDEVICE_VECTORS DeviceVectors, ULONG DeviceId, ULONG PartitionId, ULONG SectorSize, PULONG SectorStart, PULONG SectorCount);

/// <summary>
/// Gets the number of partitions (that can be read by this ARC firmware) on a disk.
/// </summary>
/// <param name="DeviceId">Device ID.</param>
/// <param name="PartitionCount">On success obtains the number of partitions</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsPartitionCount(ULONG DeviceId, PULONG PartitionCount);

/// <summary>
/// Gets the number of MBR partitions on a disk.
/// </summary>
/// <param name="DeviceVectors">Device function table.</param>
/// <param name="DeviceId">Device ID.</param>
/// <param name="SectorSize">Sector size for the device.</param>
/// <returns>Number of partitions or 0 on failure</returns>
ULONG ArcFsMbrPartitionCount(PDEVICE_VECTORS DeviceVectors, ULONG DeviceId, ULONG SectorSize);

/// <summary>
/// Check if the files required to repartition a disk exists on a source device
/// </summary>
/// <param name="SourceDevice">ARC path source device.</param>
/// <returns>ARC status code.</returns>
ARC_STATUS ArcFsRepartFilesOnDisk(const char* SourceDevice);

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
ARC_STATUS ArcFsRepartitionDisk(ULONG DeviceId, const char* SourceDevice, ULONG NtPartMb, PULONG MacPartsMb, ULONG CountMacParts, bool* DataWritten);

/// <summary>
/// Updates the boot partition to the ARC firmware version located on the boot media.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <param name="SourceDevice">Source device path where files get copied from</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsUpdateBootPartition(ULONG DeviceId, const char* SourceDevice);

/// <summary>
/// Corrupt the MBR signature on the boot disk, so OS9 can boot and OSX install can work. Booting back into ARC firmware will fix this.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsCorruptMbr(ULONG DeviceId);

/// <summary>
/// Ensures the primary and backup MBRs match in the boot disk. If primary MBR is invalid, write backup MBR to primary MBR, otherwise copy primary MBR to backup MBR if needed.
/// </summary>
/// <param name="DeviceId">Device ID</param>
/// <returns>ARC status code</returns>
ARC_STATUS ArcFsRestoreMbr(ULONG DeviceId);


ARC_STATUS FsInitialiseForDevice(ULONG DeviceId);

ARC_STATUS FsUnmountForDevice(ULONG DeviceId);

void FsInitialiseTable(PARC_FILE_TABLE File);

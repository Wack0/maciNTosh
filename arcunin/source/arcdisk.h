#pragma once

ARC_STATUS ArcDiskTryEject(ULONG FileId);

void ArcDiskGetCounts(PULONG Disk, PULONG Cdrom);

ULONG ArcDiskGetPartitionCount(ULONG Disk);

ULONG ArcDiskGetSizeMb(ULONG Disk);

void ArcDiskInit(void);
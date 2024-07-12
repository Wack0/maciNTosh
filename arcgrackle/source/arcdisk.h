#pragma once

/// <summary>
/// Unmount all USB mass storage devices, when using USBv5
/// </summary>
void ArcDiskUnmountAllUsb(void);

void ArcDiskGetCounts(PULONG Disk, PULONG Cdrom);

ULONG ArcDiskGetPartitionCount(ULONG Disk);

ULONG ArcDiskGetSizeMb(ULONG Disk);

void ArcDiskInit(void);
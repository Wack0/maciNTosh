// MESH SCSI driver API header.
#pragma once

typedef struct _MESH_SCSI_DEVICE MESH_SCSI_DEVICE, * PMESH_SCSI_DEVICE;
struct _MESH_SCSI_DEVICE {
	PMESH_SCSI_DEVICE Next;
	ULONG NumberOfSectors;
	ULONG BytesPerSector;
	UCHAR TargetId;
	UCHAR Lun;
	UCHAR IsCdRom;
};

int mesh_init(uint32_t addr);
PMESH_SCSI_DEVICE mesh_get_first_device(void);
PMESH_SCSI_DEVICE mesh_open_drive(UCHAR TargetId, UCHAR Lun);
ULONG mesh_read_blocks(PMESH_SCSI_DEVICE drive, PVOID buffer, ULONG sector, ULONG count);
ULONG mesh_write_blocks(PMESH_SCSI_DEVICE drive, PVOID buffer, ULONG sector, ULONG count);

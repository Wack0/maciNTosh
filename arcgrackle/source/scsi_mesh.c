// SCSI - Heathrow MESH driver
// MESH is a derivative of the NCR 53C9x scsi controller, kind of. some registers are similar, that's basically it.
// It was included in Hydra so was documented in "Macintosh Technology for the Common Hardware Reference Platform".

#include <stdio.h>
#include <stdlib.h>

#include "arc.h"
#include "runtime.h"
#include "timer.h"
#include "scsi_mesh.h"

// MESH registers
typedef volatile UCHAR MESH_REGISTER __attribute__((aligned(0x10)));

typedef struct _MESH_REGISTERS {
	// The two TransferCount registers are used to indicate the number of bytes to be transferred
	MESH_REGISTER TransferCountLow;
	MESH_REGISTER TransferCountHigh;
	// 16 byte FIFO, number of bytes in the FIFO indicated in the FIFO Count register
	MESH_REGISTER Fifo;
	// Used to direct the controller into a SCSI bus phase. 4 bits command opcode, bits for active negation; attenuation; target mode; DMA.
	MESH_REGISTER Command;
	// Req32 : Ack32 : Req : Ack : Atn : Msg : CD : IO
	MESH_REGISTER ScsiStatusLow;
	// RST : Busy : Select : <unused low 5 bits>
	MESH_REGISTER ScsiStatusHigh;
	// Number of bytes in FIFO, for a data transfer to be considered complete both TransferCount and FifoCount must be clear
	MESH_REGISTER FifoCount;
	// <unused high 2 bits> : SelectWithAttention : Selected : Reselected : ArbitrationLost : PhaseMismatch : SelectionTimeOut
	MESH_REGISTER Exception;
	// <unused high 1 bit> : UnexplainedDisconnection : ScsiReset : SequenceError : ParityError [4 bits]
	MESH_REGISTER Error;
	// Write 0 to a bit disables that interrupt
	// <unused high 5 bits> : Error : Exception : CmdDone
	MESH_REGISTER IntMask;
	// Write 1 to clear the interrupt
	// <unused high 5 bits> : Error : Exception : CmdDone
	MESH_REGISTER Interrupt;
	// Contains the SCSI bus ID used for arbitration. "It is also the register that the controller responds to during the Selection phase (in target mode) or Reselection phase (in initiator mode)"
	MESH_REGISTER SourceID;
	// Contains the SCSI bus ID of the device to be selected or reselected.
	MESH_REGISTER DestID;
	// High nibble is Sync Offset, low nibble is Sync Period (which must be set to 2 or greater for async transfer, OF driver sets it to 2, and offset nibble to 0)
	MESH_REGISTER SyncParams;
	// Used to indicate the version of the SCSI controller. Heathrow's is version 4, Grand Central's (and Hydra's) is 0xE2.
	MESH_REGISTER MeshID;
	// Selection timeout, set to 0x19 (decimal 25) by the OF driver.
	MESH_REGISTER SelTimeOut;
} MESH_REGISTERS, *PMESH_REGISTERS;

enum {
	STSLOW_REQ32 = ARC_BIT(7),
	STSLOW_ACK32 = ARC_BIT(6),
	STSLOW_REQ = ARC_BIT(5),
	STSLOW_ACK = ARC_BIT(4),
	STSLOW_ATN = ARC_BIT(3),
	STSLOW_MSG = ARC_BIT(2),
	STSLOW_CD = ARC_BIT(1),
	STSLOW_IO = ARC_BIT(0),

	STSHIGH_RST = ARC_BIT(7),
	STSHIGH_BUSY = ARC_BIT(6),
	STSHIGH_SELECT = ARC_BIT(5)
};

enum {
	SEQ_CMD_NOP = 0, ///< No operation
	SEQ_CMD_ARB = 1, ///< Arbitrate
	SEQ_CMD_SEL = 2, ///< Selection
	SEQ_CMD_CMD = 3, ///< Execute command
	SEQ_CMD_STATUS = 4, ///< Get status
	SEQ_CMD_DATAOUT = 5, ///< Send data
	SEQ_CMD_DATAIN = 6, ///< Receive data
	SEQ_CMD_MSGOUT = 7, ///< Send message
	SEQ_CMD_MSGIN = 8, ///< Receive message
	SEQ_CMD_BUSFREE = 9, ///< Bus expected to be free
	SEQ_CMD_ENPARCHK = 0xA, ///< Enable parity checks
	SEQ_CMD_DISPARCHK = 0xB, ///< Disable parity checks
	SEQ_CMD_ENRESEL = 0xC, ///< Enables reselection
	SEQ_CMD_DISRESEL = 0xD, ///< Disable reselection
	SEQ_CMD_RESET = 0xE, ///< Software-reset SCSI controller
	SEQ_CMD_FLUSH = 0xF, ///< Flushes FIFO

	SEQ_MASKCMD = BIT(4) - 1,

	SEQ_ACTNEG = BIT(4), ///< Active negation mode
	SEQ_ATN = BIT(5), ///< Assert the ATN signal
	SEQ_TMODE = BIT(6), ///< Puts the controller into target mode
	SEQ_DMA = BIT(7) ///< Transfer to be done using DMA
};

enum {
	INT_CMDDONE = BIT(0),
	INT_EXCEPTION = BIT(1),
	INT_ERROR = BIT(2)
};

enum {
	EXC_SELECTIONTIMEOUT = BIT(0),
	EXC_PHASEMISMATCH = BIT(1),
	EXC_ARBITRATIONLOST = BIT(2),
	EXC_RESELECTED = BIT(3),
	EXC_SELECTED = BIT(4),
	EXC_SELECTWITHATTENUTAION = BIT(5)
};

static PMESH_REGISTERS s_Mesh = NULL;
static PMESH_SCSI_DEVICE s_FirstScsiDevice = NULL;
// BUGBUG: should really be using MmioWrite8 here???

static void mesh_set_tx_count(uint16_t value) {
	s_Mesh->TransferCountHigh = (value >> 8);
	s_Mesh->TransferCountLow = value;
}

static void mesh_clear_interrupts(void) {
	s_Mesh->Interrupt = s_Mesh->Interrupt;
}

// If start is true, starts the timer.
// Otherwise, returns true if timer is expired.
static bool mesh_do_timeout(bool start) {
	static ULONG s_EndMsecs = 0;
	if (start) {
		ULONG Msecs = currmsecs();
		enum {
			WRAP_AROUND_MSECS = (UINT32_MAX - (15000 - 1))
		};
		if (Msecs >= WRAP_AROUND_MSECS) {
			// Wait for our counter to wrap-around.
			ULONG WrapAroundTime = WRAP_AROUND_MSECS - Msecs;
			if (WrapAroundTime == 0) WrapAroundTime = 1;
			mdelay(WrapAroundTime);
		}
		s_EndMsecs = currmsecs() + 15000;
		return false;
	}

	return (currmsecs() >= s_EndMsecs);
}

static void mesh_clear_fifo(void) {
	while (s_Mesh->FifoCount) {
		(void)s_Mesh->Fifo;
	}
}

static ULONG mesh_read_fifo(PVOID Buffer, ULONG Length) {
	PUCHAR Buffer8 = (PUCHAR)Buffer;
	//mesh_do_timeout(true);
	for (ULONG i = 0; i < Length;) {
		// Wait on fifo.
		while (s_Mesh->FifoCount == 0) {
			if (mesh_do_timeout(false)) return ((ULONG)Buffer8 - (ULONG)Buffer);
		}
		// Read out of the fifo into the buffer
		UCHAR FifoCount = s_Mesh->FifoCount;
		if (FifoCount > Length) FifoCount = Length;
		MmioReadBuf8(&s_Mesh->Fifo, Buffer8, FifoCount);
		Buffer8 += FifoCount;
		i += FifoCount;
		Length -= FifoCount;
	}
	return ((ULONG)Buffer8 - (ULONG)Buffer);
}

static ULONG mesh_write_fifo(PVOID Buffer, ULONG Length) {
	MmioWriteBuf8(&s_Mesh->Fifo, (PUCHAR)Buffer, Length);
}

static void mesh_reset(void) {
	mesh_clear_interrupts();

	// put the scsi controller into reset
	s_Mesh->ScsiStatusLow = 0;
	s_Mesh->ScsiStatusHigh = STSHIGH_RST;
	mdelay(100);

	// take the scsi controller out of reset
	s_Mesh->ScsiStatusHigh = 0;
	mdelay(100);

	mesh_clear_interrupts();

	// software reset the scsi controller
	s_Mesh->Command = SEQ_CMD_RESET;
	mdelay(10);
	mesh_clear_interrupts();

	// ensure bus is free
	s_Mesh->Command = SEQ_CMD_BUSFREE;
	mdelay(10);

	mesh_clear_fifo();
	mesh_clear_interrupts();
}

static bool mesh_wait_op_done(void) {
	//mesh_do_timeout(true);
	while (s_Mesh->Interrupt == 0) {
		if (mesh_do_timeout(false)) return false;
	}

	if (s_Mesh->Interrupt == INT_CMDDONE) {
		mesh_clear_interrupts();
		return true;
	}

	return false;
}

#if 0
#define SCSI_DEBUG(x, ...) printf("SCSI: " x "\r\n", ## __VA_ARGS__)
#else
#define SCSI_DEBUG(x, ...)
#endif

static UCHAR mesh_run_scsi_command(UCHAR TargetId, PUCHAR ScsiCmd, ULONG ScsiCmdLength, PVOID TransferBuffer, ULONG TransferLength32, bool Write, PUCHAR Status) {
	PUCHAR Buffer = (PUCHAR)TransferBuffer;
	if (Status == NULL) return 0xFF;
	// TransferLength of 0 means 0x10000 bytes according to the documentation, so:
	if (TransferLength32 > 0x10000) return 0xFF;
	if (ScsiCmdLength > 20) return 0xFF;

	USHORT TransferLength = TransferLength32;
	if (s_Mesh->ScsiStatusHigh || s_Mesh->ScsiStatusLow) mesh_reset();

	// Set the target id.
	s_Mesh->DestID = TargetId & 7;
	SCSI_DEBUG("target ID = %d", TargetId & 7);

	// Open Firmware driver does copy the provided cmd, into its own static buffer, and sets up the LUN there.
	// We won't bother, assume caller does set up the LUN correctly.

	// Initialise the timeout here, single 15 second timeout for a single command.
	// BUGBUG: what does linux / darwin do?
	mesh_do_timeout(true);

	// Arbitrate the bus
	s_Mesh->Command = SEQ_CMD_ARB;

	if (!mesh_wait_op_done()) {
		SCSI_DEBUG("bus arbitration failed");
		mesh_reset();
		return 0xFF;
	}

	// Select.
	s_Mesh->Command = SEQ_CMD_SEL;

	if (!mesh_wait_op_done()) {
		if ((s_Mesh->Exception & EXC_SELECTIONTIMEOUT) != 0) {
			SCSI_DEBUG("bus selection timed out");
			mesh_clear_interrupts();
			s_Mesh->Command = SEQ_CMD_BUSFREE;
			mesh_wait_op_done();
			mesh_clear_interrupts();
			return 0xFF;
		}
	}

	mesh_set_tx_count(ScsiCmdLength);
	s_Mesh->Command = SEQ_CMD_CMD;
	mesh_write_fifo(ScsiCmd, ScsiCmdLength);
	bool Done = false;
	UCHAR Msg = 0;
	ULONG TxCount = 0;
	while (!Done) {
		SCSI_DEBUG("cmd: %x", s_Mesh->Command);
		bool success = mesh_wait_op_done();
		UCHAR LastCmd = 0;
		if (!success) {
			if ((s_Mesh->Interrupt & INT_EXCEPTION) != 0) {
				if ((s_Mesh->Exception & EXC_PHASEMISMATCH) != 0) {
					mesh_clear_interrupts();
					s_Mesh->Command = SEQ_CMD_BUSFREE;
					mesh_wait_op_done();
					mesh_clear_interrupts();
					if ((s_Mesh->ScsiStatusLow & (STSLOW_MSG | STSLOW_CD | STSLOW_IO)) == (STSLOW_IO | STSLOW_CD)) {
						LastCmd = SEQ_CMD_DATAIN;
					}
					else {
						SCSI_DEBUG("unexpected bus state");
						// unexpected bus state
						return 0;
					}
				}
				else {
					SCSI_DEBUG("other exception");
					// other exception
					return 0;
				}
			}
			else {
				SCSI_DEBUG("error");
				// not exception, must be error
				return 0;
			}
		}
		else {
			LastCmd = s_Mesh->Command;
		}

		LastCmd &= SEQ_MASKCMD;

		switch (LastCmd) {
		case SEQ_CMD_CMD:
			if (Buffer != NULL && TransferLength32 != 0) {
				// Open Firmware driver uses DMA here, we'll use PIO.
				if (Write) {
					mesh_set_tx_count(TransferLength);
					s_Mesh->Command = SEQ_CMD_DATAOUT;
					mesh_write_fifo(TransferBuffer, TransferLength);
				}
				else {
					// for data in i think we have to just keep going filling the FIFO buffer manually when not using DMA
					// MESH specs for Hydra says 16-byte FIFO
					// No idea if it's different for heathrow but for compatibility (GC/OHare uses same SCSI block as Hydra) just use that
					// Ensure the FIFO is clear first

					TxCount = TransferLength;
					if (TxCount > 0x10) TxCount = 0x10;
					mesh_set_tx_count(TxCount);
					s_Mesh->Command = SEQ_CMD_DATAIN;
				}
			}
			else {
				mesh_set_tx_count(1);
				s_Mesh->Command = SEQ_CMD_STATUS;
			}
			break;
		case SEQ_CMD_STATUS:
			mesh_set_tx_count(1);
			s_Mesh->Command = SEQ_CMD_MSGIN;
			break;
		case SEQ_CMD_DATAOUT:
			mesh_set_tx_count(1);
			s_Mesh->Command = SEQ_CMD_STATUS;
			break;
		case SEQ_CMD_DATAIN:
			mesh_read_fifo(Buffer, TxCount);
			TransferLength -= TxCount;
			Buffer += TxCount;
			if (TransferLength == 0) {
				mesh_set_tx_count(1);
				s_Mesh->Command = SEQ_CMD_STATUS;
			}
			else {
				TxCount = TransferLength;
				if (TxCount > 0x10) TxCount = 0x10;
				mesh_set_tx_count(TxCount);
				s_Mesh->Command = SEQ_CMD_DATAIN;
			}
			break;
		case SEQ_CMD_MSGIN:
			s_Mesh->Command = SEQ_CMD_BUSFREE;
			break;
		case SEQ_CMD_BUSFREE:
			mesh_read_fifo(Status, sizeof(*Status));
			mesh_read_fifo(&Msg, sizeof(Msg));
			Done = true;
			break;
		}
	}
	return 1;
}

static bool mesh_run_scsi_command_retry(UCHAR TargetId, PUCHAR ScsiCmd, ULONG ScsiCmdLength, PVOID TransferBuffer, ULONG TransferLength32, bool Write, PUCHAR Status) {
	for (ULONG i = 0; i < 8; i++) {
		UCHAR Ret = mesh_run_scsi_command(TargetId, ScsiCmd, ScsiCmdLength, TransferBuffer, TransferLength32, Write, Status);
		if (Ret == 1) return true;
		if (Ret == 0xFF) break;
	}
	return false;
}

static bool mesh_scsi_inquiry(UCHAR TargetId, UCHAR Lun, PUCHAR Buffer, UCHAR Length) {
	if (Lun >= 8) return false;
	UCHAR InquiryCmd[] = { 0x12, Lun << 5, 0x00, 0x00, Length, 0x00 };
	UCHAR Status = 0;
	return mesh_run_scsi_command_retry(TargetId, InquiryCmd, sizeof(InquiryCmd), Buffer, Length, false, &Status);
}

static bool mesh_read_sectors(PMESH_SCSI_DEVICE drive, ULONG block, PVOID dest, USHORT length) {
	UCHAR Read10Cmd[] = {
		0x28,
		drive->Lun << 5,
		block >> 24,
		block >> 16,
		block >> 8,
		block >> 0,
		0,
		length >> 8,
		length >> 0,
		0
	};

	UCHAR Status = 0;
	return mesh_run_scsi_command_retry(drive->TargetId, Read10Cmd, sizeof(Read10Cmd), dest, drive->BytesPerSector * length, false, &Status);
}

static bool mesh_write_sectors(PMESH_SCSI_DEVICE drive, ULONG block, PVOID dest, USHORT length) {
	UCHAR Write10Cmd[] = {
		0x2A,
		drive->Lun << 5,
		block >> 24,
		block >> 16,
		block >> 8,
		block >> 0,
		0,
		length >> 8,
		length >> 0,
		0
	};

	UCHAR Status = 0;
	return mesh_run_scsi_command_retry(drive->TargetId, Write10Cmd, sizeof(Write10Cmd), dest, drive->BytesPerSector * length, true, &Status);
}

PMESH_SCSI_DEVICE mesh_open_drive(UCHAR TargetId, UCHAR Lun) {
	for (PMESH_SCSI_DEVICE Device = s_FirstScsiDevice; Device != NULL; Device = Device->Next) {
		if (Device->TargetId != TargetId) continue;
		if (Device->Lun != Lun) continue;
		if (Device->IsCdRom) {
			// test unit ready
			UCHAR TestUnitReady[] = { 0x00, Lun << 5, 0x00, 0x00, 0x00, 0x00 };
			UCHAR Status = 0;
			if (!mesh_run_scsi_command_retry(TargetId, TestUnitReady, sizeof(TestUnitReady), NULL, 0, false, &Status)) return NULL;
			if (Status != 0) return NULL;

			// start/stop unit: start
			UCHAR StartStopUnit[] = { 0x1B, Lun << 5, 0x00, 0x00, 0x01, 0x00 };
			if (!mesh_run_scsi_command_retry(TargetId, StartStopUnit, sizeof(StartStopUnit), NULL, 0, false, &Status)) return NULL;

			// read capacity
			U32BE ReadCap10[2] = { {0},{0} };

			UCHAR ReadCapCmd[] = { 0x25, Lun << 5, 0, 0, 0, 0, 0, 0, 0, 0 };
			if (!mesh_run_scsi_command_retry(TargetId, ReadCapCmd, sizeof(ReadCapCmd), (PVOID)(ULONG)ReadCap10, sizeof(ReadCap10), false, &Status)) {
				Device->NumberOfSectors = 0x7FFFFFFF;
				Device->BytesPerSector = 2048;
				return Device;
			}
			Device->NumberOfSectors = ReadCap10[0].v;
			Device->BytesPerSector = ReadCap10[1].v;
		}
		return Device;
	}
	return NULL;
}

enum {
	READ16_MAXLEN = 0xFFFF
};

ULONG mesh_read_blocks(PMESH_SCSI_DEVICE drive, PVOID buffer, ULONG sector, ULONG count)
{
	ULONG n = count;
	PUCHAR dest = (PUCHAR)buffer;
	ULONG blk = sector;
	ULONG transferred = 0;

	while (n) {
		ULONG len = n;
		if (len > READ16_MAXLEN)
			len = READ16_MAXLEN;

		if (!mesh_read_sectors(drive, blk, dest, len)) {
			break;
		}

		transferred += len;
		dest += len * drive->BytesPerSector;
		n -= len;
		blk += len;
	}

	return transferred;
}

ULONG mesh_write_blocks(PMESH_SCSI_DEVICE drive, PVOID buffer, ULONG sector, ULONG count)
{
	ULONG n = count;
	PUCHAR dest = (PUCHAR)buffer;
	ULONG blk = sector;
	ULONG transferred = 0;

	while (n) {
		ULONG len = n;
		if (len > READ16_MAXLEN)
			len = READ16_MAXLEN;

		if (!mesh_write_sectors(drive, blk, dest, len)) {
			break;
		}

		dest += len * drive->BytesPerSector;
		transferred += len;
		n -= len;
		blk += len;
	}

	return transferred;
}

PMESH_SCSI_DEVICE mesh_get_first_device(void) {
	return s_FirstScsiDevice;
}

int mesh_init(uint32_t addr) {
	s_Mesh = (PMESH_REGISTERS)addr;

	// Reset the controller.
	mesh_reset();

	// Set the sourceID to SCSI id 7.
	s_Mesh->SourceID = 7;
	// Set the selection timeout.
	s_Mesh->SelTimeOut = 25;
	// Set the sync period.
	s_Mesh->SyncParams = 2;
	// Zero out the transfer count.
	mesh_set_tx_count(0);

	// Probe the scsi bus.

	PMESH_SCSI_DEVICE* DeviceEntry = &s_FirstScsiDevice;

	UCHAR InquiryData[0x24];


	U32BE ReadCap10[2];
	for (UCHAR target = 0; target < 8; target++) {
		ReadCap10[0].v = ReadCap10[1].v = 0;
		for (UCHAR lun = 0; lun < 8; lun++) {
			if (!mesh_scsi_inquiry(target, lun, InquiryData, sizeof(InquiryData))) {
				if (lun == 0) break; // if LUN 0 failed then there is no device on this scsi id
				continue;
			}

			// Ensure this is actually a block i/o device, we don't care about anything else
			UCHAR DevType = InquiryData[0];

			if (DevType != 0 && // disk
				DevType != 0x80 && // disk (not active)
				DevType != 4 && // WORM
				DevType != 5 && // ROM optical disc
				DevType != 7 // optical disc (writable?)
			) continue;

			ULONG NumberOfSectors = 0, SectorSize = 0;
			if ((DevType & 0x7F) != 0) {
				// This is a cdrom, don't bother
				NumberOfSectors = 0x7FFFFFFF;
				SectorSize = 2048;
			}
			else if (ReadCap10[0].v == 0) {
				// Send a read capacity(10) command to get the number of sectors and bytes per sector
				UCHAR ReadCapCmd[] = { 0x25, lun << 5, 0, 0, 0, 0, 0, 0, 0, 0 };
				UCHAR Status;
				if (!mesh_run_scsi_command_retry(target, ReadCapCmd, sizeof(ReadCapCmd), (PVOID)(ULONG)ReadCap10, sizeof(ReadCap10), false, &Status)) continue;
				NumberOfSectors = ReadCap10[0].v;
				SectorSize = ReadCap10[1].v;
			}

			// We got the inquiry data response, allocate a list entry for it
			PMESH_SCSI_DEVICE Device = (PMESH_SCSI_DEVICE)malloc(sizeof(MESH_SCSI_DEVICE));
			if (Device == NULL) return false;
			memset(Device, 0, sizeof(Device));
			Device->TargetId = target;
			Device->Lun = lun;
			Device->NumberOfSectors = NumberOfSectors;
			Device->BytesPerSector = SectorSize;
			Device->IsCdRom = (DevType & 0x7F) != 0;
			*DeviceEntry = Device;
			DeviceEntry = &Device->Next;
		}
	}
}
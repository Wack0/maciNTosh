// PXI drivers (CUDA + PMU)

#include <stdio.h>
#include <memory.h>
#include "arc.h"
#include "types.h"
#include "runtime.h"
#include "pxi.h"
#include "timer.h"

typedef volatile UCHAR PXI_REGISTER __attribute__((aligned(0x200)));

typedef struct _PXI_REGISTERS {
	PXI_REGISTER BufB;
	PXI_REGISTER BufAH;
	PXI_REGISTER DirB; // for each line, 1=output, 0=input
	PXI_REGISTER DirA; // for each line, 1=output, 0=input
	PXI_REGISTER T1C;
	PXI_REGISTER T1CH;
	PXI_REGISTER T1L;
	PXI_REGISTER T1LH;
	PXI_REGISTER T2C;
	PXI_REGISTER T2CH;
	PXI_REGISTER SR;
	PXI_REGISTER ACR;
	PXI_REGISTER rPCR;
	PXI_REGISTER IFR;
	PXI_REGISTER IER;
	PXI_REGISTER BufA;
} PXI_REGISTERS, * PPXI_REGISTERS;

_Static_assert(sizeof(PXI_REGISTERS) == 0x2000);

// PXI.ACR register bits
enum {
	PXI_ACR_PAENL = ARC_BIT(0), // PA enable latch
	PXI_ACR_PBENL = ARC_BIT(1), // PB enable latch

	PXI_ACR_SRMD1 = (1 << 2), // Shift in under control of T2C
	PXI_ACR_SRMD2 = (2 << 2), // Shift in under control of Phase 2
	PXI_ACR_SRMD3 = (3 << 2), // Shift in under control of external clock
	PXI_ACR_SRMD4 = (4 << 2), // Shift out free running at T2 rate
	PXI_ACR_SRMD5 = (5 << 2), // Shift out under control of T2
	PXI_ACR_SRMD6 = (6 << 2), // Shift out under control of theta2
	PXI_ACR_SRMD7 = (7 << 2), // Shift out under control of external clock

	PXI_ACR_SRMMASK = PXI_ACR_SRMD7, // Shift register mask.	

	PXI_ACR_T2CD = ARC_BIT(5), // Timer 2 count down with pulses on PB6
	PXI_ACR_T1CONT = ARC_BIT(6), // Timer 1 continuous counting
	PXI_ACR_T1PB7 = ARC_BIT(7), // Timer 1 drives PB7

};

// PXI.IER / PXI.IFR register bits
enum {
	PXI_IE_CA2 = ARC_BIT(0), // Interrupt on CA2
	PXI_IE_CA1 = ARC_BIT(1), // Interrupt on CA1
	PXI_IE_SR = ARC_BIT(2), // Interrupt on shift register
	PXI_IE_CB2 = ARC_BIT(3), // Interrupt on CB2
	PXI_IE_CB1 = ARC_BIT(4), // Interrupt on CB1
	PXI_IE_TIM2 = ARC_BIT(5), // Interrupt on timer 2
	PXI_IE_TIM1 = ARC_BIT(6), // Interrupt on timer 1
	PXI_IE_SET = ARC_BIT(7), // Set / ack interrupt bits.
};

// PXI port bits
enum {
	PXI_PORT_P0 = ARC_BIT(0),
	PXI_PORT_P1 = ARC_BIT(1),
	PXI_PORT_P2 = ARC_BIT(2),
	PXI_PORT_P3 = ARC_BIT(3),
	PXI_PORT_P4 = ARC_BIT(4),
	PXI_PORT_P5 = ARC_BIT(5),
	PXI_PORT_P6 = ARC_BIT(6),
	PXI_PORT_P7 = ARC_BIT(7),

	PXI_PORT_REQ = PXI_PORT_P4,
	PXI_PORT_ACK = PXI_PORT_P3,

	PXI_PORT_REQ_CUDA = PXI_PORT_P3,
	PXI_PORT_ACK_CUDA = PXI_PORT_P4,
	PXI_PORT_TIP_CUDA = PXI_PORT_P5,

	PXI_PORT_RX_CUDA = PXI_PORT_TIP_CUDA | PXI_PORT_ACK_CUDA
};

// PMU commands
enum {
	PMU_SET_ADB_CMD = 0x20, // Set New Apple Desktop Bus Command : variable in, zero out
	PMU_ADB_AUTO_ABORT = 0x21, // ADB Autopoll Abort : zero in, zero out
	PMU_RTC_WRITE = 0x30, // RTC write : 4 in, zero out
	PMU_RTC_READ = 0x38, // RTC read : zero in, 4 out
	PMU_SET_CONSTRAST = 0x40, // Set LCD contrast : 1 in, 0 out
	PMU_SET_BRIGHTNESS = 0x41, // Set LCD brightness : 1 in, 0 out
	PMU_SET_INT = 0x70, // Set Interrupt Mask : 1 in, 0 out
	PMU_READ_IF = 0x78, // Read Interrupt Flag : zero in, variable out
	PMU_POWEROFF_PPC = 0x7E, // Power off powerpc cpu (shut down system) : 4 in, 1 out  (in must be 'M','A','T','T')
	PMU_RESET_PPC = 0xD0, // Reset powerpc cpu (reboot system) : zero in, zero out
	PMU_SYSTEM_READY = 0xDF, // System ready : variable in, variable out

};

// CUDA commands
enum {
	CUDA_TYPE_ADB = 0,
	CUDA_TYPE_CMD = 1,

	CUDA_RTC_READ = 0x03,
	CUDA_RTC_WRITE = 0x09,
	CUDA_POWEROFF_PPC = 0x0a,
	CUDA_RESET_PPC = 0x11
};

enum {
	PMU_INT_PCMCIA = ARC_BIT(2),
	PMU_INT_VOL_BRIGHT = ARC_BIT(3),
	PMU_INT_ADB = ARC_BIT(4),
	PMU_INT_BATTERY = ARC_BIT(5),
	PMU_INT_ENV = ARC_BIT(6),
	PMU_INT_TICK = ARC_BIT(7),

	PMU_INTS_TO_ENABLE = PMU_INT_TICK | PMU_INT_ENV | PMU_INT_PCMCIA | PMU_INT_VOL_BRIGHT | PMU_INT_BATTERY | PMU_INT_ENV
};

static PPXI_REGISTERS s_PxiRegs = NULL;
static bool s_PxiIsCuda = false;

static bool PxipIsBusy(void) {
	if (s_PxiIsCuda) return (s_PxiRegs->IFR & PXI_IE_SR) == 0;
	return (s_PxiRegs->BufB & PXI_PORT_ACK) == 0;
}

static void PxipNotifyMcu(bool Value) {
	UCHAR BufB = s_PxiRegs->BufB;
	MmioWrite8(&s_PxiRegs->BufB, Value ? BufB & ~PXI_PORT_REQ : BufB | PXI_PORT_REQ);
}

static void PxipNotifyMcuTip(bool Value) {
	if (Value) MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB | PXI_PORT_ACK_CUDA);
	UCHAR BufB = s_PxiRegs->BufB;
	MmioWrite8(&s_PxiRegs->BufB, Value ? BufB & ~PXI_PORT_TIP_CUDA : BufB | PXI_PORT_TIP_CUDA);
}

static void PxipNotifyMcuRx(bool Value) {
	UCHAR BufB = s_PxiRegs->BufB;
	MmioWrite8(&s_PxiRegs->BufB, Value ? BufB | PXI_PORT_RX_CUDA : BufB & ~PXI_PORT_RX_CUDA);
}

static void PxipSetAcrSrm(UCHAR Value) {
	MmioWrite8(&s_PxiRegs->ACR, (s_PxiRegs->ACR & ~PXI_ACR_SRMMASK) | Value);
}

static bool PxipCudaFinished(void) {
	if (!s_PxiIsCuda) return false;
	return (s_PxiRegs->BufB & PXI_PORT_REQ_CUDA) != 0;
}

static void PxipSetAcrOut(void) {
	PxipSetAcrSrm(PXI_ACR_SRMD7);
}

static void PxipSetAcrIn(void) {
	PxipSetAcrSrm(PXI_ACR_SRMD3);
}

static void PxipWriteByte(UCHAR Data) {
	if (!s_PxiIsCuda) {
		PxipSetAcrOut();
		MmioWrite8(&s_PxiRegs->SR, Data);
		// Tell PXI we provided some data
		PxipNotifyMcu(true);
		// Wait for PXI to respond
		while (!PxipIsBusy()) {}
		PxipNotifyMcu(false);
		while (PxipIsBusy()) {}
		PxipNotifyMcu(false);
		return;
	}

	MmioWrite8(&s_PxiRegs->SR, Data);
	MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB ^ PXI_PORT_ACK_CUDA);
	while (PxipIsBusy()) {}
}

static void PxipWriteTxStart(UCHAR Data) {
	if (!s_PxiIsCuda) {
		// Wait for PXI to accept our command
		while (PxipIsBusy()) {}
		// Send it
		PxipWriteByte(Data);
		return;
	}

	PxipSetAcrOut();
	MmioWrite8(&s_PxiRegs->SR, Data);
	PxipNotifyMcuTip(true);
	while (PxipIsBusy()) {}
}

static void PxipWriteTxEnd(void) {
	if (!s_PxiIsCuda) return;


	PxipSetAcrIn();
	// Clear interrupt?
	(void)s_PxiRegs->SR;
	// Tell PXI we're waiting
	PxipNotifyMcuRx(true);
}

static UCHAR PxipReadByte(void) {
	if (!s_PxiIsCuda) {
		PxipSetAcrIn();
		// Poke the data register for some reason
		(void)s_PxiRegs->SR;
		// Tell PXI we're waiting
		PxipNotifyMcu(true);
		// Wait for PXI to respond
		while (!PxipIsBusy()) {}
		PxipNotifyMcu(false);
		while (PxipIsBusy()) {}
		return s_PxiRegs->SR;
	}

	MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB ^ PXI_PORT_ACK_CUDA);
	while (PxipIsBusy()) {}
	return s_PxiRegs->SR;
}

static UCHAR PxipReadTxStart(void) {
	if (!s_PxiIsCuda) return PxipReadByte();

	while (PxipIsBusy()) {}

	// Clear interrupt
	(void)s_PxiRegs->SR;

	PxipNotifyMcuTip(true);

	while (PxipIsBusy()) {}

	return s_PxiRegs->SR;
}

static void PxipReadTxEnd(void) {
	if (!s_PxiIsCuda) return;

	// Tell PXI we're waiting
	PxipNotifyMcuRx(true);

	while (PxipIsBusy()) {}

	// Clear interrupt
	(void)s_PxiRegs->SR;
}

UCHAR PxiSendSyncRequest(UCHAR Command, PUCHAR Arguments, UCHAR ArgLength, BOOLEAN VariadicIn, PUCHAR Response, UCHAR ResponseLength, BOOLEAN VariadicOut) {

	// Send it
	PxipWriteTxStart(Command);

	// If caller says this command is variadic in, send the length
	if (VariadicIn) PxipWriteByte(ArgLength);

	// Send the provided args
	for (int i = 0; i < ArgLength; i++) PxipWriteByte(Arguments[i]);

	PxipWriteTxEnd();

	// If command responds with data, then read it all
	bool TxInProgress = false;
	UCHAR RealResponseLength = ResponseLength;
	if (ResponseLength || VariadicOut) {
		if (VariadicOut) {
			RealResponseLength = PxipReadTxStart();
			TxInProgress = true;
		}
		if (Response == NULL) ResponseLength = 0;
		for (int i = 0; i < RealResponseLength; i++) {
			UCHAR Data = TxInProgress ? PxipReadByte() : PxipReadTxStart();
			TxInProgress = true;
			if (i < ResponseLength) Response[i] = Data;
			if (PxipCudaFinished()) {
				RealResponseLength = i + 1;
				break;
			}
		}
		PxipReadTxEnd();
	}

	return RealResponseLength;
}

void PxiInit(PVOID MmioBase, bool IsCuda) {
	s_PxiIsCuda = IsCuda;
	s_PxiRegs = (PPXI_REGISTERS)MmioBase;

	if (s_PxiIsCuda) {
		MmioWrite8(&s_PxiRegs->DirB, (s_PxiRegs->DirB & ~PXI_PORT_REQ_CUDA) | PXI_PORT_ACK_CUDA | PXI_PORT_TIP_CUDA);
#if 1
		MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB | PXI_PORT_RX_CUDA);
#else // following is what openbsd driver does:
		PxipSetAcrIn();
		MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB | PXI_PORT_RX_CUDA);
		s_PxiRegs->SR; // clear int
		MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB | PXI_PORT_RX_CUDA);
		udelay(150);
		PxipNotifyMcuTip(true);
		udelay(150);
		MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB ^ PXI_PORT_ACK_CUDA);
		udelay(150);
		PxipNotifyMcuTip(false);
		udelay(150);
		MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB | PXI_PORT_RX_CUDA);
		s_PxiRegs->SR; // clear int
#endif

		// turn off autopoll
		UCHAR Cmd[2] = { 1, 0 };
		UCHAR Out[3];
		PxiSendSyncRequest(CUDA_TYPE_CMD, Cmd, sizeof(Cmd), false, Out, sizeof(Out), false);

		return;
	}

	MmioWrite8(&s_PxiRegs->BufB, s_PxiRegs->BufB | PXI_PORT_REQ);
	MmioWrite8(&s_PxiRegs->DirB, (s_PxiRegs->DirB & ~PXI_PORT_ACK) | PXI_PORT_REQ);
	// Disable all interrupts then enable only CB1.
	MmioWrite8(&s_PxiRegs->IER, ~(PXI_IE_CB1 | PXI_IE_SR | PXI_IE_SET));
	MmioWrite8(&s_PxiRegs->IER, PXI_IE_CB1 | PXI_IE_SR | PXI_IE_SET);



#if 0
	// Tell PMU we are ready.
	{
		UCHAR Arg = 2;
		PxiSendSyncRequest(PMU_SYSTEM_READY, &Arg, sizeof(Arg), true, NULL, 0, true);
	}
#endif

	// turn off autopoll
	PxiSendSyncRequest(PMU_ADB_AUTO_ABORT, NULL, 0, false, NULL, 0, false);

	// Enable PXI interrupts.
	{
		UCHAR Arg = PMU_INT_ADB;
		PxiSendSyncRequest(PMU_SET_INT, &Arg, sizeof(Arg), false, NULL, 0, false);
	}

	// Ack any existing interrupts.
	{
		UCHAR OutBuffer[16];
		while ((s_PxiRegs->IFR & PXI_IE_CB1) != 0) {
			MmioWrite8(&s_PxiRegs->IFR, PXI_IE_CB1 | PXI_IE_SET);
			PxiSendSyncRequest(PMU_READ_IF, NULL, 0, false, OutBuffer, 0x10, true);
		}
	}

	// PXI is initialised, commands can be sent now.
	// This is PMU and so must be a laptop (given that this is grackle)
	// Set the lcd brightness and constrast to the "best" settings as says openpmu.
	{
		UCHAR Arg = 14;
		PxiSendSyncRequest(PMU_SET_BRIGHTNESS, &Arg, sizeof(Arg), false, NULL, 0, false);
		Arg = 127;
		PxiSendSyncRequest(PMU_SET_CONSTRAST, &Arg, sizeof(Arg), false, NULL, 0, false);
	}
}

ULONG PxiRtcRead(void) {
	U32BE Ret;

	if (s_PxiIsCuda) {
		UCHAR Cmd = CUDA_RTC_READ;
		PxiSendSyncRequest(CUDA_TYPE_CMD, &Cmd, sizeof(Cmd), false, (PUCHAR)(ULONG)&Ret, sizeof(Ret), false);
	}
	else {
		PxiSendSyncRequest(PMU_RTC_READ, NULL, 0, false, (PUCHAR)(ULONG)&Ret, sizeof(Ret), false);
	}

	return Ret.v;
}

void PxiRtcWrite(ULONG Value) {
	U32BE Data;
	Data.v = Value;

	if (s_PxiIsCuda) {
		UCHAR Cmd[5] = { CUDA_RTC_READ };
		memcpy(&Cmd[1], &Data, sizeof(Data));
		UCHAR Out[3];
		PxiSendSyncRequest(CUDA_TYPE_CMD, &Cmd, sizeof(Cmd), false, Out, sizeof(Out), false);
		return;
	}

	PxiSendSyncRequest(PMU_RTC_WRITE, (PUCHAR)(ULONG)&Data, sizeof(Data), false, NULL, 0, false);
}

void PxiPowerOffSystem(bool Reset) {
	// Power off or reset system.
	if (Reset) {
		if (s_PxiIsCuda) {
			UCHAR Cmd = CUDA_RESET_PPC;
			UCHAR Out[16];
			PxiSendSyncRequest(CUDA_TYPE_CMD, &Cmd, sizeof(Cmd), false, Out, sizeof(Out), false);
		}
		else {
			PxiSendSyncRequest(PMU_RESET_PPC, NULL, 0, false, NULL, 0, false);
		}
	}
	else {
		if (s_PxiIsCuda) {
			UCHAR Cmd = CUDA_POWEROFF_PPC;
			UCHAR Out[16];
			PxiSendSyncRequest(CUDA_TYPE_CMD, &Cmd, sizeof(Cmd), false, Out, sizeof(Out), false);
		}
		else {
			UCHAR Args[] = { 'M','A','T','T' };
			PxiSendSyncRequest(PMU_POWEROFF_PPC, Args, sizeof(Args), false, NULL, 1, false);
		}
	}
	// Should not reach here.
	while (1) {}
}

ULONG PxiAdbCommand(PUCHAR Command, ULONG Length, PUCHAR Response) {
	if (s_PxiIsCuda) {
		UCHAR Buffer[16];
		UCHAR Offset = 0;
		UCHAR RespLen = PxiSendSyncRequest(CUDA_TYPE_ADB, Command, Length, false, Buffer, sizeof(Buffer), false);
		if (RespLen > 1 && Buffer[0] == CUDA_TYPE_ADB) {
			if (RespLen > 2 && Buffer[2] == Command[0]) {
				Offset = 3;
			}
			else {
				Offset = 2;
			}
		}
		else {
			Offset = 1;
		}
		if (RespLen >= Offset) RespLen -= Offset;
		else RespLen = 0;
		memcpy(Response, &Buffer[Offset], RespLen);
		return RespLen;
	}

	if (Length == 0) return 0;

	UCHAR Buffer[128] = { Command[0], 0, Length - 1 };
	memcpy(&Buffer[3], &Command[1], Length - 1);
	PxiSendSyncRequest(PMU_SET_ADB_CMD, Buffer, Length + 2, true, NULL, 0, false);
	UCHAR RespLen = 0;
	// Apple's polling implementation in OF does this kind of thing.
	for (;;) {
		// Wait for and ack the interrupt.
		while ((s_PxiRegs->IFR & (PXI_IE_CB1 | PXI_IE_SR)) == 0);
		MmioWrite8(&s_PxiRegs->IFR, PXI_IE_CB1 | PXI_IE_SET);
		UCHAR RespLen = PxiSendSyncRequest(PMU_READ_IF, NULL, 0, false, Buffer, 0x10, true);
		//printf("[PXI_ADB]: %02x - %d %02x %02x %02x\r\n", Command[0], RespLen, Buffer[0], Buffer[1], Buffer[2]);
		if (RespLen < 2) continue;
		if ((Buffer[0] & PMU_INT_ADB) == 0) continue;
		memcpy(Response, &Buffer[1], RespLen - 2);
		return RespLen - 2;
	}
}
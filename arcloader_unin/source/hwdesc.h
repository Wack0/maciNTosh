#pragma once

enum { // MRF = "MacRISC Flag"
	MRF_IN_EMULATOR = ARC_BIT(0),
	MRF_VIA_IS_CUDA = ARC_BIT(1),
};

typedef struct _HW_DESCRIPTION {
	ULONG MemoryLength; // Length of physical memory.
	ULONG MacIoStart; // Base address of Mac I/O controller.
	ULONG DecrementerFrequency; // Decrementer frequency.
	ULONG MrFlags; // MRF_* bit flags.
	ULONG UsbOhciStart[2]; // Base address of USB controller(s).
	ULONG Pci1SlotInterrupts[32 / 2]; // Packed PCI1 slot interrupts.
	ULONG MioAta6Start[2]; // Base address of the Mac I/O-style ATA-6 controller, if it is present.
	// Some systems have two ata-6 controllers (xserve with two "applekiwi" controllers with ata-6 on each)

	// Framebuffer details.
	ULONG FrameBufferBase; // Base address of frame buffer.
	//ULONG FrameBufferLength; // Length of frame buffer in video RAM. (unneeded, can be calculated later by height * stride)
	ULONG FrameBufferWidth; // Display width
	ULONG FrameBufferHeight; // Display height
	ULONG FrameBufferStride; // Number of bytes per line.
	
	// Image details.
	ULONG BootImgBase; // Base address of boot.img
	ULONG BootImgSize; // Size of boot.img
	ULONG PtdrSize; // size of ATA.ptDR (located after boot.img)
	ULONG WikiSize; // size of ATA.wiki (located after ptdr)
	ULONG DriversImgBase; // Base address of drivers.img
	ULONG DriversImgSize; // Size of drivers.img
} HW_DESCRIPTION, *PHW_DESCRIPTION;

_Static_assert(sizeof(HW_DESCRIPTION) % 8 == 0, "HW_DESCRIPTION struct must be a multiple of 8 bytes");
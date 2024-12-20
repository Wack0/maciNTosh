#pragma once

enum { // MRF = "MacRISC Flag"
	MRF_IN_EMULATOR = ARC_BIT(0),
	MRF_VIA_IS_CUDA = ARC_BIT(1),
	MRF_OLD_WORLD = ARC_BIT(2),
};

typedef struct _HW_DESCRIPTION {
	ULONG MemoryLength; // Length of physical memory.
	ULONG MacIoStart; // Base address of Mac I/O controller.
	ULONG DecrementerFrequency; // Decrementer frequency.
	ULONG MrFlags; // MRF_* bit flags.
	ULONG UsbOhciStart[2]; // Base address of USB controller(s).

	// Framebuffer details.
	ULONG FrameBufferBase; // Base address of frame buffer.
	//ULONG FrameBufferLength; // Length of frame buffer in video RAM. (unneeded, can be calculated later by height * stride)
	ULONG FrameBufferWidth; // Display width
	ULONG FrameBufferHeight; // Display height
	ULONG FrameBufferStride; // Number of bytes per line.
	
	// Ramdisk image details if loaded by stage1
	ULONG DriversImgBase; // Base address of drivers.img
	ULONG DriversImgSize; // Size of drivers.img
} HW_DESCRIPTION, *PHW_DESCRIPTION;
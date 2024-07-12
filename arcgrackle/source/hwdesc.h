#pragma once

enum { // MRP = "MacRISC Paddington"
	MRP_IN_EMULATOR = ARC_BIT(0),
	MRP_VIA_IS_CUDA = ARC_BIT(1),
};

typedef struct _HW_DESCRIPTION {
	ULONG MemoryLength; // Length of physical memory.
	ULONG MacIoStart; // Base address of Mac I/O controller.
	ULONG DecrementerFrequency; // Decrementer frequency.
	ULONG MrpFlags; // MRP_* bit flags.
	ULONG UsbOhciStart[2]; // Base address of USB controller(s).

	// Framebuffer details.
	ULONG FrameBufferBase; // Base address of frame buffer.
	//ULONG FrameBufferLength; // Length of frame buffer in video RAM. (unneeded, can be calculated later by height * stride)
	ULONG FrameBufferWidth; // Display width
	ULONG FrameBufferHeight; // Display height
	ULONG FrameBufferStride; // Number of bytes per line.
	
	// Boot device details.
	ULONG BootDevice;
	ULONG Padding;
} HW_DESCRIPTION, *PHW_DESCRIPTION;
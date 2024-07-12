#pragma once
#include "types.h"

typedef struct ARC_LE _MDL {
	ULONG Next;
	CSHORT Size;
	CSHORT MdlFlags;
	ULONG Process;
	ULONG MappedSystemVa;
	ULONG StartVa;
	ULONG ByteCount;
	ULONG ByteOffset;
} MDL, *PMDL;

typedef struct ARC_LE _IO_STATUS_BLOCK {
	ULONG Status;
	ULONG Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
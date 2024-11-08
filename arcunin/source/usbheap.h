#pragma once
#include <stdbool.h>

typedef struct _USB_HEAP_CHUNK {
	ULONG Status;
	ULONG Size;
	struct _USB_HEAP_CHUNK
		* Previous, * Next;
} USB_HEAP_CHUNK, *PUSB_HEAP_CHUNK;

typedef struct _USB_HEAP {
	PVOID Base;
	ULONG Size;
	PUSB_HEAP_CHUNK FreeList;
} USB_HEAP, *PUSB_HEAP;


bool UhCreate(PUSB_HEAP Heap, PVOID Ptr, ULONG Size);
void UhDelete(PUSB_HEAP Heap);
PVOID UhAlloc(PUSB_HEAP Heap, ULONG Size);
PVOID UhAllocAligned(PUSB_HEAP Heap, ULONG Size, ULONG Alignment);
void UhFree(PUSB_HEAP Heap, PVOID Ptr);

bool UhHeapInit(PVOID Ptr, ULONG Size);
PVOID UhHeapAlloc(ULONG Size);
PVOID UhHeapAllocAligned(ULONG Size, ULONG Alignment);
void UhHeapFree(PVOID Ptr);
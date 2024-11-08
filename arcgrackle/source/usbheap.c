#include <stdlib.h>
#include "types.h"
#include "arc.h"
#include "usbheap.h"

#include <stdio.h>

typedef enum {
	CHUNK_FREE,
	CHUNK_USED,
	CHUNK_ALIGNED
} USB_HEAP_CHUNK_STATUS;

enum {
	CACHE_ALIGN_SIZE = 32,
	CACHE_ALIGN_MASK = CACHE_ALIGN_SIZE - 1
};

static inline bool UhpChunkInHeap(PUSB_HEAP Heap, PUSB_HEAP_CHUNK Chunk) {
	return (Chunk >= Heap->Base && (ULONG)Chunk < ((ULONG)Heap->Base + Heap->Size));
}

static void UhpCombineChunks(PUSB_HEAP_CHUNK Chunk) {
	if (Chunk == NULL) return;
	ULONG NextFree = (ULONG)Chunk->Next;
	if (NextFree != ((ULONG)Chunk + Chunk->Size + sizeof(*Chunk))) return;
	
	PUSB_HEAP_CHUNK Next = Chunk->Next;
	Chunk->Next = Next->Next;
	if (Chunk->Next != NULL) {
		Chunk->Next->Previous = Chunk;
	}
	Chunk->Size += Next->Size + sizeof(*Next);
}

bool UhCreate(PUSB_HEAP Heap, PVOID Ptr, ULONG Size) {
	if (((ULONG)Ptr & CACHE_ALIGN_MASK) != 0) return false;
	
	Heap->Base = Ptr;
	Heap->Size = Size;
	Heap->FreeList = (PUSB_HEAP_CHUNK) Ptr;
	Heap->FreeList->Status = CHUNK_FREE;
	Heap->FreeList->Size = Size - sizeof(USB_HEAP_CHUNK);
	Heap->FreeList->Previous = Heap->FreeList->Next = NULL;
	
	return true;
}

void UhDelete(PUSB_HEAP Heap) {
	if (Heap == NULL) return;
	
	Heap->Base = NULL;
	Heap->Size = 0;
	Heap->FreeList = NULL;
	
}

static inline PUSB_HEAP_CHUNK UhpGetChunk(PVOID Ptr) {
	return (PUSB_HEAP_CHUNK) (
		(ULONG)Ptr - sizeof(USB_HEAP_CHUNK)
	);
}

static inline void UhpEnsureChunkLinkInHeap(PUSB_HEAP Heap, PUSB_HEAP_CHUNK Chunk, PUSB_HEAP_CHUNK Link) {
	if (!UhpChunkInHeap(Heap, Link)) {
		while (1);
	}
}

static PVOID UhpAllocLocked(PUSB_HEAP Heap, ULONG Size, ULONG Alignment) {
	if (Size == 0) Size = 1;
	if (Alignment == 0) return NULL;
	if (Alignment < CACHE_ALIGN_SIZE) Alignment = CACHE_ALIGN_SIZE;
	ULONG AlignMask = Alignment - 1;
	if ((Alignment & AlignMask) != 0) {
		Alignment = 1 << (32 - __builtin_clz(Alignment - 1));
		AlignMask = Alignment - 1;
	}
	
	// Align Size to a cache line.
	Size = (Size + CACHE_ALIGN_MASK) & ~CACHE_ALIGN_MASK;
	
	// Search the free list.
	PUSB_HEAP_CHUNK BestFit = NULL;
	for (PUSB_HEAP_CHUNK Chunk = Heap->FreeList; Chunk != NULL; Chunk = Chunk->Next) {
		ULONG Body = (ULONG)Chunk + sizeof(*Chunk);
		ULONG Extra = (Alignment - (Body & AlignMask)) & AlignMask;
		if (Extra == 0 && Chunk->Size == Size) {
			BestFit = Chunk;
			break;
		}
		ULONG Total = Size + Extra;
		if (Chunk->Size >= Total) {
			if (BestFit == NULL || Chunk->Size < BestFit->Size) {
				BestFit = Chunk;
				continue;
			}
		}
	}
	
	if (BestFit == NULL) {
		return NULL;
	}
	
	// Found a chunk
	ULONG ChunkBody = (ULONG)BestFit + sizeof(*BestFit);
	ULONG Extra = (Alignment - (ChunkBody & AlignMask)) & AlignMask;
	
	// Split the chunk if size is larger than what is wanted
	ULONG WantedSize = Size + Extra + sizeof(*BestFit);
	if (BestFit->Size > WantedSize) {
		PUSB_HEAP_CHUNK New = (PUSB_HEAP_CHUNK)(
			(PUCHAR)BestFit + WantedSize
		);
		New->Status = CHUNK_FREE;
		New->Size = BestFit->Size - WantedSize;
		New->Next = BestFit->Next;
		if (New->Next != NULL) {
			New->Next->Previous = New;
		}
		BestFit->Next = New;
		BestFit->Size = Size + Extra;
	}
	
	BestFit->Status = CHUNK_USED;
	if (BestFit->Previous != NULL) {
		UhpEnsureChunkLinkInHeap(Heap, BestFit, BestFit->Previous);
		BestFit->Previous->Next = BestFit->Next;
	} else {
		Heap->FreeList = BestFit->Next;
	}
	
	if (BestFit->Next != NULL) {
		UhpEnsureChunkLinkInHeap(Heap, BestFit, BestFit->Next);
		BestFit->Next->Previous = BestFit->Previous;
	}
	BestFit->Previous = BestFit->Next = NULL;
	
	PVOID Body = (PUCHAR)BestFit + Extra + sizeof(*BestFit);
	
	if (Extra != 0) {
		PUSB_HEAP_CHUNK ExtraChunk = UhpGetChunk(Body);
		ExtraChunk->Status = CHUNK_ALIGNED;
		ExtraChunk->Previous = BestFit;
	}
	
	return Body;
}

static PVOID UhpAlloc(PUSB_HEAP Heap, ULONG Size, ULONG Alignment) {
	// Allocate the memory
	PVOID Buffer = UhpAllocLocked(Heap, Size, Alignment);
	
	// Return.
	return Buffer;
}

static void UhpFreeLocked(PUSB_HEAP Heap, PVOID Ptr) {
	// Check pointer is within heap bounds
	ULONG HeapStart = (ULONG)Heap->Base + sizeof(USB_HEAP_CHUNK);
	ULONG HeapEnd = (ULONG)Heap->Base + Heap->Size;
	if ((ULONG)Ptr < HeapStart || (ULONG)Ptr > HeapEnd) return;
	
	// Grab the memchunk header
	PUSB_HEAP_CHUNK Chunk = UhpGetChunk(Ptr);
	
	// Ensure it's actually in use.
	if (Chunk->Status == CHUNK_ALIGNED) {
		Chunk = Chunk->Previous;
	}
	if (Chunk->Status != CHUNK_USED) return;
	
	Chunk->Status = CHUNK_FREE;
	
	// Find previous free chunk.
	PUSB_HEAP_CHUNK PreviousChunk = Heap->FreeList;
	for (; PreviousChunk != NULL; PreviousChunk = PreviousChunk->Next) {
		if (PreviousChunk->Next == NULL) break;
		if (PreviousChunk->Next > Chunk) break;
	}
	
	if (PreviousChunk != NULL && Chunk > PreviousChunk) {
		// Add chunk to free list
		Chunk->Previous = PreviousChunk;
		Chunk->Next = PreviousChunk->Next;
		PreviousChunk->Next = Chunk;
	} else {
		// Set Chunk as the first entry in the free list
		Chunk->Next = Heap->FreeList;
		Heap->FreeList = Chunk;
		Chunk->Previous = NULL;
	}
	
	if (Chunk->Next != NULL) {
		UhpEnsureChunkLinkInHeap(Heap, Chunk, Chunk->Next);
		Chunk->Next->Previous = Chunk;
	}
	
	// Combine any chunks that can be combined.
	UhpCombineChunks(Chunk);
	UhpCombineChunks(Chunk->Previous);
}

PVOID UhAlloc(PUSB_HEAP Heap, ULONG Size) {
	return UhpAlloc(Heap, Size, CACHE_ALIGN_SIZE);
}

PVOID UhAllocAligned(PUSB_HEAP Heap, ULONG Size, ULONG Alignment) {
	return UhpAlloc(Heap, Size, Alignment);
}

void UhFree(PUSB_HEAP Heap, PVOID Ptr) {
	// Free the memory
	UhpFreeLocked(Heap, Ptr);
}


static USB_HEAP s_UsbHeap;

bool UhHeapInit(PVOID Ptr, ULONG Size) {
	ULONG Ptr32 = (ULONG)Ptr;
	// convert pointer to uncached
	Ptr32 -= 0x80000000;
	Ptr32 += 0x90000000;
	return UhCreate(&s_UsbHeap, (PVOID)Ptr32, Size);
}

PVOID UhHeapAlloc(ULONG Size) {
	return UhAlloc(&s_UsbHeap, Size);
}

PVOID UhHeapAllocAligned(ULONG Size, ULONG Alignment) {
	return UhAllocAligned(&s_UsbHeap, Size, Alignment);
}

void UhHeapFree(PVOID Ptr) {
	return UhFree(&s_UsbHeap, Ptr);
}
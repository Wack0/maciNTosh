#pragma once

typedef struct _ARC_MEMORY_CHUNK ARC_MEMORY_CHUNK, *PARC_MEMORY_CHUNK;

struct _ARC_MEMORY_CHUNK {
	MEMORY_DESCRIPTOR Descriptor;
	PARC_MEMORY_CHUNK Next;
	PARC_MEMORY_CHUNK Prev;
};

/// <summary>
/// Obtains the free chunk including a set of pages.
/// </summary>
/// <param name="BasePage">Base page to search for.</param>
/// <param name="LengthPages">Number of pages of the allocation.</param>
/// <returns>Free memory chunk suitable for ArcMemAllocateFromFreeChunk, or NULL if not found.</returns>
PMEMORY_DESCRIPTOR ArcMemFindChunk(ULONG BasePage, ULONG LengthPages);

/// <summary>
/// Given a free memory chunk, allocate an area of memory from it.
/// </summary>
/// <param name="Desc">Chunk to allocate from</param>
/// <param name="BasePage">Base page</param>
/// <param name="PageCount">Page count</param>
/// <param name="MemoryType">Memory type</param>
/// <remarks>Sub-chunks are carved out around the passed in descriptor, which will be the allocated sub-chunk on function success.</remarks>
/// <returns>ARC status code.</returns>
ARC_STATUS ArcMemAllocateFromFreeChunk(PMEMORY_DESCRIPTOR Desc, ULONG BasePage, ULONG PageCount, MEMORY_TYPE MemoryType);

/// <summary>
/// Allocates a chunk of memory to use during boot. The chunk is configured as FirmwareTemporary.
/// </summary>
/// <param name="length">Length to allocate in bytes.</param>
/// <returns>Pointer to allocated memory.</returns>
PVOID ArcMemAllocTemp(size_t length);

/// <summary>
/// Allocates a chunk of memory to use for hardware purposes. The chunk is configured as FirmwarePermanent so NT doesn't clobber it.
/// </summary>
/// <param name="length">Length to allocate in bytes.</param>
/// <returns>Pointer to allocated memory.</returns>
PVOID ArcMemAllocDirect(size_t length);

/// <summary>
/// Initialise the default memory descriptors so ArcMemAllocFromDdrDirect can work.
/// </summary>
int ArcMemInitDescriptors(ULONG PhysicalMemorySize);

void ArcMemInit(void);
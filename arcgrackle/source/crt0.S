#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.text

	.extern FwMain
	.extern __executable_start
	.globl _start
_start:
// We are currently in little endian mode,
// in non-translated mode,
// with interrupts disabled.
	// quick debug test. are we getting here?
	// r28=ffff_ffff, r30=physaddr of framebuffer
	//stw r28, 0(r30)
	//stw r28, 4(r30)
	//stw r28, 8(r30)
	//stw r28, 0xc(r30)
	
// r3 is the physical address of our hardware description struct.
// Do not bother touching it right now.
// In fact, save it in a higher register:
	mr r31, r3

// Following init code comes from libogc:

	// clear all BATs
	li		r0,0
	mtspr	IBAT0U,r0; mtspr	IBAT1U,r0; mtspr	IBAT2U,r0; mtspr	IBAT3U,r0		// IBAT0...3
	mtspr	DBAT0U,r0; mtspr	DBAT1U,r0; mtspr	DBAT2U,r0; mtspr	DBAT3U,r0		// DBAT0...3
	isync
	
	// Invalidate all TLBs
	// Comes from mario kart wii forum - ppc pagetable tutorial
	// Open Firmware used pagetables so TLBs have been used so invalidate them:
	li r0,64
	li r3,0
	// Wipe SDR1 here:
	sync
	mtspr 25, r3
	isync
	
	mtctr r0
invalidate_tlb_loop:
	tlbie r3
	addi r3, r3, 0x1000
	bdnz+ invalidate_tlb_loop
after_invalidate_tlb:
	tlbsync
	
	// clear all SRs
	lis		r0,0x8000
	mtsr	0,r0; mtsr	1,r0; mtsr	2,r0; mtsr	3,r0; mtsr	4,r0; mtsr	5,r0; mtsr	6,r0
	mtsr	7,r0; mtsr	8,r0; mtsr	9,r0; mtsr	10,r0; mtsr	11,r0; mtsr	12,r0; mtsr	13,r0
	mtsr	14,r0; mtsr	15,r0
	isync
	
	// set DBAT0 and IBAT0:
	// 0x8000_0000 + 256MB => physical 0x0000_0000 (cached, r+w)
	li		r3,2
	lis		r4,0x8000
	ori		r4,r4,0x1fff
	mtspr	IBAT0L,r3
	mtspr	IBAT0U,r4
	mtspr	DBAT0L,r3
	mtspr	DBAT0U,r4
	isync
	
	// Grackle can in theory address up to 1GB, but that would use all available BATs.
	// A BAT has been set to map the first 256MB, that should be more than enough for ARC firmware purposes.
	// When the ARC firmware memory map, just set any memory above 256MB if present as firmware temporary,
	// In this case, boot loaders will not use it; but NT kernel indeed can.
	
	// set DBAT1:
	// 0xc000_0000 + 256MB => physical 0x8000_0000 (uncached, r+w)
	lis r3, 0x8000
	ori r3, r3, 0x2a
	lis r4, 0xc000
	ori r4, r4, 0x1fff
	mtspr DBAT1L, r3
	mtspr DBAT1U, r4
	isync
	
	// set DBAT2:
	// 0xf000_0000 + 256MB => physical 0xf000_0000 (uncached, r+w)
	// Grackle PCI configuration space is here, map the entire 256MB.
	lis r3, 0xf000
	ori r3, r3, 0x2a
	lis r4, 0xf000
	ori r4, r4, 0x1fff
	mtspr DBAT2L, r3
	mtspr DBAT2U, r4
	isync

	// set DBAT3:
	// 0x9000_0000 + 256MB => physical 0x0000_0000 (uncached, r+w)
	li r3, 0x2a
	lis r4, 0x9000
	ori r4, r4, 0x1fff
	mtspr DBAT3L, r3
	mtspr DBAT3U, r4
	isync

	
	// set up a stack:
	// we are at 9MB, use ram before it for stack.
	// this way we can set up 1MB at 8MB as firmware temporary.
	lis r1, __executable_start@h
	ori r1, r1, __executable_start@l
	subi r1, r1, 8
	
	// switch into translated mode and jump to FwMain
	mr r3, r31
	oris r3, r3, 0x8000
	lis r5, FwMain@h
	ori r5, r5, FwMain@l
	mtsrr0 r5
	mfmsr r4
	ori r4, r4, MSR_DR|MSR_IR
	mtsrr1 r4
	rfi

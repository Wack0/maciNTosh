// Startup code.
// Exception handlers are adapted from OpenBIOS:
/*
 *   Creation Date: <2001/06/16 21:30:18 samuel>
 *   Time-stamp: <2003/04/04 16:32:06 samuel>
 *
 *	<init.S>
 *
 *	Asm glue for ELF images run inside MOL
 *
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

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
// Same for framebuffer physical address (top 4 bits):
	mr r30, r4

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

	// A BAT has been set to map the first 256MB, that should be more than enough for ARC firmware purposes.
	// When the ARC firmware memory map, just set any memory above 256MB if present as firmware temporary,
	// In this case, boot loaders will not use it; but NT kernel indeed can.

	// set DBAT1:
	// 0x7000_0000 + 256MB => physical 0x0000_0000 (uncached, r+w)
	li r3, 0x2a
	lis r4, 0x7000
	ori r4, r4, 0x1fff
	mtspr DBAT1L, r3
	mtspr DBAT1U, r4
	isync

	// set DBAT2:
	// 0x6000_0000 + 256MB => physical 0x8000_0000 (uncached, r+w)
	lis r3, 0x8000
	ori r3, r3, 0x2a
	lis r4, 0x6000
	ori r4, r4, 0x1fff
	mtspr DBAT2L, r3
	mtspr DBAT2U, r4
	isync
	
	// DBAT3 is used for whatever PCI access, set up by DSI exception handler.
	// The DSI exception handler currently only works on G4 systems.
	// Map the 256MB of physical memory containing the framebuffer using DBAT3.
	// Uni-North systems using PPC750 are Pismo plus a bunch of iBooks (clamshell, snow) only;
	// and only needs one additional PCI address space area mapped (for framebuffer) anyway,
	// so this is enough for PPC750 systems not to hit DSI at all.
	mr r3, r30
	ori r3, r3, 0x2a
	mr r4, r30
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
	
	// copy vector_dsi to 0x300
	li r0, (vector_dsi_end - vector_dsi) / 4
	lis r3, vector_dsi@h
	ori r3, r3, vector_dsi@l
	rlwinm r3, r3, 0, 1, 31
	li r4, 0x300
	bl copy_32
	
#if 0 // ISI isn't needed, we only want to map PCI address space for data, not for instruction.
	// copy vector_isi to 0x400
	li r0, (vector_isi_end - vector_isi) / 4
	lis r3, vector_isi@h
	ori r3, r3, vector_isi@l
	rlwinm r3, r3, 0, 1, 31
	li r4, 0x400
	bl copy_32
#endif
	
	
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

copy_32: // r0=length, r3=src, r4=dest
	mtctr r0
copy_loop:
	lwz r0,0(r3)
	stw r0,0(r4)
	addi r3,r3,4
	addi r4,r4,4
	bdnz+ copy_loop
	blr

// DSI and ISI exceptions
.extern dsi_exception
#if 0
.extern isi_exception
#endif

exception_return:
	addi	r1,r1,16	// pop ABI frame

	lwz	r0,52(r1)
	mtlr	r0
	lwz	r0,56(r1)
	mtcr	r0
	lwz	r0,60(r1)
	mtctr	r0
	lwz	r0,64(r1)
	mtxer	r0

	lwz	r0,0(r1)	// restore r0
	lwz	r2,8(r1)	// restore r2
	lwz	r3,12(r1)	// restore r3
	lwz	r4,16(r1)
	lwz	r5,20(r1)
	lwz	r6,24(r1)
	lwz	r7,28(r1)
	lwz	r8,32(r1)
	lwz	r9,36(r1)
	lwz	r10,40(r1)
	lwz	r11,44(r1)
	lwz	r12,48(r1)
	lwz	r1,4(r1)	// restore r1
	rfi


#define EXCEPTION_PROLOGUE \
	mtsprg1	r1 ;		/* scratch */ \
	rlwinm r1, r1, 0,1,31 ; /* convert stack pointer to physical address */ \
	addi	r1,r1,-80 ;	/* push exception frame */ \
 \
	stw	r0,0(r1) ;	/* save r0 */ \
	mfsprg1	r0 ; \
	stw	r0,4(r1) ;	/* save r1 */ \
	stw	r2,8(r1) ;	/* save r2 */ \
	stw	r3,12(r1) ;	/* save r3 */ \
	stw	r4,16(r1) ; \
	stw	r5,20(r1) ; \
	stw	r6,24(r1) ; \
	stw	r7,28(r1) ; \
	stw	r8,32(r1) ; \
	stw	r9,36(r1) ; \
	stw	r10,40(r1) ; \
	stw	r11,44(r1) ; \
	stw	r12,48(r1) ; \
 \
	mflr	r0 ; \
	stw	r0,52(r1) ; \
	mfcr	r0 ; \
	stw	r0,56(r1) ; \
	mfctr	r0 ; \
	stw	r0,60(r1) ; \
	mfxer	r0 ; \
	stw	r0,64(r1) ; \
 \
	/* 76(r1) unused */ \
	addi	r1,r1,-16 ;	/* call conventions uses 0(r1) and 4(r1)... */

#define EXCEPTION_EPILOGUE \
	lis r3, exception_return@h ; \
	ori r3, r3, exception_return@l ; \
	rlwinm r3, r3, 0, 1, 31 ; \
	mtctr r3 ; \
	bctr ;

vector_dsi:
	EXCEPTION_PROLOGUE
	lis r3, dsi_exception@h
	ori r3, r3, dsi_exception@l
	rlwinm r3, r3, 0, 1, 31
	mtctr r3
	bctrl
	EXCEPTION_EPILOGUE
vector_dsi_end:

#if 0
vector_isi:
	EXCEPTION_PROLOGUE
	lis r3, isi_exception@h
	ori r3, r3, isi_exception@l
	rlwinm r3, r3, 0, 1, 31
	mtctr r3
	bctrl
	EXCEPTION_EPILOGUE
vector_isi_end:
#endif
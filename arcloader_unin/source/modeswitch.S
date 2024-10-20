#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.text

.global ModeSwitchEntry
ModeSwitchEntry: // (ArcFirmEntry Start, PHW_DESCRIPTION HwDesc)
	
// save our arguments
// r3 (le entrypoint) into srr0
	mr r29, r3
// r4 (argument) into r31
	mr r31, r4
	mr r30, r5
	li r28,-1
	
	//lwz r27, 0(r30)

// Disable interrupts.
	mfmsr r7
	rlwinm r8, r7, 0, 17, 15
	mtmsr r8
	isync
	
// mark that we are here
	//stw r28, 0(r30)
	//stw r28, 4(r30)
	//stw r28, 8(r30)
	//stw r28, 0xc(r30)

// All exceptions lead to infinite loop. No exceptions.
	li r0,0x10
	mtctr r0
	li r7,0
	lis r8, 0x4800 // b .
exception_wipe_loop:
	stw r8, 4(r7)
	addi r7, r7, 0x100
	bdnz+ exception_wipe_loop

// Set MSR_ILE
	mfmsr r7
	lis r8, 1
	or r7, r7, r8
	mtmsr r7
	isync

// set up srr1 ready to set MSR_LE, disable MSR_IR|MSR_DR
	ori r7, r7, 1
	rlwinm r7, r7, 0, 28, 25
	mtsrr1 r7
	mtsrr0 r29

// srr0 already set up
// set the hwdesc arg (vaddr):
	oris r3, r31, 0x8000
// put the top 4 bits of the framebuffer address into r4
	rlwinm r4, r30, 0, 0, 3
// turn that into 0x9000_0000 if it's 0x8000_0000
	lis r9, 0x8000
	cmpw 0, r4, r9
	bne+ 0, not_bat0
	lis r4, 0x9000
not_bat0:
// and rfi into le entrypoint
	rfi
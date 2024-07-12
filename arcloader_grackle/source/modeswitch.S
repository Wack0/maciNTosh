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

// Disable interrupts.
	mfmsr r7
	rlwinm r8, r7, 0, 17, 15
	mtmsr r8
	isync

// r4 = CONFIG_ADDR
	lis r4, 0xFEC0
	ori r4, r4, 4
// r5 = CONFIG_DATA
	addis r5, r4, 0x20
// r6 = PICR1 addr
	lis r6, 0x8000
	ori r6, r6, 0x00A8

// Ensure we can access grackle pci config registers through the MMU:
	lwbrx r0,0,r4
	lwbrx r0,0,r5

// All exceptions lead to infinite loop. No exceptions.
	li r0,0x10
	mtctr r0
	li r7,0
	lis r8, 0x4800 // b .
exception_wipe_loop:
	stw r8, 4(r7)
	addi r7, r7, 0x100
	bdnz+ exception_wipe_loop

// Set up grackle to r/w PICR1
	stwbrx r6,0,r4
	eieio
	sync
	lwbrx r0,0,r4
	sync
// Set PICR1_LE_MODE in PICR1
	lwbrx r7,0,r5
	sync
	ori r7,r7,0x20
	stwbrx r7,0,r5
	eieio
	sync

// Set MSR_ILE now PCI bus is endian swapping and interrupts are disabled
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
// and rfi into le entrypoint
	rfi
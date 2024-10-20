#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.text

.global __ArcInvokeImpl
__ArcInvokeImpl:
	mflr r0
	stwu r0, -4(r1)
	mtctr r3
	mr r2, r4
	mr r3, r5
	mr r4, r6
	mr r5, r7
	bctrl
	lwzu r0, 0(r1)
	addi r1, r1, 4
	mtlr r0
	blr

	.globl DCFlushRangeNoSync
DCFlushRangeNoSync:
	cmplwi r4, 0   # zero or negative size?
	blelr
	clrlwi. r5, r3, 27  # check for lower bits set in address
	beq 1f
	addi r4, r4, 0x20 
1:
	addi r4, r4, 0x1f
	srwi r4, r4, 5
	mtctr r4
2:
	dcbf r0, r3
	addi r3, r3, 0x20
	bdnz 2b
	blr

	.globl ICInvalidateRange
ICInvalidateRange:
	cmplwi r4, 0   # zero or negative size?
	blelr
	clrlwi. r5, r3, 27  # check for lower bits set in address
	beq 1f
	addi r4, r4, 0x20 
1:
	addi r4, r4, 0x1f
	srwi r4, r4, 5
	mtctr r4
2:
	icbi r0, r3
	addi r3, r3, 0x20
	bdnz 2b
	sync
	isync
	blr


	.globl DCFlushRangeInlineSync
DCFlushRangeInlineSync:
	cmplwi r4, 0   # zero or negative size?
	blelr
	clrlwi. r5, r3, 27  # check for lower bits set in address
	beq 1f
	addi r4, r4, 0x20 
1:
	addi r4, r4, 0x1f
	srwi r4, r4, 5
	mtctr r4
2:
	dcbf r0, r3
	addi r3, r3, 0x20
	bdnz 2b
	mfspr	r3,HID0
	ori		r4,r3,0x0008
	mtspr	HID0,r4
	isync
	sync
	mtspr	HID0,r3
	blr

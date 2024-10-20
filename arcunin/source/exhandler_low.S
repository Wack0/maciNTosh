#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.text

.extern ArcBugcheck
.extern RegisterSpace

.globl BugcheckTrampoline
BugcheckTrampoline:
    mtspr SPRG3, r31
    lis r31, RegisterSpace@h
    ori r31, r31, RegisterSpace@l
    clrlwi r31, r31, 2
    stw r0, 0(r31)
    mfsrr0 r0
    stw r0, 128(r31)
    mfsrr1 r0
    stw r0, 132(r31)

    lis r31, BugcheckHandler@h
    ori r31, r31, BugcheckHandler@l
    mtsrr0 r31
    mfmsr		r31
	ori			r31,r31,MSR_IR|MSR_DR
	mtsrr1 r31
    mfspr r31, SPRG3
    rfi

.globl BugcheckHandler
BugcheckHandler:
    lis r31, RegisterSpace@h
    ori r31, r31, RegisterSpace@l
    //stw r0, 0(r31)
    stw r1, 4(r31)
    stw r2, 8(r31)
    stw r3, 12(r31)
    stw r4, 16(r31)
    stw r5, 20(r31)
    stw r6, 24(r31)
    stw r7, 28(r31)
    stw r8, 32(r31)
    stw r9, 36(r31)
    stw r10, 40(r31)
    stw r11, 44(r31)
    stw r12, 48(r31)
    stw r13, 52(r31)
    stw r14, 56(r31)
    stw r15, 60(r31)
    stw r16, 64(r31)
    stw r17, 68(r31)
    stw r18, 72(r31)
    stw r19, 76(r31)
    stw r20, 80(r31)
    stw r21, 84(r31)
    stw r22, 88(r31)
    stw r23, 92(r31)
    stw r24, 96(r31)
    stw r25, 100(r31)
    stw r26, 104(r31)
    stw r27, 108(r31)
    stw r28, 112(r31)
    stw r29, 116(r31)
    stw r30, 120(r31)
    stw r31, 124(r31)
    
    //mfspr r4, 26
    //stw r4, 128(r31)
    //mfspr r4, 27
    //stw r4, 132(r31)
    //mfspr r4, CR
    //stw r4, 136(r31)
    mfspr r4, 8
    stw r4, 140(r31)
    mfspr r4, 9
    stw r4, 144(r31)
    mfspr r4, 1
    stw r4, 148(r31)
    mfspr r4, 19
    stw r4, 152(r31)
    mfspr r4, 18
    stw r4, 156(r31)
    mfspr r4, 25
    stw r4, 160(r31)

    mr r3, r31
    b ArcBugcheck

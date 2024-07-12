#define _LANGUAGE_ASSEMBLY
#include "asm.h"

.text

	.globl _get_ticks
_get_ticks:
      mftbu   r4
      mftb    r3
      mftbu   r5
      cmpw    0,r4,r5
      bne     _get_ticks
      blr

	  .globl _wait_ticks
_wait_ticks:
	mflr r8
	mr r7,r3
	bl _get_ticks

	addc r7, r3, r7
	addze r6, r4

1:	bl _get_ticks
	subfc r3, r3, r7
	subfe. r4, r4, r6
	bge 1b

	mtlr r8
	blr


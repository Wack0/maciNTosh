#ifndef __EXCONTEXT_H__
#define __EXCONTEXT_H__

#define NUM_EXCEPTIONS		15

#define EX_SYS_RESET		 0
#define EX_MACH_CHECK		 1
#define EX_DSI				 2
#define EX_ISI				 3
#define EX_INT				 4
#define EX_ALIGN			 5
#define EX_PRG				 6
#define EX_FP				 7
#define EX_DEC				 8
#define EX_SYS_CALL			 9
#define EX_TRACE			10
#define EX_PERF				11
#define EX_IABR				12
#define EX_RESV				13
#define EX_THERM			14

#ifndef _LANGUAGE_ASSEMBLY

#include "types.h"

#ifdef __cplusplus
   extern "C" {
#endif /* __cplusplus */

typedef struct _excption_frame {
	ULONG EXCPT_Number;
	ULONG SRR0,SRR1;
	ULONG GPR[32];
	ULONG GQR[8];
	ULONG CR, LR, CTR, XER, MSR, DAR;

	USHORT	state;		//used to determine whether to restore the fpu context or not
	USHORT mode;		//unused

	double FPR[32];
	uint64_t	FPSCR;
	double PSFPR[32];
} frame_context;

#ifdef __cplusplus
   }
#endif /* __cplusplus */

#endif		//!_LANGUAGE_ASSEMBLY

#endif

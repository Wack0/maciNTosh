// Based on https://gist.github.com/TheRouletteBoi/e1e9925699ee8d708881e167e397058b
// Assumptions:
// - r0 is always safe to use in function prologue (it's only ever used as a scratch register)

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "arc.h"
#include "arcconsole.h"
#include "ppcinst.h"

enum {
	BRANCH_OPTION_ALWAYS = 20, // 0b10100
	SPR_CTR = 288, // 0b01001_00000
};

static PPC_INSTRUCTION_BIG s_TrampolineBuffer[1024 / sizeof(PPC_INSTRUCTION_BIG)] = { 0 };
static PPC_INSTRUCTION_BIG s_EmulationBuffer[1024 / sizeof(PPC_INSTRUCTION_BIG)] = { 0 };
static size_t s_TrampolineSize = 0;
static size_t s_EmulationSize = 0;

static inline ARC_FORCEINLINE bool is_offset_in_branch_range(long offset)
{
	return (offset >= -0x2000000 && offset <= 0x1fffffc && !(offset & 0x3));
}

static size_t PPCHook_EmitAbsoluteBranchImpl(PPPC_INSTRUCTION_BIG Destination, const PPPC_INSTRUCTION_BIG Target, bool Call, ULONG BranchOptions, ULONG ConditionRegister) {
	LONG JumpOffset = ((ULONG)Target - (ULONG)Destination);
	if (BranchOptions == BRANCH_OPTION_ALWAYS && is_offset_in_branch_range(JumpOffset)) {
		if (Destination != NULL) {
			PPC_INSTRUCTION_BIG Branch;
			Branch.Long = 0;
			Branch.Primary_Op = B_OP;
			Branch.Iform_LK = Call;
			Branch.Iform_LI = JumpOffset >> 2;
			Destination[0] = Branch;
		}
		return 1;
	}
	PPC_INSTRUCTION_BIG Instructions[4] = { 0 };
	// addis r0, 0, Target@hi
	Instructions[0].Primary_Op = ADDIS_OP;
	Instructions[0].Dform_RT = 0;
	Instructions[0].Dform_RA = 0;
	Instructions[0].Dform_D = (USHORT)((ULONG)Target >> 16);
	// ori r0, r0, Target@lo
	Instructions[1].Primary_Op = ORI_OP;
	Instructions[1].Dform_RT = 0;
	Instructions[1].Dform_RA = 0;
	Instructions[1].Dform_D = ((USHORT)(ULONG)Target);
	// mtctr r0
	Instructions[2].Primary_Op = X31_OP;
	Instructions[2].XFXform_XO = MTSPR_OP;
	Instructions[2].XFXform_RS = 0;
	Instructions[2].XFXform_spr = SPR_CTR;
	// bcctr(l)
	Instructions[3].Primary_Op = X19_OP;
	Instructions[3].XLform_XO = BCCTR_OP;
	Instructions[3].XLform_BO = BranchOptions;
	Instructions[3].XLform_BI = ConditionRegister;
	Instructions[3].XLform_LK = Call;

	if (Destination != NULL) {
		for (size_t i = 0; i < sizeof(Instructions) / sizeof(Instructions[0]); i++) Destination[i] = Instructions[i];
	}
	return sizeof(Instructions) / sizeof(Instructions[0]);
}

static size_t PPCHook_EmitAbsoluteBranch(PPPC_INSTRUCTION_BIG Destination, const PPPC_INSTRUCTION_BIG Target) {
	return PPCHook_EmitAbsoluteBranchImpl(Destination, Target, false, BRANCH_OPTION_ALWAYS, 0);
}

static size_t PPCHook_GetAbsoluteBranchSize(ULONG Destination, ULONG Target) {
	if (Target != 0) {
		LONG JumpOffset = Target - Destination;
		if (is_offset_in_branch_range(JumpOffset)) return 1; // can be done in 1 instruction!
	}
	return PPCHook_EmitAbsoluteBranch(NULL, NULL);
}

static inline ARC_FORCEINLINE LONG HookSignExtBranch(LONG x) {
	return x & 0x2000000 ? (LONG)(x | 0xFC000000) : (LONG)(x);
}

static inline ARC_FORCEINLINE LONG HookSignExt16(SHORT x) {
	return (LONG)x;
}

static size_t PPCHook_RelocateBranch(PPPC_INSTRUCTION_BIG Destination, PPPC_INSTRUCTION_BIG Source) {
	if (Source->Iform_AA) {
		// Branch is absolute, so no special handling needs doing
		if (Destination != NULL) *Destination = *Source;
		return 1;
	}

	LONG BranchOffset;
	ULONG BranchOptions, ConditionRegister;
	switch (Source->Primary_Op) {
	case B_OP:
		BranchOffset = HookSignExtBranch(Source->Iform_LI << 2);
		BranchOptions = BRANCH_OPTION_ALWAYS;
		ConditionRegister = 0;
		break;
	case BC_OP:
		BranchOffset = HookSignExt16(Source->Bform_BD << 2);
		BranchOptions = Source->Bform_BO;
		ConditionRegister = Source->Bform_BI;
		break;
	}

	PPPC_INSTRUCTION_BIG BranchAddress = (PPPC_INSTRUCTION_BIG)((ULONG)Source + BranchOffset);
	return PPCHook_EmitAbsoluteBranchImpl(Destination, BranchAddress, Source->Iform_LK, BranchOptions, ConditionRegister);
}

static size_t PPCHook_RelocateEmulation(PPPC_INSTRUCTION_BIG Destination, PPPC_INSTRUCTION_BIG Source) {
	if (Destination == NULL) return 2;
	PPC_INSTRUCTION_BIG Insn = { 0 };
	Insn.Primary_Op = EMU_OP;
	Insn.Emu_Offset = s_EmulationSize;
	ULONG TableOffset = Source->Emu_Offset;
	TableOffset *= sizeof(ULONG);
	PPPC_INSTRUCTION_BIG Real = (PPPC_INSTRUCTION_BIG)((ULONG)Source + TableOffset);
	s_EmulationBuffer[s_EmulationSize] = *Real;
	s_EmulationSize++;

	*Destination = Insn;
	return 1;
}

static size_t PPCHook_RelocateInstruction(PPPC_INSTRUCTION_BIG Destination, PPPC_INSTRUCTION_BIG Source) {
	switch (Source->Primary_Op) {
	case B_OP:
	case BC_OP:
		return PPCHook_RelocateBranch(Destination, Source);
	case EMU_OP:
		return PPCHook_RelocateEmulation(Destination, Source);
	default:
		if (Destination != NULL) *Destination = *Source;
		return 1;
	}
}

/// <summary>
/// Hooks a powerpc function, using the specified code-cave to store the trampoline.
/// </summary>
/// <param name="FunctionPointer">Pointer to function pointer to hook, will get overwritten by orig_function trampoline address.</param>
/// <param name="HookLocation">Pointer to hooked implementation.</param>
/// <param name="TrampolineCave">Code cave to use as trampoline.</param>
/// <returns>Number of instructions written to TrampolineCave.</returns>
size_t PPCHook_HookWithCave(PVOID* FunctionPointer, PVOID HookLocation, PPPC_INSTRUCTION_BIG TrampolineCave) {
	s_EmulationSize = 0;
	// Get the length of the hook (in instructions)
	PPPC_INSTRUCTION_BIG Function = (PPPC_INSTRUCTION_BIG)*FunctionPointer;
	bool BranchToCaveInOne = PPCHook_GetAbsoluteBranchSize((ULONG)Function, (ULONG)TrampolineCave);

	size_t HookSize = 0;
	size_t RelocationStart = 0;
	size_t InsnCount = PPCHook_GetAbsoluteBranchSize((ULONG)Function, (ULONG)HookLocation);
	if (InsnCount == 1) BranchToCaveInOne = false;
	else if (BranchToCaveInOne) {
		InsnCount = 1;
		// Write out a branch from trampoline to the hook location.
		HookSize = PPCHook_EmitAbsoluteBranch(TrampolineCave, (PPPC_INSTRUCTION_BIG)HookLocation);
		RelocationStart = HookSize;
	}

	// Relocate the instructions to the provided cave.
	for (size_t i = 0; i < InsnCount; i++) {
		HookSize += PPCHook_RelocateInstruction(TrampolineCave == NULL ? NULL : &TrampolineCave[HookSize], &Function[i]);
	}

	// Write out the branch to original function.
	// Don't preserve r0, as it's expected to be clobbered across function calls anyway
	PPPC_INSTRUCTION_BIG OriginalBranch = &Function[InsnCount];
	HookSize += PPCHook_EmitAbsoluteBranch(TrampolineCave == NULL ? NULL : &TrampolineCave[HookSize], OriginalBranch);

	// All instructions have been written, now fix up the emulated instructions if any:
	if (s_EmulationSize != 0) {
		if (TrampolineCave == NULL) HookSize += s_EmulationSize;
		else {
			for (size_t i = 0; i < InsnCount; i++) {
				size_t reloc = RelocationStart + i;
				if (TrampolineCave[reloc].Primary_Op != EMU_OP) continue;
				TrampolineCave[HookSize] = s_EmulationBuffer[TrampolineCave[reloc].Emu_Offset];
				TrampolineCave[reloc].Emu_Offset = (HookSize - reloc);
				HookSize++;
			}
		}
	}

	if (TrampolineCave != NULL) {
		*FunctionPointer = &TrampolineCave[RelocationStart];

		// Write out the branch to the hook location, or to the trampoline if that can fit in one instruction and branch to hook location cannot.
		PPPC_INSTRUCTION_BIG HookLocationInsn = (PPPC_INSTRUCTION_BIG)HookLocation;
		if (BranchToCaveInOne) {
			PPCHook_EmitAbsoluteBranch(Function, TrampolineCave);
		}
		else {
			PPCHook_EmitAbsoluteBranch(Function, HookLocationInsn);
		}
	}

	return HookSize;
}

/// <summary>
/// Hooks a powerpc function, using a global buffer to store the trampoline.
/// </summary>
/// <param name="FunctionPointer">Pointer to function pointer to hook, will get overwritten by orig_function trampoline address.</param>
/// <param name="HookLocation">Pointer to hooked implementation.</param>
/// <returns>Number of instructions written to TrampolineCave.</returns>
size_t PPCHook_Hook(PVOID* FunctionPointer, PVOID HookLocation) {
	// Get the length of the trampoline (in instructions)
	size_t InsnCount = PPCHook_HookWithCave(FunctionPointer, HookLocation, NULL);
	// Bounds check the trampoline buffer.
	if ((s_TrampolineSize + InsnCount) >= sizeof(s_TrampolineBuffer) / sizeof(s_TrampolineBuffer[0])) return 0;
	// Hook the function.
	size_t RetVal = PPCHook_HookWithCave(FunctionPointer, HookLocation, &s_TrampolineBuffer[s_TrampolineSize]);
	s_TrampolineSize += RetVal;
	return RetVal;
}

/// <summary>
/// Hooks a powerpc function end, using a global buffer to store the trampoline.
/// </summary>
/// <param name="FunctionPointer">Pointer to function pointer to hook, will get overwritten by orig_function trampoline address.</param>
/// <param name="HookLocation">Pointer to hooked implementation.</param>
/// <returns>Number of instructions written to TrampolineCave.</returns>
size_t PPCHook_HookEnd(PVOID FunctionPointer, PVOID HookLocation) {
	PVOID* pFunctionPointer = &FunctionPointer;
	// Get the length of the trampoline (in instructions)
	size_t InsnCount = PPCHook_HookWithCave(pFunctionPointer, HookLocation, NULL);
	if (InsnCount != 1) return 0;
	// Bounds check the trampoline buffer.
	if ((s_TrampolineSize + InsnCount) >= sizeof(s_TrampolineBuffer) / sizeof(s_TrampolineBuffer[0])) return 0;
	// Hook the function.
	size_t RetVal = PPCHook_HookWithCave(pFunctionPointer, HookLocation, &s_TrampolineBuffer[s_TrampolineSize]);
	s_TrampolineSize += RetVal;
	return RetVal;
}
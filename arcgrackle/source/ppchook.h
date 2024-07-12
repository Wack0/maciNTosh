#pragma once

/// <summary>
/// Hooks a powerpc function, using the specified code-cave to store the trampoline.
/// </summary>
/// <param name="FunctionPointer">Pointer to function pointer to hook, will get overwritten by orig_function trampoline address.</param>
/// <param name="HookLocation">Pointer to hooked implementation.</param>
/// <param name="TrampolineCave">Code cave to use as trampoline.</param>
/// <returns>Number of instructions written to TrampolineCave.</returns>
size_t PPCHook_HookWithCave(PVOID* FunctionPointer, PVOID HookLocation, PPPC_INSTRUCTION_BIG TrampolineCave);

/// <summary>
/// Hooks a powerpc function, using a global buffer to store the trampoline.
/// </summary>
/// <param name="FunctionPointer">Pointer to function pointer to hook, will get overwritten by orig_function trampoline address.</param>
/// <param name="HookLocation">Pointer to hooked implementation.</param>
/// <returns>Number of instructions written to TrampolineCave.</returns>
size_t PPCHook_Hook(PVOID* FunctionPointer, PVOID HookLocation);

/// <summary>
/// Hooks a powerpc function end, using a global buffer to store the trampoline.
/// </summary>
/// <param name="FunctionPointer">Function end (blr insn) to hook.</param>
/// <param name="HookLocation">Pointer to hooked implementation.</param>
/// <returns>Number of instructions written to TrampolineCave.</returns>
size_t PPCHook_HookEnd(PVOID FunctionPointer, PVOID HookLocation);
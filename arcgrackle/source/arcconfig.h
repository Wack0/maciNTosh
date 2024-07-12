#pragma once

/// <summary>
/// Parses the next part of an ARC device path.
/// </summary>
/// <param name="pPath">Pointer to the part of the string to parse. On function success, pointer is advanced past the part that was parsed.</param>
/// <param name="ExpectedType">The device type that is expected to be parsed.</param>
/// <param name="Key">On function success, returns the parsed key from the string.</param>
/// <returns>True if parsing succeeded, false if it failed.</returns>
bool ArcDeviceParse(PCHAR* pPath, CONFIGURATION_TYPE ExpectedType, ULONG* Key);

/// <summary>
/// For an ARC device, get its ARC path.
/// </summary>
/// <param name="Component">ARC component</param>
/// <param name="Path">Buffer to write path to</param>
/// <param name="Length">Length of buffer</param>
/// <returns>Length written without null terminator</returns>
ULONG ArcDeviceGetPath(PCONFIGURATION_COMPONENT Component, PCHAR Path, ULONG Length);

void ArcConfigInit(void);
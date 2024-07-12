#pragma once

enum {
	ARC_ENV_VARS_SIZE = 1024,
	ARC_ENV_MAXIMUM_VALUE_SIZE = 256,
};

/// <summary>
/// Gets the environment variable table.
/// </summary>
/// <returns>Environment variable table</returns>
const BYTE* ArcEnvGetVars(void);

/// <summary>
/// Sets an environment variable in memory.
/// </summary>
/// <param name="Key">Environment variable key.</param>
/// <param name="Value">Environment variable value.</param>
/// <returns>ARC status.</returns>
ARC_STATUS ArcEnvSetVarInMem(PCHAR Key, PCHAR Value);

/// <summary>
/// Gets a device environment variable.
/// </summary>
/// <param name="Key">Name of environment variable</param>
/// <returns>Pointer to environment variable</returns>
PCHAR ArcEnvGetDevice(PCHAR Key);

/// <summary>
/// Sets a device path environment variable.
/// </summary>
/// <param name="Key">Environment variable key.</param>
/// <param name="Value">Environment variable value.</param>
/// <returns>ARC status.</returns>
ARC_STATUS ArcEnvSetDevice(PCHAR Key, PCHAR Value);

/// <summary>
/// Sets the hard disk containing ARC NV storage, after it has been formatted.
/// </summary>
/// <param name="DevicePath">ARC device path.</param>
void ArcEnvSetDiskAfterFormat(PCHAR DevicePath);

/// <summary>
/// Loads environment from ARC non-volatile storage, scanning all hard disks to find the one containing ARC NV storage.
/// </summary>
void ArcEnvLoad(void);

void ArcEnvInit(void);
#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "arc.h"
#include "arcenv.h"
#include "arcterm.h"
#include "arcconfig.h"
#include "arcdisk.h"
#include "arcio.h"
#include "arcfs.h"

enum {
	SIZE_OF_ENV = ARC_ENV_VARS_SIZE,
	SIZE_OF_VAL = ARC_ENV_MAXIMUM_VALUE_SIZE
};

static BYTE s_EnvironmentVariableArea[SIZE_OF_ENV] ARC_ALIGNED(32);
static BYTE s_EnvironmentVariableAreaDevices[SIZE_OF_ENV] ARC_ALIGNED(32);
static BYTE s_EnvironmentVariableOut[SIZE_OF_VAL];

static char s_NvDiskPath[SIZE_OF_VAL];

const BYTE* ArcEnvGetVars(void) {
	return s_EnvironmentVariableArea;
}

static ARC_STATUS EnvFindVarDevice(PCHAR Variable, PULONG OffsetKey, PULONG OffsetValue) {
	ULONG Index = 0;

	if (Variable == NULL || *Variable == 0) return _ENOENT;

	while (true) {
		PCHAR String = Variable;
		ULONG LocalOffsetKey = Index;
		for (; Index < SIZE_OF_ENV; Index++) {
			char Character = s_EnvironmentVariableAreaDevices[Index];
			char ExpectedChar = *String;
			// Convert to uppercase.
			if (ExpectedChar >= 'a' && ExpectedChar <= 'z') ExpectedChar &= ~0x20;
			if (Character != ExpectedChar) break;

			String++;
		}

		if (*String == 0 && s_EnvironmentVariableAreaDevices[Index] == '=') {
			// Found the match.
			*OffsetKey = LocalOffsetKey;
			*OffsetValue = Index + 1;
			return _ESUCCESS;
		}

		// Move to the next var.
		for (; s_EnvironmentVariableAreaDevices[Index] != 0; Index++) {
			if (Index >= SIZE_OF_ENV) return _ENOENT;
		}
		Index++;
	}
}

static ARC_STATUS EnvFindVar(PCHAR Variable, PULONG OffsetKey, PULONG OffsetValue) {
	ULONG Index = 0;

	if (Variable == NULL || *Variable == 0) return _ENOENT;

	while (true) {
		PCHAR String = Variable;
		ULONG LocalOffsetKey = Index;
		for (; Index < SIZE_OF_ENV; Index++) {
			char Character = s_EnvironmentVariableArea[Index];
			char ExpectedChar = *String;
			// Convert to uppercase.
			if (ExpectedChar >= 'a' && ExpectedChar <= 'z') ExpectedChar &= ~0x20;
			if (Character != ExpectedChar) break;

			String++;
		}

		if (*String == 0 && s_EnvironmentVariableArea[Index] == '=') {
			// Found the match.
			*OffsetKey = LocalOffsetKey;
			*OffsetValue = Index + 1;
			return _ESUCCESS;
		}

		// Move to the next var.
		for (; s_EnvironmentVariableArea[Index] != 0; Index++) {
			if (Index >= SIZE_OF_ENV) return _ENOENT;
		}
		Index++;
	}
}

/// <summary>
/// Gets a device environment variable.
/// </summary>
/// <param name="Key">Name of environment variable</param>
/// <returns>Pointer to environment variable</returns>
PCHAR ArcEnvGetDevice(PCHAR Key) {
	ULONG OffsetKey, OffsetVal;
	if (ARC_FAIL(EnvFindVarDevice(Key, &OffsetKey, &OffsetVal))) return NULL;

	// Copy string to output.
	int i = 0;
	for (; i < SIZE_OF_VAL - 1; i++) {
		char Character = s_EnvironmentVariableAreaDevices[OffsetVal + i];
		if (Character == 0) break;
		s_EnvironmentVariableOut[i] = Character;
	}
	// Null terminate string.
	s_EnvironmentVariableOut[i] = 0;
	return s_EnvironmentVariableOut;
}

static PCHAR ArcGetEnvVar(PCHAR Key) {
	ULONG OffsetKey, OffsetVal;
	if (ARC_FAIL(EnvFindVar(Key, &OffsetKey, &OffsetVal))) return NULL;

	// Copy string to output.
	int i = 0;
	for (; i < SIZE_OF_VAL - 1; i++) {
		char Character = s_EnvironmentVariableArea[OffsetVal + i];
		if (Character == 0) break;
		s_EnvironmentVariableOut[i] = Character;
	}
	// Null terminate string.
	s_EnvironmentVariableOut[i] = 0;
	return s_EnvironmentVariableOut;
}

static ULONG EnvGetEmptySpace(ULONG Index) {
	for (; s_EnvironmentVariableArea[Index] == 0; Index--) {
		if (Index == 0) break;
	}

	if (Index != 0) {
		Index += sizeof(" "); // Increment past the non-null character, and null terminator
	}
	return Index;
}

static ULONG EnvGetEmptySpaceDevice(ULONG Index) {
	for (; s_EnvironmentVariableAreaDevices[Index] == 0; Index--) {
		if (Index == 0) break;
	}

	if (Index != 0) {
		Index += sizeof(" "); // Increment past the non-null character, and null terminator
	}
	return Index;
}

/// <summary>
/// Sets a device path environment variable.
/// </summary>
/// <param name="Key">Environment variable key.</param>
/// <param name="Value">Environment variable value.</param>
/// <returns>ARC status.</returns>
ARC_STATUS ArcEnvSetDevice(PCHAR Key, PCHAR Value) {
	// Check if variable already exists.
	ULONG OffsetKey, OffsetVal;
	ARC_STATUS Status = EnvFindVarDevice(Key, &OffsetKey, &OffsetVal);
	// If variable does not exist and Value is empty string then just return success
	// (caller wanted to delete a variable that does not exist)
	if (ARC_FAIL(Status) && *Value == 0) return _ESUCCESS;

	// Find the amount of space we have.
	ULONG Index = EnvGetEmptySpaceDevice(SIZE_OF_ENV - 1);
	ULONG Length = SIZE_OF_ENV - Index;

	ULONG KeyLen = strlen(Key);
	ULONG ValueLen = strlen(Value);

	// Variable already exists?
	if (ARC_SUCCESS(Status)) {
		// Does new value equal old value? If so, do nothing.
		ULONG ExistingValueLen = 0;
		bool ValuesEqual = true;
		for (; s_EnvironmentVariableAreaDevices[OffsetVal + ExistingValueLen] != 0; ExistingValueLen++) {
			if (ValuesEqual && (Value[ExistingValueLen] == 0 || Value[ExistingValueLen] != s_EnvironmentVariableAreaDevices[OffsetVal + ExistingValueLen])) {
				ValuesEqual = false;
			}
		}
		if (ValuesEqual && Value[ExistingValueLen] == 0) return _ESUCCESS;
		// Is there enough space to hold new value?
		Length += ExistingValueLen;
		if (Length < ValueLen) return _ENOSPC;

		// Remove the existing variable.
		ULONG OffsetPastExisting = OffsetVal + ExistingValueLen + 1;
		ULONG LengthToCopy = SIZE_OF_ENV - OffsetPastExisting;
		memcpy(&s_EnvironmentVariableAreaDevices[OffsetKey], &s_EnvironmentVariableAreaDevices[OffsetPastExisting], LengthToCopy);
		memset(&s_EnvironmentVariableAreaDevices[OffsetKey + LengthToCopy], 0, SIZE_OF_ENV - OffsetKey - LengthToCopy);

		// If Value is empty string return success, variable has been deleted which is what caller wanted.
		if (*Value == 0) return _ESUCCESS;

		// Correct the index to take the additional space into account
		Index = EnvGetEmptySpaceDevice(Index);
	}
	else {
		// Is there enough space to hold new variable?
		ULONG NewVarLen = KeyLen + ValueLen + 1;
		if (Length < NewVarLen) return _ENOSPC;
	}

	// Write the key, converting to upper case.
	for (int i = 0; i < KeyLen; i++) {
		char Character = Key[i];
		if (Character >= 'a' && Character <= 'z') Character &= ~0x20;
		s_EnvironmentVariableAreaDevices[Index] = Character;
		Index++;
	}
	// Write equals character.
	s_EnvironmentVariableAreaDevices[Index] = '=';
	Index++;
	// Write value.
	for (int i = 0; i < ValueLen; i++) {
		s_EnvironmentVariableAreaDevices[Index] = Value[i];
		Index++;
	}
	// Ensure null terminated.
	s_EnvironmentVariableAreaDevices[Index] = 0;
	return _ESUCCESS;
}

/// <summary>
/// Sets an environment variable in memory.
/// </summary>
/// <param name="Key">Environment variable key.</param>
/// <param name="Value">Environment variable value.</param>
/// <returns>ARC status.</returns>
ARC_STATUS ArcEnvSetVarInMem(PCHAR Key, PCHAR Value) {
	// Check if variable already exists.
	ULONG OffsetKey, OffsetVal;
	ARC_STATUS Status = EnvFindVar(Key, &OffsetKey, &OffsetVal);
	// If variable does not exist and Value is empty string then just return success
	// (caller wanted to delete a variable that does not exist)
	if (ARC_FAIL(Status) && *Value == 0) return _ESUCCESS;

	// Find the amount of space we have.
	ULONG Index = EnvGetEmptySpace(SIZE_OF_ENV - 1);
	ULONG Length = SIZE_OF_ENV - Index;

	ULONG KeyLen = strlen(Key);
	ULONG ValueLen = strlen(Value);

	// Variable already exists?
	if (ARC_SUCCESS(Status)) {
		// Does new value equal old value? If so, do nothing.
		ULONG ExistingValueLen = 0;
		bool ValuesEqual = true;
		for (; s_EnvironmentVariableArea[OffsetVal + ExistingValueLen] != 0; ExistingValueLen++) {
			if (ValuesEqual && (Value[ExistingValueLen] == 0 || Value[ExistingValueLen] != s_EnvironmentVariableArea[OffsetVal + ExistingValueLen])) {
				ValuesEqual = false;
			}
		}
		if (ValuesEqual && Value[ExistingValueLen] == 0) return _ESUCCESS;
		// Is there enough space to hold new value?
		Length += ExistingValueLen;
		if (Length < ValueLen) return _ENOSPC;

		// Remove the existing variable.
		ULONG OffsetPastExisting = OffsetVal + ExistingValueLen + 1;
		ULONG LengthToCopy = SIZE_OF_ENV - OffsetPastExisting;
		memcpy(&s_EnvironmentVariableArea[OffsetKey], &s_EnvironmentVariableArea[OffsetPastExisting], LengthToCopy);
		memset(&s_EnvironmentVariableArea[OffsetKey + LengthToCopy], 0, SIZE_OF_ENV - OffsetKey - LengthToCopy);

		// If Value is empty string return success, variable has been deleted which is what caller wanted.
		if (*Value == 0) return _ESUCCESS;

		// Correct the index to take the additional space into account
		Index = EnvGetEmptySpace(Index);
	}
	else {
		// Is there enough space to hold new variable?
		ULONG NewVarLen = KeyLen + ValueLen + 1;
		if (Length < NewVarLen) return _ENOSPC;
	}

	// Write the key, converting to upper case.
	for (int i = 0; i < KeyLen; i++) {
		char Character = Key[i];
		if (Character >= 'a' && Character <= 'z') Character &= ~0x20;
		s_EnvironmentVariableArea[Index] = Character;
		Index++;
	}
	// Write equals character.
	s_EnvironmentVariableArea[Index] = '=';
	Index++;
	// Write value.
	for (int i = 0; i < ValueLen; i++) {
		s_EnvironmentVariableArea[Index] = Value[i];
		Index++;
	}
	// Ensure null terminated.
	s_EnvironmentVariableArea[Index] = 0;
	return _ESUCCESS;
}

static ARC_STATUS ArcEnvSaveToDisk(void) {
	if (s_NvDiskPath[0] == 0) return _ENODEV;

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

	// Open the disk
	U32LE FileId;
	ARC_STATUS Status = Api->OpenRoutine(s_NvDiskPath, ArcOpenReadWrite, &FileId);
	if (ARC_FAIL(Status)) return Status;

	do {
		// Seek to NV variable area.
		LARGE_INTEGER SeekOffset = INT32_TO_LARGE_INTEGER(REPART_APM_PART7_START * REPART_SECTOR_SIZE);
		Status = Api->SeekRoutine(FileId.v, &SeekOffset, SeekAbsolute);
		if (ARC_FAIL(Status)) break;

		// Write it to disk.
		U32LE Count;
		Status = Api->WriteRoutine(FileId.v, s_EnvironmentVariableArea, sizeof(s_EnvironmentVariableArea), &Count);
		if (ARC_FAIL(Status)) break;
		if (Count.v != sizeof(s_EnvironmentVariableArea)) Status = _EIO;
	} while (false);

	// Close the disk
	Api->CloseRoutine(FileId.v);
	return Status;
}

static ARC_STATUS ArcSetEnvVar(PCHAR Key, PCHAR Value) {
	ARC_STATUS Status = ArcEnvSetVarInMem(Key, Value);
	if (ARC_FAIL(Status)) return Status;

	return ArcEnvSaveToDisk();
}

void ArcEnvSetDiskAfterFormat(PCHAR DevicePath) {
	strncpy(s_NvDiskPath, DevicePath, sizeof(s_NvDiskPath));
	// Wipe the in-RAM ARC environment
	memset(s_EnvironmentVariableArea, 0, sizeof(s_EnvironmentVariableArea));
}

void ArcEnvLoad(void) {
	// Scans through all fixed disks.
	// For each fixed disk attempts to see if it's in our custom APM+MBR format.
	// If so, reads out the data and applies each var.

	ULONG DiskCount = 0;
	ArcDiskGetCounts(&DiskCount, NULL);

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	char DiskVar[] = "hd00:";
	BYTE Sector[0x200];
	bool Loaded = false;
	for (ULONG i = 0; i < DiskCount; i++) {
		DiskVar[3] = (i % 10) + '0';
		DiskVar[2] = (i / 10) + '0';
		PCHAR DevicePath = ArcEnvGetDevice(DiskVar);
		if (DevicePath == NULL) continue;

		// Open the disk
		U32LE FileId;
		ARC_STATUS Status = Api->OpenRoutine(DevicePath, ArcOpenReadWrite, &FileId);
		if (ARC_FAIL(Status)) continue;

		do {
			// Read first sector
			U32LE Count;
			Status = Api->ReadRoutine(FileId.v, Sector, sizeof(Sector), &Count);
			if (ARC_FAIL(Status)) break;
			if (Count.v != sizeof(Sector)) break;

			// Ensure MBR is present.
			// BUGBUG: If MBR partition layout ever changes, same changes need to be made in HAL arccalls.c - otherwise NT won't be able to read/write ARC environment!
			if (Sector[0x1FE] != 0x55) break;
			if (Sector[0x1FF] != 0xAA) break;
			// Ensure first, second and last partitions are correct type.
			// Partition 1: 0x41
			if (Sector[0x1BE + 4] != 0x41) break;
			// Partition 3: FAT16 system partition
			if (Sector[0x1DE + 4] != 0x0E && Sector[0x1DE + 4] != 0x06) break;
			// Partition 4: 0xEE
			if (Sector[0x1EE + 4] != 0xEE) break;
			// (active byte 0x7F)
			if (Sector[0x1EE + 0] != 0x7F) break;

			// MBR looks like ours. Seek to APM partition 7 start which is empty space covered by partition 1 where we have 2 sectors for NVRAM.
			LARGE_INTEGER SeekOffset = INT32_TO_LARGE_INTEGER(REPART_APM_PART7_START * REPART_SECTOR_SIZE);
			Status = Api->SeekRoutine(FileId.v, &SeekOffset, SeekAbsolute);
			if (ARC_FAIL(Status)) break;

			// Load them in.
			BYTE EnvSectors[ARC_ENV_VARS_SIZE];
			Status = Api->ReadRoutine(FileId.v, EnvSectors, sizeof(EnvSectors), &Count);
			if (ARC_FAIL(Status)) break;
			if (Count.v != sizeof(EnvSectors)) break;

			// The format is a series of null terminated strings, "key=value\x00key2=value2\x00"...

			PBYTE Key = EnvSectors;
			while (true) {
				// Start at the first byte of the next key.
				PBYTE Value = Key;
				// If it's zero, there's no more data.
				if (*Key == 0) break;

				// Search for null termination or key termination.
				while (*Value != 0 && *Value != '=') Value++;
				// If it's null, there's no more data.
				if (*Value == 0) break;

				// Null terminate at end of key.
				*Value = 0;
				// Set variable in memory.
				ArcEnvSetVarInMem(Key, &Value[1]);
				// Scan forward to end of variable.
				Value++;
				while (*Value != 0) Value++;
				// Move to next variable.
				Value++;
				// Set new key.
				Key = Value;
				// Loop.
			}

			strncpy(s_NvDiskPath, DevicePath, sizeof(s_NvDiskPath));
			Loaded = true;
		} while (false);

		Api->CloseRoutine(FileId.v);
		if (Loaded) break;
	}
}

void ArcEnvInit(void) {
	// Paddington has actual battery-backed NVRAM, but let's not party on the NVRAM used by other OSes and Open Firmware.
	// 

	// Initialise the functions implemented here.
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	Api->GetEnvironmentRoutine = ArcGetEnvVar;
	Api->SetEnvironmentRoutine = ArcSetEnvVar;
}
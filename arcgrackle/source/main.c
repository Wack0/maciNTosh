#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "arc.h"
#include "runtime.h"
#include "hwdesc.h"
#include "arcconfig.h"
#include "arcdisk.h"
#include "arcload.h"
#include "arcio.h"
#include "arcenv.h"
#include "arcterm.h"
#include "arcmem.h"
#include "arctime.h"
#include "arcconsole.h"
#include "arcfs.h"
#include "getstr.h"
//#include "ppchook.h"
#include "hwdesc.h"

#include "pxi.h"

ULONG s_MacIoStart;

void ArcFwSetup(void);

static const PCHAR s_ArcErrors[] = {
	"Argument list is too long",
	"Access violation",
	"Resource temporarily unavailable",
	"Bad image file type",
	"Device is busy",
	"Fault occured",
	"Invalid argument",
	"Device error",
	"File is a directory",
	"Too many open files",
	"Too many links",
	"Name is too long",
	"Invalid device name",
	"The file or device does not exist",
	"Execute format error",
	"Not enough memory",
	"File is not a directory",
	"Inappropriate control operation",
	"Media not loaded",
	"Read-only file system"
};

PCHAR ArcGetErrorString(ARC_STATUS Status) {
	ULONG Idx = Status - 1;
	if (Idx > sizeof(s_ArcErrors) / sizeof(*s_ArcErrors)) return "";
	return s_ArcErrors[Status - 1];
}

extern size_t __FirmwareToVendorTable[];

static void ArcNotImplemented() {
	printf("\nUnimplemented ARC function called from %08x\n", (size_t)__builtin_return_address(0));
	while (1) {}
}

typedef struct ARC_LE {
	size_t Function;
	size_t Toc;
} FIRMWARE_CALLER;

static FIRMWARE_CALLER __FirmwareToVendorTable2[sizeof(FIRMWARE_VECTOR_TABLE) / sizeof(PVOID)];
extern void* _SDA2_BASE_;

bool g_UsbInitialised = false;
bool g_SdInitialised = false;

typedef char ARC_ENV_VAR[ARC_ENV_MAXIMUM_VALUE_SIZE];
typedef struct {
	ARC_ENV_VAR
		SystemPartition, OsLoader, OsLoadPartition, OsLoadFilename, OsLoadOptions, LoadIdentifier;
} BOOT_ENTRIES, *PBOOT_ENTRIES;

typedef struct {
	PCHAR SystemPartition, OsLoader, OsLoadPartition, OsLoadFilename, OsLoadOptions, LoadIdentifier;
} BOOT_ENTRY, *PBOOT_ENTRY;

enum {
	BOOT_ENTRY_MAXIMUM_COUNT = 5
};

static BOOT_ENTRIES s_BootEntries;
static BOOT_ENTRY s_BootEntryTable[BOOT_ENTRY_MAXIMUM_COUNT];
static ARC_ENV_VAR s_BootEntryChoices[BOOT_ENTRY_MAXIMUM_COUNT];
static BYTE s_BootEntryCount;

#if 0
static LONG s_hUsbHid = -1;
static LONG s_hUsbVen = -1;
#endif

static inline ARC_FORCEINLINE bool InitVariable(PVENDOR_VECTOR_TABLE Api, PCHAR Key, PCHAR Value, ULONG ValueLength) {
	PCHAR StaticValue = Api->GetEnvironmentRoutine(Key);
	if (StaticValue == NULL) return false;
	snprintf(Value, ValueLength, "%s", StaticValue);
	return true;
}

#define INIT_BOOT_VARIABLE(Key) InitVariable(Api, #Key, s_BootEntries . Key , sizeof( s_BootEntries . Key ))
#define INIT_BOOT_ARGV(Key, Str) snprintf(BootEntryArgv . Key, sizeof(BootEntryArgv . Key ), Str "=%s", s_BootEntryTable[DefaultChoice]. Key )

static bool SearchBootEntry(ULONG Index) {
	PCHAR* LastEntry = (PCHAR*)&s_BootEntryTable[Index - 1];
	PCHAR* ThisEntry = (PCHAR*)&s_BootEntryTable[Index];
	bool RetVal = true;
	for (int EntryIdx = 0; EntryIdx < sizeof(s_BootEntryTable[0]) / sizeof(s_BootEntryTable[0].LoadIdentifier); EntryIdx++) {
		PCHAR Entry = strchr(LastEntry[EntryIdx], ';');
		// even if one is not present, the rest still need to be cut
		if (Entry == NULL) {
			RetVal = false;
			ThisEntry[EntryIdx] = NULL;
			continue;
		}
		*Entry = 0;
		ThisEntry[EntryIdx] = Entry + 1;
	}
	return RetVal;
}

static bool InitBootEntriesImpl(PVENDOR_VECTOR_TABLE Api) {
	if (!INIT_BOOT_VARIABLE(SystemPartition)) return false;
	if (!INIT_BOOT_VARIABLE(OsLoader)) return false;
	if (!INIT_BOOT_VARIABLE(OsLoadPartition)) return false;
	if (!INIT_BOOT_VARIABLE(OsLoadFilename)) return false;
	if (!INIT_BOOT_VARIABLE(OsLoadOptions)) {
		// OsLoadOptions is not required. Ensure it's empty string.
		s_BootEntries.OsLoadOptions[0] = 0;
	}
	if (!INIT_BOOT_VARIABLE(LoadIdentifier)) return false;

	// Each boot variable is split by ";"
	// Handle up to five boot entries.
	// First one is always at the start.
	s_BootEntryTable[0].SystemPartition = s_BootEntries.SystemPartition;
	s_BootEntryTable[0].OsLoader = s_BootEntries.OsLoader;
	s_BootEntryTable[0].OsLoadPartition = s_BootEntries.OsLoadPartition;
	s_BootEntryTable[0].OsLoadFilename = s_BootEntries.OsLoadFilename;
	s_BootEntryTable[0].OsLoadOptions = s_BootEntries.OsLoadOptions;
	s_BootEntryTable[0].LoadIdentifier = s_BootEntries.LoadIdentifier;
	s_BootEntryCount = 1;
	// Search through all of them, looking for ';'. If it's not found in one var, then stop.
	for (int i = 1; i < sizeof(s_BootEntryTable) / sizeof(s_BootEntryTable[0]); i++) {
		if (!SearchBootEntry(i)) break;
		s_BootEntryCount++;
	}
	// Boot entries are now known. Initialise the menu choices.
	for (int i = 0; i < s_BootEntryCount; i++) {
		PBOOT_ENTRY Entry = &s_BootEntryTable[i];
		PCHAR EntryName = Entry->LoadIdentifier;
		if (*EntryName != 0) snprintf(s_BootEntryChoices[i], sizeof(s_BootEntryChoices[i]), "Start %s", EntryName);
		else {
			snprintf(s_BootEntryChoices[i], sizeof(s_BootEntryChoices[i]), "Start %s%s",
				Entry->OsLoadPartition, Entry->OsLoadFilename);
		}
	}
	return true;
}

static void InitBootEntries(PVENDOR_VECTOR_TABLE Api) {
	if (!InitBootEntriesImpl(Api)) {
		s_BootEntryCount = 0;
	}
}

static bool EnableTimeout(PVENDOR_VECTOR_TABLE Api) {
	PCHAR AutoLoad = Api->GetEnvironmentRoutine("AutoLoad");
	if (AutoLoad == NULL) return false;
	return (*AutoLoad == 'y' || *AutoLoad == 'Y');
}

static void ArcInitStdHandle(PVENDOR_VECTOR_TABLE Api, PCHAR Name, OPEN_MODE OpenMode, ULONG ExpectedHandle) {
	PCHAR Path = Api->GetEnvironmentRoutine(Name);
	if (Path == NULL) {
		printf("ARC firmware init failed: %s var was not set\n", Name);
		IOSKBD_ReadChar();
		Api->HaltRoutine();
	}
	U32LE DeviceId;
	ARC_STATUS Status = Api->OpenRoutine(Path, OpenMode, &DeviceId);
	if (ARC_FAIL(Status)) {
		printf("ARC firmware init failed: %s open error %s\n", Name, ArcGetErrorString(Status));
		IOSKBD_ReadChar();
		Api->HaltRoutine();
	}
	if (DeviceId.v != ExpectedHandle) {
		printf("ARC firmware init failed: %s expected fid=%d got %d\n", Name, ExpectedHandle, DeviceId.v);
		IOSKBD_ReadChar();
		Api->HaltRoutine();
	}
}

static void PrintDevices(ULONG DiskCount, ULONG CdromCount) {
	// hds first
	char PathName[ARC_ENV_MAXIMUM_VALUE_SIZE];
	for (ULONG Hd = 0; Hd < DiskCount; Hd++) {
		snprintf(PathName, sizeof(PathName), "hd%02d:", Hd);
		printf(" %s - %s\r\n", PathName, ArcEnvGetDevice(PathName));
		printf("  (Partitions: %d)\r\n", ArcDiskGetPartitionCount(Hd));
	}
	// then cds
	for (ULONG Cd = 0; Cd < CdromCount; Cd++) {
		snprintf(PathName, sizeof(PathName), "cd%02d:", Cd);
		printf(" %s - %s\r\n", Cd == 0 ? "cd:" : PathName, ArcEnvGetDevice(PathName));
	}
	// then ramdisk
	if (s_RuntimeRamdisk.Buffer.Length != 0) printf(" drivers.img ramdisk loaded\r\n");
}

bool ArcHasRamdiskLoaded(void) {
	return (s_RuntimeRamdisk.Buffer.Length != 0);
}

void ArcInitRamDisk(ULONG ControllerKey, PVOID Pointer, ULONG Length) {
	s_RuntimeRamdisk.ControllerKey = ControllerKey;
	s_RuntimeRamdisk.Buffer.PointerArc = (ULONG)Pointer & ~0x80000000;
	s_RuntimeRamdisk.Buffer.Length = Length;

	s_RuntimePointers[RUNTIME_RAMDISK].v = (ULONG)&s_RuntimeRamdisk;
}

static void ArcMain() {
	// Initialise the ARC firmware.
	PSYSTEM_PARAMETER_BLOCK Spb = ARC_SYSTEM_TABLE();
	size_t CurrentAddress = ARC_SYSTEM_TABLE_ADDRESS;
	// Zero out the entire block of memory used for the system table, before initialising fields.
	// Runtime block is at SYSTEM_TABLE_ADDRESS + PAGE_SIZE, so only zero out one page.
	memset(Spb, 0, PAGE_SIZE);
	Spb->Signature = ARC_SYSTEM_BLOCK_SIGNATURE;
	Spb->Length = sizeof(*Spb);
	Spb->Version = ARC_VERSION_MAJOR;
	Spb->Revision = ARC_VERSION_MINOR;

	// Restart block.
	CurrentAddress += sizeof(*Spb);
	ARC_SYSTEM_TABLE_LE()->RestartBlock = (CurrentAddress);
	// TODO: multiprocessor support

	// Firmware vectors.
	CurrentAddress += sizeof(Spb->RestartBlock[0]);
	ARC_SYSTEM_TABLE_LE()->FirmwareVector = (CurrentAddress);
	PLITTLE_ENDIAN32 FirmwareVector = (PLITTLE_ENDIAN32)CurrentAddress;
	Spb->FirmwareVectorLength = sizeof(Spb->FirmwareVector[0]);

	// Vendor vectors.
	// This implementation sets the vendor vectors to the big-endian firmware vendor function pointers.
	CurrentAddress += sizeof(Spb->FirmwareVector[0]);
	ARC_SYSTEM_TABLE_LE()->VendorVector = (CurrentAddress);
	PVENDOR_VECTOR_TABLE Api = (PVENDOR_VECTOR_TABLE)CurrentAddress;
	Spb->VendorVectorLength = sizeof(Spb->VendorVector[0]);
	// Initialise all vendor vectors to not implemented stub.
	for (int i = 0; i < sizeof(Spb->VendorVector[0]) / sizeof(PVOID); i++) {
		PVOID* vec = (PVOID*)CurrentAddress;
		vec[i] = ArcNotImplemented;
	}

	// Initialise sub-components.
	ArcMemInit();
	ArcTermInit();
	ArcEnvInit();
	ArcLoadInit();
	ArcConfigInit();
	ArcIoInit();
	ArcDiskInit();
	ArcTimeInit();

	// Load environment from HD if possible.
	ArcEnvLoad();

#if 0 // Already checked by stage1
	// Ensure we have valid decrementer frequency
	if (s_RuntimePointers[RUNTIME_DECREMENTER_FREQUENCY].v == 0) {
		printf("%s", "ARC firmware init failed: could not obtain decrementer frequency\r\n");
		IOSKBD_ReadChar();
		Api->HaltRoutine();
	}
#endif

	// stdout must be file id 0, stdin must be file id 1
	ArcInitStdHandle(Api, "consolein", ArcOpenReadOnly, 0);
	ArcInitStdHandle(Api, "consoleout", ArcOpenWriteOnly, 1);

	// Initialise all firmware vectors using the required calling convention.
	size_t* _OriginalVector = (size_t*)Api;
	_Static_assert(sizeof(VENDOR_VECTOR_TABLE) == sizeof(FIRMWARE_VECTOR_TABLE));
	for (int i = 0; i < sizeof(Spb->FirmwareVector[0]) / sizeof(PVOID); i++) {
		__FirmwareToVendorTable2[i].Function = _OriginalVector[i];
		__FirmwareToVendorTable2[i].Toc = (size_t)&_SDA2_BASE_;
		FirmwareVector[i].v = (size_t)&__FirmwareToVendorTable2[i];
	}

	// Set up the runtime pointer address.
	ARC_SYSTEM_TABLE_LE()->RuntimePointers = (ULONG)s_RuntimePointers;

	// Main loop.
	ULONG DiskCount, CdromCount;
	ArcDiskGetCounts(&DiskCount, &CdromCount);
	ULONG DefaultChoice = 0;
	bool Timeout = true;
	while (1) {
		// Initialise the boot entries.
		InitBootEntries(Api);

		// Initialise the menu.
		PCHAR MenuChoices[BOOT_ENTRY_MAXIMUM_COUNT + 3] = { 0 };
		ULONG NumberOfMenuChoices = s_BootEntryCount + 3;
		for (int i = 0; i < s_BootEntryCount; i++) {
			MenuChoices[i] = s_BootEntryChoices[i];
		}

		MenuChoices[s_BootEntryCount] = "Run a program";
		MenuChoices[s_BootEntryCount + 1] = "Run firmware setup";
		MenuChoices[s_BootEntryCount + 2] = "Restart system";

		if (DefaultChoice >= NumberOfMenuChoices) DefaultChoice = NumberOfMenuChoices - 1;

		// Display the menu.
		ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
		ArcSetScreenAttributes(true, false, false);
		ArcClearScreen();
		ArcSetPosition(3, 0);
		printf(" Actions:\n");

		for (int i = 0; i < NumberOfMenuChoices; i++) {
			ArcSetPosition(i + 5, 5);

			if (i == DefaultChoice) ArcSetScreenAttributes(true, false, true);
			
			printf("%s", MenuChoices[i]);
			ArcSetScreenAttributes(true, false, false);
		}

		ArcSetPosition(NumberOfMenuChoices + 6, 0);

		printf(" Use the arrow keys to select.\r\n");
		printf(" Press Enter to choose.\r\n");
		printf("\r\n\n");
		printf("Detected block I/O devices:\r\n");
		PrintDevices(DiskCount, CdromCount);

		LONG Countdown = 5;
		ULONG PreviousTime = 0;
		static const char s_TimeoutMsg[] = " Seconds until auto-boot, select another option to override: ";
		if (Timeout) {
			Timeout = s_BootEntryCount != 0 && EnableTimeout(Api);
			if (Timeout) {
				ArcSetPosition(NumberOfMenuChoices + 8, 0);
				PCHAR CountdownEnv = Api->GetEnvironmentRoutine("Countdown");
				if (CountdownEnv != NULL) {
					LONG CountdownConv = atoi(CountdownEnv);
					if (CountdownConv != 0) Countdown = CountdownConv;
				}
				printf("%s%d", s_TimeoutMsg, Countdown);
				PreviousTime = Api->GetRelativeTimeRoutine();
			}
		}

		// Implement the menu UI.
		UCHAR Character = 0;
		do {
			if (IOSKBD_CharAvailable()) {
				Character = IOSKBD_ReadChar();
				switch (Character) {
				case 0x1b:
					Character = IOSKBD_ReadChar();
					if (Character != '[') break;
					// fall-through: \x1b[ == \x9b
				case 0x9b:
					Character = IOSKBD_ReadChar();
					ArcSetPosition(DefaultChoice + 5, 5);
					printf("%s", MenuChoices[DefaultChoice]);

					switch (Character) {
					case 'A': // Up arrow
					case 'D': // Left arrow
						if (DefaultChoice == 0) DefaultChoice = NumberOfMenuChoices;
						DefaultChoice--;
						break;
					case 'B': // Down arrow
					case 'C': // Right arrow
						DefaultChoice++;
						if (DefaultChoice >= NumberOfMenuChoices) DefaultChoice = 0;
						break;
					case 'H': // Home
						DefaultChoice = 0;
						break;

					default:
						break;
					}

					ArcSetPosition(DefaultChoice + 5, 5);
					ArcSetScreenAttributes(true, false, true);
					printf("%s", MenuChoices[DefaultChoice]);
					ArcSetScreenAttributes(true, false, false);
					continue;

				// other ARC firmware can support 'D'/'d' to break on load
				// other ARC firmware can support 'K'/'k'/sysrq to enable kd

				default:
					break;
				}
			}

			// if menu option got moved, cancel the timeout
			if (Timeout && DefaultChoice != 0) {
				Timeout = false;
				ArcSetPosition(NumberOfMenuChoices + 8, 0);
				printf("%s", "\x1B[2K");
			}

			// if the timeout is active then update it
			if (Timeout) {
				ULONG RelativeTime = Api->GetRelativeTimeRoutine();
				if (RelativeTime != PreviousTime) {
					PreviousTime = RelativeTime;
					Countdown--;
					ArcSetPosition(NumberOfMenuChoices + 8, 0);
					printf("%s", "\x1B[2K");
					printf("%s%d", s_TimeoutMsg, Countdown);
				}
			}
		} while ((Character != '\n') && (Character != '\r') && (Countdown >= 0));

		// Clear the menu.
		for (int i = 0; i < NumberOfMenuChoices; i++) {
			ArcSetPosition(i + 5, 5);
			printf("%s", "\x1B[2K");
		}

		// Execute the selected option.
		if (DefaultChoice == s_BootEntryCount + 2) {
			Api->RestartRoutine();
			ArcClearScreen();
			ArcSetPosition(5, 5);
			ArcSetScreenColour(ArcColourCyan, ArcColourBlack);
			printf("\r\n ArcRestart() failed, halting...");
			ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
			while (1) {}
		}
		if (DefaultChoice == s_BootEntryCount + 1) {
			// Firmware setup.
			ArcClearScreen();
			ArcFwSetup();
			continue;
		}

		char PathName[ARC_ENV_MAXIMUM_VALUE_SIZE];
		char TempName[ARC_ENV_MAXIMUM_VALUE_SIZE];
		PCHAR TempArgs = NULL;
		if (DefaultChoice == s_BootEntryCount) {
			// User-specified program.
			ArcClearScreen();
			// Get the path.
			static const char s_PrgRunText[] = "Program to run: ";
			ArcSetPosition(5, 5);
			printf(s_PrgRunText);
			GETSTRING_ACTION Action;
			do {
				Action = KbdGetString(TempName, sizeof(TempName), NULL, 5, 5 + sizeof(s_PrgRunText) - 1);
			} while (Action != GetStringEscape && Action != GetStringSuccess);

			// Go back to menu if no path was specified.
			if (TempName[0] == 0) continue;

			// Grab the arguments.
			TempArgs = strchr(TempName, ' ');
			if (TempArgs == NULL) TempArgs = "";
			else {
				*TempArgs = 0;
				TempArgs++;
			}

			// If the name does not contain '(', then it's not an ARC path and needs to be resolved.
			if (strchr(TempName, '(') == NULL) {
				PCHAR Colon = strchr(TempName, ':');
				if (Colon != NULL) {
					// Copy out and convert to lower case.
					int i = 0;
					for (; TempName[i] != ':'; i++) {
						char Character = TempName[i];
						if (Character >= 'A' && Character <= 'Z') Character |= 0x20;
						PathName[i] = Character;
					}
					PathName[i] = ':';
					PathName[i + 1] = 0;

					// Get the env var.
					PCHAR EnvironmentValue = NULL;
					// First, check for "cd:", and instead use "cd00:", the first optical drive detected by the disk sub-component.
					if (PathName[0] == 'c' && PathName[1] == 'd' && PathName[2] == ':' && PathName[3] == 0) {
						EnvironmentValue = ArcEnvGetDevice("cd00:");
					}
					else {
						// Otherwise, use the drive name as obtained.
						EnvironmentValue = ArcEnvGetDevice(PathName);
					}

					if (EnvironmentValue == NULL || Colon[1] != '\\') {
						ArcSetPosition(7, 0);
						ArcSetScreenColour(ArcColourCyan, ArcColourBlack);
						printf(" Path cannot be resolved");
						ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
						Character = IOSKBD_ReadChar();
						continue;
					}

					snprintf(PathName, sizeof(PathName), "%s\\%s", EnvironmentValue, &Colon[2]);
				}
				else {
					ArcSetPosition(7, 0);
					ArcSetScreenColour(ArcColourCyan, ArcColourBlack);
					printf(" Path cannot be resolved");
					ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
					Character = IOSKBD_ReadChar();
					continue;
				}
			}
			else {
				// Looks like a full ARC path, use it.
				snprintf(PathName, sizeof(PathName), "%s", TempName);
			}
		}
		else {
			// Boot entry chosen, use it.
			snprintf(PathName, sizeof(PathName), "%s", s_BootEntryTable[DefaultChoice].OsLoader);
		}

		// Get the environment table.
		PCHAR LoadEnvp[20];
		LoadEnvp[0] = (PCHAR) ArcEnvGetVars();

		if (LoadEnvp[0] != NULL) {
			// Fill up the table.
			ULONG Index = 0;
			while (Index < 19 && *LoadEnvp[Index]) {
				ULONG Next = Index + 1;
				LoadEnvp[Next] = LoadEnvp[Index] + strlen(LoadEnvp[Index]) + 1;
				Index = Next;
			}

			// Last one set to NULL
			LoadEnvp[Index] = NULL;
		}

		PCHAR LoadArgv[8];
		LoadArgv[0] = PathName;
#if 0 // We can't read HFS partitions either :)
		// HACK:
		// osloader can't read HFS partitions.
		// Open the raw drive, if it looks like an ISO (ISO/HFS hybrid) then pass partition(0) (raw drive) as argv[0]
		char ArgvPathName[ARC_ENV_MAXIMUM_VALUE_SIZE];
		strcpy(ArgvPathName, PathName);
		do {
			PCHAR partition = strstr(ArgvPathName, "partition(");
			if (partition == NULL) break;
			partition += (sizeof("partition(") - 1);
			if (*partition != ')' && *partition != '0') *partition = '0';
			PCHAR filepath = strchr(ArgvPathName, '\\');
			if (filepath == NULL) break;
			*filepath = 0;
			U32LE DeviceId;
			if (ARC_FAIL(Api->OpenRoutine(ArgvPathName, ArcOpenReadOnly, &DeviceId))) break;
			do {
				// Seek to sector 0x10
				LARGE_INTEGER Offset = INT64_TO_LARGE_INTEGER(0x800 * 0x10);
				if (ARC_FAIL(Api->SeekRoutine(DeviceId.v, &Offset, SeekAbsolute))) break;
				// Read single sector
				UCHAR IsoSector[2048];
				U32LE Count = { 0 };
				if (ARC_FAIL(Api->ReadRoutine(DeviceId.v, IsoSector, sizeof(IsoSector), &Count)) && Count.v == sizeof(IsoSector)) break;
				// If bytes 0x1fe-1ff are not printable ascii, this isn't an ISO image.
				// Technically, some printable ascii characters are disallowed;
				// this is intended to ensure a backup MBR in this position that also "looks like an ISO" is disallowed. 
				if (IsoSector[0x1fe] < 0x20 || IsoSector[0x1fe] > 0x7f) break;
				if (IsoSector[0x1ff] < 0x20 || IsoSector[0x1fe] > 0x7f) break;
				// Check for identifiers at the correct offset: ISO9660, HSF/High Sierra
				if (!memcmp(&IsoSector[1], "CD001", sizeof("CD001") - 1) || !memcmp(&IsoSector[9], "CDROM", sizeof("CDROM") - 1)) {
					// This looks like an ISO.
					*filepath = '\\';
					LoadArgv[0] = ArgvPathName;
				}
			} while (false);
			Api->CloseRoutine(DeviceId.v);
		} while (false);
#endif
		ULONG ArgCount = 1;
		// Load the standard arguments if needed.
		if (DefaultChoice < s_BootEntryCount) {
			static BOOT_ENTRIES BootEntryArgv = { 0 };
			INIT_BOOT_ARGV(OsLoader, "OSLOADER");
			INIT_BOOT_ARGV(SystemPartition, "SYSTEMPARTITION");
			INIT_BOOT_ARGV(OsLoadFilename, "OSLOADFILENAME");
			INIT_BOOT_ARGV(OsLoadPartition, "OSLOADPARTITION");
			INIT_BOOT_ARGV(OsLoadOptions, "OSLOADOPTIONS");

			LoadArgv[1] = BootEntryArgv.OsLoader;
			LoadArgv[2] = BootEntryArgv.SystemPartition;
			LoadArgv[3] = BootEntryArgv.OsLoadFilename;
			LoadArgv[4] = BootEntryArgv.OsLoadPartition;
			LoadArgv[5] = BootEntryArgv.OsLoadOptions;
			LoadArgv[6] = NULL;
			LoadArgv[7] = NULL;

			// Look through the environment to find consolein and consoleout
			for (ULONG Index = 0; (LoadArgv[6] == NULL || LoadArgv[7] == NULL) && LoadEnvp[Index] != NULL; Index++) {
				static const char s_ConsoleIn[] = "CONSOLEIN=";
				static const char s_ConsoleOut[] = "CONSOLEOUT=";

				if (LoadArgv[6] == NULL && memcmp(LoadEnvp[Index], s_ConsoleIn, sizeof(s_ConsoleIn) - 1) == 0)
					LoadArgv[6] = LoadEnvp[Index];

				if (LoadArgv[7] == NULL && memcmp(LoadEnvp[Index], s_ConsoleOut, sizeof(s_ConsoleOut) - 1) == 0)
					LoadArgv[7] = LoadEnvp[Index];
			}

			if (LoadArgv[7] != NULL && LoadArgv[6] == NULL) {
				LoadArgv[6] = LoadArgv[7];
				LoadArgv[7] = NULL;
				ArgCount = 7;
			}
			else if (LoadArgv[6] == NULL) ArgCount = 6;
			else if (LoadArgv[7] == NULL) ArgCount = 7;
			else ArgCount = 8;
		}
		else if (TempArgs != NULL) {
			// Set up argv based on the given cmdline.
			ULONG Index = 0;

			for (Index = 0; TempArgs[Index] && ArgCount < sizeof(LoadArgv) / sizeof(*LoadArgv); Index++) {
				if (TempArgs[Index] == ' ') TempArgs[Index] = 0;
				else {
					if (Index != 0 && TempArgs[Index - 1] == 0) {
						LoadArgv[ArgCount] = &TempArgs[Index];
						ArgCount++;
					}
				}
			}
		}

		// If the file can not be opened, add .exe extension
		U32LE FileId;
		if (ARC_FAIL(Api->OpenRoutine(PathName, ArcOpenReadOnly, &FileId))) {
			strcat(PathName, ".exe");
		}
		else {
			Api->CloseRoutine(FileId.v);
		}

		// Run the executable.
		ArcClearScreen();
		ARC_STATUS Status = Api->ExecuteRoutine(PathName, ArgCount, LoadArgv, LoadEnvp);

		if (ARC_SUCCESS(Status)) {
			printf("\n Press any key to continue...\n");
			IOSKBD_ReadChar();
		}
		else {
			ArcSetScreenColour(ArcColourCyan, ArcColourBlack);
			printf("\n Error: ");
			if (Status <= _EROFS) {
				printf("%s", s_ArcErrors[Status - 1]);
			}
			else {
				printf("Error code = %d", Status);
			}
			printf("\n Press any key to continue...\n");
			ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
			IOSKBD_ReadChar();
		}
	}
}

static void ARC_NORETURN FwEarlyPanic(const char* error) {
	printf("%s\r\n%s\r\n", error, "System halting.");
	while (1);
}

void fatal(const char* ptr) {
	printf("%s", ptr);
	mdelay(500);
	PxiPowerOffSystem(true);
	while (1);
}

static PVOID PciPhysToVirt(ULONG Addr) {
	// OldWorld systems with grackle map to 0xF000_0000 area, so check that too 
	if (Addr > 0xF0000000) return (PVOID)Addr;
	return (PVOID) (((Addr) & ~0x80000000) | 0xC0000000);
}

//---------------------------------------------------------------------------------
void ARC_NORETURN FwMain(PHW_DESCRIPTION Desc) {
//---------------------------------------------------------------------------------
	// Copy HW_DESCRIPTION down to stack to avoid it from potentially getting overwritten by init code.
	HW_DESCRIPTION StackDesc;
	memcpy(&StackDesc, Desc, sizeof(StackDesc));
	Desc = &StackDesc;

	// Initialise the console. We know where it is. Just convert it from physical address to our BAT mapping.
	ArcConsoleInit(PciPhysToVirt(Desc->FrameBufferBase), 0, 0, Desc->FrameBufferWidth, Desc->FrameBufferHeight, Desc->FrameBufferStride);

	// Initialise the exception handlers.
	void ArcBugcheckInit(void);
	ArcBugcheckInit();

	// Initialise ARC memory descriptors. 
	if (ArcMemInitDescriptors(Desc->MemoryLength) < 0) {
		FwEarlyPanic("[ARC] Could not initialise memory description");
	}
	// Carve out some space for heap.
	// We will use 4MB. All Grackle + Heathrow/Paddington systems are guaranteed to have at least 32MB of RAM.
	// This allocates from the end of RAM that's accessible by BAT.
	PVOID HeapChunk = ArcMemAllocTemp(0x400000);
	if (HeapChunk == NULL) {
		FwEarlyPanic("[ARC] Could not allocate heap memory");
	}
	add_malloc_block(HeapChunk, 0x100000);

	// Initialise hardware.
	// Timers.
	void setup_timers(ULONG DecrementerFreq);
	setup_timers(Desc->DecrementerFrequency);
	// PXI.
	printf("Init pxi...\r\n");
	PxiInit(PciPhysToVirt(Desc->MacIoStart + 0x16000), (Desc->MrpFlags & MRP_VIA_IS_CUDA) != 0);
	// ADB.
	int adb_bus_init();
	printf("Init adb...\r\n");
	adb_bus_init();

#if 0 // usb driver is for now broken :/
	// USB controllers.
	void ob_usb_ohci_init(PVOID addr);
	printf("Init usb...\r\n");
	if (Desc->UsbOhciStart[0] != 0) ob_usb_ohci_init(PciPhysToVirt(Desc->UsbOhciStart[0]));
	if (Desc->UsbOhciStart[1] != 0) ob_usb_ohci_init(PciPhysToVirt(Desc->UsbOhciStart[1]));
#endif

	// IDE controllers.
	printf("Init ide...\r\n");
	int macio_ide_init(uint32_t addr, int nb_channels);
	macio_ide_init((ULONG) PciPhysToVirt(Desc->MacIoStart), 2);

	// SCSI controller.
	printf("Init scsi...\r\n");
	int mesh_init(uint32_t addr);
	mesh_init((ULONG)PciPhysToVirt(Desc->MacIoStart + 0x10000));

	printf("Early driver init done.\r\n");

	// Zero out the entire runtime area.
	memset(s_RuntimeArea, 0, sizeof(*s_RuntimeArea));

	// Emulator status.
	s_RuntimePointers[RUNTIME_IN_EMULATOR].v = (Desc->MrpFlags & MRP_IN_EMULATOR) != 0;

	// Initialise the first runtime pointer to the VI framebuffer information.
	s_RuntimeFb.PointerArc = Desc->FrameBufferBase;
	s_RuntimeFb.Length = Desc->FrameBufferHeight * Desc->FrameBufferStride;
	s_RuntimeFb.Width = Desc->FrameBufferWidth;
	s_RuntimeFb.Height = Desc->FrameBufferHeight;
	s_RuntimeFb.Stride = Desc->FrameBufferStride;
	s_RuntimePointers[RUNTIME_FRAME_BUFFER].v = (ULONG)&s_RuntimeFb;

	// Initialise the decrementer frequency.
	s_RuntimePointers[RUNTIME_DECREMENTER_FREQUENCY].v = Desc->DecrementerFrequency;

	s_RuntimePointers[RUNTIME_HAS_CUDA].v = (Desc->MrpFlags & MRP_VIA_IS_CUDA) != 0;

	s_MacIoStart = Desc->MacIoStart;
	ArcMain();
	// should never reach here
	while (1) {}
	return 0;
}

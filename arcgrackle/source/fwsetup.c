#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "arc.h"
#include "runtime.h"
#include "arcconfig.h"
#include "arcdisk.h"
#include "arcio.h"
#include "arcenv.h"
#include "arcmem.h"
#include "arctime.h"
#include "arcconsole.h"
#include "arcfs.h"
#include "getstr.h"

enum {
	SETUP_MENU_CHOICE_SYSPART,
	SETUP_MENU_CHOICE_UPDATE,
	SETUP_MENU_CHOICE_LOADRD,
	SETUP_MENU_CHOICE_NOMBRBOOT,
	SETUP_MENU_CHOICE_EXIT,
	SETUP_MENU_CHOICES_COUNT
};

enum {
	PARTITION_HD_MENU_CHOICES_COUNT = 16,
	PARTITION_HD_MENU_CHOICES_VISIBLE = 8
};

typedef char STRING_MENU_CHOICE [80];

static void PartitionerPrintDiskEntry(ULONG Hd, ULONG ExitIndex) {
	if (Hd == ExitIndex) printf("Cancel");
	else {
		char HdVar[6];
		snprintf(HdVar, sizeof(HdVar), "hd%02d:", Hd);
		printf("%s (%dMB) - %s", HdVar, ArcDiskGetSizeMb(Hd), ArcEnvGetDevice(HdVar));
	}
}

static bool StringIsDigits(const char* str) {
	while (*str != 0) {
		if (*str < '0') return false;
		if (*str > '9') return false;
		str++;
	}
	return true;
}
static ULONG s_MacPartitions[64] = { 0 };

enum {
	PARTITION_MENU_CHOICE_CREATE,
	PARTITION_MENU_CHOICE_FINISH,
	PARTITION_MENU_CHOICE_CANCEL,
	PARTITION_MENU_CHOICES_COUNT
};

static char s_InstallMediaPath[ARC_ENV_MAXIMUM_VALUE_SIZE] = { 0 };

static bool ArcFwCheckInstallMedia(void) {
	// Only use the whole disk. We only install from ISO9660 media.
	ULONG HdCount, CdCount;
	ArcDiskGetCounts(&HdCount, &CdCount);

	for (ULONG attempt = 0; attempt < 2; attempt++) {
		bool Hd = attempt == 1;
		ULONG WantedCount = CdCount;
		if (Hd) WantedCount = HdCount;
		for (ULONG i = 0; i < WantedCount; i++) {
			char HdVar[6];
			snprintf(HdVar, sizeof(HdVar), "%cd%02d:", Hd ? 'h' : 'c', i);
			PCHAR HdDevice = ArcEnvGetDevice(HdVar);
			if (ARC_FAIL(ArcFsRepartFilesOnDisk(HdDevice))) continue;
			strncpy(s_InstallMediaPath, HdDevice, sizeof(s_InstallMediaPath));
			return true;
		}
	}

	return false;
}

static void ArcFwPartitionerSelected(ULONG Hd) {
	// Clear the screen.
	ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
	ArcSetScreenAttributes(true, false, false);
	ArcClearScreen();
	ArcSetPosition(3, 0);

	// Get the disk size.
	ULONG DiskSizeMb = ArcDiskGetSizeMb(Hd);

	printf("Total disk space on hd%02d: %dMB\r\n", Hd, DiskSizeMb);
	// 33 MB at the start of the disk is reserved (1MB for bootloader, partition table, etc; 32MB for arc system partition)
	// We also need at least 110MB of disk space for NT (95MB for NT 3.5x)
	// Can't install on any disk below that size.
	// Round it up to a clean 153MB (33MB + 120MB)
	if (DiskSizeMb < 153) {
		printf("At least 153MB of disk space is required for installation.\r\n");
		printf(" Press any key to continue...\r\n");
		IOSKBD_ReadChar();
		return;
	}

	ULONG RemainingDiskSize = DiskSizeMb - 33;
	printf("Available disk space on hd%02d: %dMB\r\n", Hd, RemainingDiskSize);

	// Get the partition size
	ULONG NtPartitionSize = 0;
	char TempName[ARC_ENV_MAXIMUM_VALUE_SIZE];
	while (true) {
		ArcClearScreen();
		ArcSetPosition(3, 0);
		printf("Total disk space on hd%02d: %dMB\r\n", Hd, DiskSizeMb);
		printf("Available disk space on hd%02d: %dMB\r\n", Hd, RemainingDiskSize);
		printf("Maximum size of NT operating system partition: %dMB\r\n", (RemainingDiskSize < REPART_MAX_NT_PART_IN_MB) ? RemainingDiskSize : REPART_MAX_NT_PART_IN_MB);

		static const char s_SizeText[] = "Enter size of NT operating system partition: ";
		ArcSetPosition(6, 5);
		printf("%s", "\x1B[2K");
		printf(s_SizeText);
		GETSTRING_ACTION Action;
		do {
			Action = KbdGetString(TempName, sizeof(TempName), NULL, 6, 5 + sizeof(s_SizeText) - 1);
		} while (Action != GetStringEscape && Action != GetStringSuccess);

		if (Action == GetStringEscape) {
			return;
		}

		if (!StringIsDigits(TempName)) continue;
		
		NtPartitionSize = (ULONG)atoll(TempName);
		if (NtPartitionSize < 120) {
			printf("\r\nAt least 120MB is required.\r\n");
			printf(" Press any key to continue...\r\n");
			IOSKBD_ReadChar();
			continue;
		}
		if (NtPartitionSize > REPART_MAX_NT_PART_IN_MB || NtPartitionSize > RemainingDiskSize) {
			printf("\r\nPartition size is too large.\r\n");
			printf(" Press any key to continue...\r\n");
			IOSKBD_ReadChar();
			continue;
		}

		break;
	}

	RemainingDiskSize -= NtPartitionSize;

	// Set up the menu for Mac partitions.
	ULONG CountMacPartitions = 0;
	memset(s_MacPartitions, 0, sizeof(s_MacPartitions));

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	while (1) {
		ULONG DefaultChoice = 0;
		// Initialise the menu.
		PCHAR MenuChoices[PARTITION_MENU_CHOICES_COUNT] = {
			"Create Mac partition",
			"Finish partitioning and install",
			"Cancel without making changes"
		};

		// Display the menu.
		ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
		ArcSetScreenAttributes(true, false, false);
		ArcClearScreen();
		ArcSetPosition(3, 0);
		printf("Total disk space on hd%02d: %dMB\r\n", Hd, DiskSizeMb);
		printf("Remaining disk space on hd%02d: %dMB\r\n", Hd, RemainingDiskSize);

		for (ULONG i = 0; i < PARTITION_MENU_CHOICES_COUNT; i++) {
			ArcSetPosition(i + 5, 5);

			if (i == DefaultChoice) ArcSetScreenAttributes(true, false, true);

			printf("%s", MenuChoices[i]);
			ArcSetScreenAttributes(true, false, false);
		}

		ArcSetPosition(PARTITION_MENU_CHOICES_COUNT + 5, 0);

		printf("\r\nCurrent partition list:\r\n");
		printf("Reserved system space: 33MB\r\n");
		printf("NT operating system partition: %dMB\r\n", NtPartitionSize);
		for (ULONG i = 0; i < CountMacPartitions; i++) {
			printf("Mac partition %d: %dMB\r\n", i, s_MacPartitions[i]);
		}

		printf("\r\n Use the arrow keys to select.\r\n");
		printf(" Press Enter to choose.\r\n");
		printf("\r\n");

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
						if (DefaultChoice == 0) DefaultChoice = SETUP_MENU_CHOICES_COUNT;
						DefaultChoice--;
						break;
					case 'B': // Down arrow
					case 'C': // Right arrow
						DefaultChoice++;
						if (DefaultChoice >= SETUP_MENU_CHOICES_COUNT) DefaultChoice = 0;
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

				default:
					break;
				}
			}
		} while ((Character != '\n') && (Character != '\r'));


		ArcClearScreen();
		ArcSetPosition(3, 0);

		if (DefaultChoice == PARTITION_MENU_CHOICE_CANCEL) return;

		if (DefaultChoice == PARTITION_MENU_CHOICE_FINISH) {
			printf("WARNING: ALL DATA ON DISK hd%02d: WILL BE LOST!\r\n", Hd);
			printf("Partitions to be created:\r\n");
			printf("Reserved system space: 33MB\r\n");
			printf("NT operating system partition: %dMB\r\n", NtPartitionSize);
			for (ULONG i = 0; i < CountMacPartitions; i++) {
				printf("Mac partition %d: %dMB\r\n", i, s_MacPartitions[i]);
			}
			printf("PROCEED WITH OPERATION? (Y/N)\r\n");
			char Chr;
			do {
				Chr = IOSKBD_ReadChar();
			} while (Chr != 'Y' && Chr != 'y' && Chr != 'N' && Chr != 'n');

			if (Chr == 'N' || Chr == 'n') {
				printf("Operation cancelled.\r\n");
				printf(" Press any key to continue...\r\n");
				IOSKBD_ReadChar();
				return;
			}

			// open hd
			bool DataWritten = false;
			char HdVar[6];
			snprintf(HdVar, sizeof(HdVar), "hd%02d:", Hd);

			U32LE FileId;
			PCHAR HdDevice = ArcEnvGetDevice(HdVar);
			ARC_STATUS Status = Api->OpenRoutine(HdDevice, ArcOpenReadWrite, &FileId);
			if (ARC_SUCCESS(Status)) {
				Status = ArcFsRepartitionDisk(FileId.v, s_InstallMediaPath, NtPartitionSize, s_MacPartitions, CountMacPartitions, &DataWritten);
				Api->CloseRoutine(FileId.v);
			}

			if (ARC_FAIL(Status)) {
				printf("Failed to repartition drive hd%02d: for NT: %s\r\n", Hd, ArcGetErrorString(Status));
				if (!DataWritten) printf("No data has been lost.\r\n");
			}
			else {
				printf("Repartitioned drive hd%02d: for NT successfully\r\n", Hd);
				// Specify that the drive we just partitioned is to be used for ARC NV storage.
				HdDevice = ArcEnvGetDevice(HdVar);
				ArcEnvSetDiskAfterFormat(HdDevice);
				// Set the ARC system partition
				snprintf(TempName, sizeof(TempName), "%spartition(3)", HdDevice);
				Status = Api->SetEnvironmentRoutine("SYSTEMPARTITION", TempName);
				if (ARC_FAIL(Status)) printf("Could not set ARC system partition in NVRAM: %s\r\n", ArcGetErrorString(Status));
#if 0 // todo: reimplement?
				// Set the Open Firmware boot device.
				Status = ArcEnvSetOfVar("boot-device", "hd:3,\\\\:tbxi");
				if (ARC_FAIL(Status)) printf("Could not set Open Firmware boot device in NVRAM: %s\r\n", ArcGetErrorString(Status));
#endif
				printf(" Press any key to restart...\r\n");
				IOSKBD_ReadChar();
				Api->RestartRoutine();
				return;
			}

			printf(" Press any key to continue...\r\n");
			IOSKBD_ReadChar();
			return;
		}

		if (DefaultChoice != PARTITION_MENU_CHOICE_CREATE) continue; // ???

		printf("Total disk space on hd%02d: %dMB\r\n", Hd, DiskSizeMb);
		printf("Remaining disk space on hd%02d: %dMB\r\n", Hd, RemainingDiskSize);

		while (true) {
			static const char s_SizeText[] = "Enter size of Mac partition: ";
			ArcSetPosition(5, 5);
			printf("%s", "\x1B[2K");
			printf(s_SizeText);
			GETSTRING_ACTION Action;
			do {
				Action = KbdGetString(TempName, sizeof(TempName), NULL, 5, 5 + sizeof(s_SizeText) - 1);
			} while (Action != GetStringEscape && Action != GetStringSuccess);

			if (Action == GetStringEscape) {
				return;
			}

			if (!StringIsDigits(TempName)) continue;

			s_MacPartitions[CountMacPartitions] = (ULONG)atoll(TempName);
			if (s_MacPartitions[CountMacPartitions] < 120) {
				printf("\r\nAt least 120MB is required.\r\n");
				printf(" Press any key to continue...\r\n");
				IOSKBD_ReadChar();
				continue;
			}


			if (s_MacPartitions[CountMacPartitions] > RemainingDiskSize) {
				printf("\r\nPartition size is too large.\r\n");
				printf(" Press any key to continue...\r\n");
				IOSKBD_ReadChar();
				continue;
			}

			RemainingDiskSize -= s_MacPartitions[CountMacPartitions];
			CountMacPartitions++;
			break;
		}
	}
}

static void ArcFwPartitionerUpdate(ULONG Hd) {
	// Clear the screen.
	ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
	ArcSetScreenAttributes(true, false, false);
	ArcClearScreen();
	ArcSetPosition(3, 0);

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();

	// open hd
	char HdVar[6];
	snprintf(HdVar, sizeof(HdVar), "hd%02d:", Hd);

	U32LE FileId;
	PCHAR HdDevice = ArcEnvGetDevice(HdVar);
	ARC_STATUS Status = Api->OpenRoutine(HdDevice, ArcOpenReadWrite, &FileId);
	if (ARC_SUCCESS(Status)) {
		Status = ArcFsUpdateBootPartition(FileId.v, s_InstallMediaPath);
		Api->CloseRoutine(FileId.v);
	}

	if (ARC_FAIL(Status)) {
		printf("Failed to update drive hd%02d: boot partition: %s\r\n", Hd, ArcGetErrorString(Status));
	}
	else {
		printf("Updated boot partition on drive hd%02d: successfully\r\n", Hd);
		printf(" Press any key to restart...\r\n");
		IOSKBD_ReadChar();
		Api->RestartRoutine();
		return;
	}

	printf(" Press any key to continue...\r\n");
	IOSKBD_ReadChar();
	return;
}

static void ArcFwPartitioner(ULONG SetupMenuChoice) {
	ULONG HdCount;
	ArcDiskGetCounts(&HdCount, NULL);
	ULONG MenuCount = HdCount + 1;

	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	ULONG Rows = Api->GetDisplayStatusRoutine(0)->CursorMaxYPosition;
	ULONG MaxMenu = Rows - 7;
	if (MenuCount > MaxMenu) MenuCount = MaxMenu;
	ULONG ExitIndex = MenuCount - 1;

	if (!ArcFwCheckInstallMedia()) {
		printf(" Could not find install media in any drive.\r\n");
		printf(" Press any key to continue...\r\n");
		IOSKBD_ReadChar();
		return;
	}
	
	// First, select a disk.
	while (1) {
		ULONG DefaultChoice = 0;

		// Display the menu.
		ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
		ArcSetScreenAttributes(true, false, false);
		ArcClearScreen();
		ArcSetPosition(3, 0);
		printf(" Select a disk:\r\n");

		for (ULONG i = 0; i < MenuCount; i++) {
			ArcSetPosition(i + 4, 5);

			if (i == DefaultChoice) ArcSetScreenAttributes(true, false, true);

			PartitionerPrintDiskEntry(i, ExitIndex);
			ArcSetScreenAttributes(true, false, false);
		}

		ArcSetPosition(MenuCount + 4, 0);

		printf("\r\n Use the arrow keys to select.\r\n");
		printf(" Press Enter to choose.\r\n");
		printf("\n");

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
					ArcSetPosition(DefaultChoice + 4, 5);
					PartitionerPrintDiskEntry(DefaultChoice, ExitIndex);

					switch (Character) {
					case 'A': // Up arrow
					case 'D': // Left arrow
						if (DefaultChoice == 0) DefaultChoice = HdCount;
						DefaultChoice--;
						break;
					case 'B': // Down arrow
					case 'C': // Right arrow
						DefaultChoice++;
						if (DefaultChoice >= MenuCount) DefaultChoice = 0;
						break;
					case 'H': // Home
						DefaultChoice = 0;
						break;

					default:
						break;
					}

					ArcSetPosition(DefaultChoice + 4, 5);
					ArcSetScreenAttributes(true, false, true);
					PartitionerPrintDiskEntry(DefaultChoice, ExitIndex);
					ArcSetScreenAttributes(true, false, false);
					continue;

				default:
					break;
				}
			}
		} while ((Character != '\n') && (Character != '\r'));

		// Clear the menu.
		for (ULONG i = 0; i < MenuCount; i++) {
			ArcSetPosition(i + 4, 5);
			printf("%s", "\x1B[2K");
		}

		// If cancel was selected, then return
		if (DefaultChoice == ExitIndex) return;

		if (SetupMenuChoice == SETUP_MENU_CHOICE_SYSPART) ArcFwPartitionerSelected(DefaultChoice);
		else if (SetupMenuChoice == SETUP_MENU_CHOICE_UPDATE) ArcFwPartitionerUpdate(DefaultChoice);
		return;
	}
}

static PCHAR ArcFwGetSystemPartitionDrive(PCHAR SysPart) {
	if (SysPart == NULL) {
		return NULL;
	}

	// get the partition
	PCHAR SysPartNumber = strstr(SysPart, "partition(");
	if (SysPartNumber == NULL) return NULL;
	SysPartNumber[0] = 0;
	return SysPart;
}

ARC_STATUS ArcDiskInitRamdisk(void);

void ArcFwSetup(void) {
	while (1) {
		ULONG DefaultChoice = 0;
		// Initialise the menu.
		PCHAR MenuChoices[SETUP_MENU_CHOICES_COUNT] = {
			"Repartition disk for NT installation",
			"Update boot partition on disk",
			"Load driver ramdisk",
			"Reboot to OSX install or OS8/OS9",
			"Exit"
		};

		// Display the menu.
		ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
		ArcSetScreenAttributes(true, false, false);
		ArcClearScreen();
		ArcSetPosition(3, 0);
		printf(" Firmware setup actions:\r\n");

		for (ULONG i = 0; i < SETUP_MENU_CHOICES_COUNT; i++) {
			ArcSetPosition(i + 4, 5);

			if (i == DefaultChoice) ArcSetScreenAttributes(true, false, true);

			printf("%s", MenuChoices[i]);
			ArcSetScreenAttributes(true, false, false);
		}

		ArcSetPosition(SETUP_MENU_CHOICES_COUNT + 4, 0);

		printf("\r\n Use the arrow keys to select.\r\n");
		printf(" Press Enter to choose.\r\n");
		printf("\n");

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
					ArcSetPosition(DefaultChoice + 4, 5);
					printf("%s", MenuChoices[DefaultChoice]);

					switch (Character) {
					case 'A': // Up arrow
					case 'D': // Left arrow
						if (DefaultChoice == 0) DefaultChoice = SETUP_MENU_CHOICES_COUNT;
						DefaultChoice--;
						break;
					case 'B': // Down arrow
					case 'C': // Right arrow
						DefaultChoice++;
						if (DefaultChoice >= SETUP_MENU_CHOICES_COUNT) DefaultChoice = 0;
						break;
					case 'H': // Home
						DefaultChoice = 0;
						break;

					default:
						break;
					}

					ArcSetPosition(DefaultChoice + 4, 5);
					ArcSetScreenAttributes(true, false, true);
					printf("%s", MenuChoices[DefaultChoice]);
					ArcSetScreenAttributes(true, false, false);
					continue;

				default:
					break;
				}
			}
		} while ((Character != '\n') && (Character != '\r'));

		// Clear the menu.
		for (int i = 0; i < SETUP_MENU_CHOICES_COUNT; i++) {
			ArcSetPosition(i + 4, 5);
			printf("%s", "\x1B[2K");
		}

		// Execute the selected option.
		if (DefaultChoice == SETUP_MENU_CHOICE_EXIT) {
			return;
		}

		if (DefaultChoice == SETUP_MENU_CHOICE_LOADRD) {
			ARC_STATUS Status = ArcDiskInitRamdisk();
			if (ARC_SUCCESS(Status)) {
				printf(" Loaded ramdisk successfully\r\n");
			}
			else {
				printf(" Failed to load ramdisk: %s\r\n", ArcGetErrorString(Status));
			}
			printf(" Press any key to continue...\r\n");
			IOSKBD_ReadChar();
			return;
		}

		if (DefaultChoice == SETUP_MENU_CHOICE_NOMBRBOOT) {
			PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
			// Get the system partition drive
			PCHAR SysPart = ArcFwGetSystemPartitionDrive(Api->GetEnvironmentRoutine("SYSTEMPARTITION"));
			if (SysPart == NULL) {
				printf(" No system partition present.\r\n");
				printf(" Press any key to continue...\r\n");
				IOSKBD_ReadChar();
				continue;
			}

			{
				// Open the drive
				U32LE DriveId;
				ARC_STATUS Status = Api->OpenRoutine(SysPart, ArcOpenReadWrite, &DriveId);
				do {
					if (ARC_FAIL(Status)) break;

					// corrupt the MBR magic-number
					Status = ArcFsCorruptMbr(DriveId.v);
					// close drive
					Api->CloseRoutine(DriveId.v);
				} while (false);

				if (ARC_FAIL(Status)) {
					printf(" Failed to prepare booting to OSX install/OS8/OS9: %s\r\n", ArcGetErrorString(Status));
					printf(" Press any key to continue...\r\n");
					IOSKBD_ReadChar();
					continue;
				}
			}

			Api->RestartRoutine();
			ArcClearScreen();
			ArcSetPosition(5, 5);
			ArcSetScreenColour(ArcColourCyan, ArcColourBlack);
			printf("\r\n ArcRestart() failed, halting...");
			ArcSetScreenColour(ArcColourWhite, ArcColourBlue);
			while (1) {}
		}

		if (DefaultChoice == SETUP_MENU_CHOICE_SYSPART || DefaultChoice == SETUP_MENU_CHOICE_UPDATE) {
			ArcFwPartitioner(DefaultChoice);
			return;
		}
	}
}
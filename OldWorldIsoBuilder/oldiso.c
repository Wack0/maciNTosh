#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma GCC diagnostic ignored "-Wscalar-storage-order"

#define ARC_LE __attribute__((scalar_storage_order("little-endian")))
#define ARC_BE __attribute__((scalar_storage_order("big-endian")))
#define ARC_PACKED __attribute__((packed))
#define ARC_FORCEINLINE inline __attribute__((always_inline))
#define ARC_ALIGNED(x) __attribute__((aligned(x)))
#define ARC_NORETURN __attribute__((noreturn))
#define ARC_NOINLINE __attribute__((noinline))
#define ARC_WEAK __attribute__((weak))

#define ARC_BIT(x) (1 << (x))
#define ARC_MB(x) ((x) * 1024 * 1024)
#define BIT(x) ARC_BIT(x)

typedef void* PVOID;
typedef char CHAR, * PCHAR;
typedef uint8_t UCHAR, * PUCHAR, BYTE, * PBYTE, BOOLEAN, * PBOOLEAN;
typedef int16_t CSHORT, * PCSHORT, SHORT, * PSHORT;
typedef uint16_t WORD, * PWORD, USHORT, * PUSHORT;
typedef int32_t LONG, * PLONG;
typedef uint32_t ULONG, * PULONG;

typedef uint16_t WCHAR, * PWCHAR;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

// Partition table parsing stuff.
typedef struct ARC_LE ARC_PACKED _PARTITION_ENTRY {
	BYTE Active;
	BYTE StartChs[3];
	BYTE Type;
	BYTE EndChs[3];
	ULONG SectorStart;
	ULONG SectorCount;
} PARTITION_ENTRY, *PPARTITION_ENTRY;

// Driver descriptor entry
typedef struct ARC_BE ARC_PACKED _DDT_DRIVER_ENTRY {
	ULONG SectorStart;
	USHORT SectorCount;
	USHORT OsType; // classic MacOS = 1, ptDR = 0x0701, wiki = 0xF8FF
} DDT_DRIVER_ENTRY, *PDDT_DRIVER_ENTRY;

enum {
	// This is very specific.
	// This is: (sizeof(MBR_SECTOR.MbrCode) - offsetof(DDT_ENTRY, Drivers)) / sizeof(DDT_DRIVER_ENTRY).
	// that is, enough DDT_DRIVER_ENTRY structures such that DDT_ENTRY structure can fit into MBR_SECTOR.MbrCode.
	DDT_DRIVER_COUNT_FOR_MBR = 0x34
};

// Driver descriptor table for APM, at sector 0.
typedef struct ARC_BE ARC_PACKED _DDT_ENTRY {
	USHORT Signature;
	USHORT SectorSize;
	ULONG SectorCount; // of whole disk
	USHORT DeviceType;
	USHORT DeviceId;
	ULONG Data;
	USHORT DriverCount;
	DDT_DRIVER_ENTRY Drivers[DDT_DRIVER_COUNT_FOR_MBR];
} DDT_ENTRY, *PDDT_ENTRY;

// Ensure at compile time that DDT_ENTRY can fit into MbrCode.
_Static_assert(sizeof(DDT_ENTRY) <= 0x1B8);

// MBR (sector 0)
typedef struct ARC_LE ARC_PACKED _MBR_SECTOR {
	union {
		BYTE MbrCode[0x1B8];
		DDT_ENTRY ApmDdt;
	};
	ULONG Signature;
	USHORT Reserved;
	PARTITION_ENTRY Partitions[4];
	USHORT ValidMbr;
} MBR_SECTOR, *PMBR_SECTOR;
_Static_assert(sizeof(MBR_SECTOR) == 0x200);

// APM partition table. (each entry starts at sector 1)
typedef struct ARC_BE ARC_PACKED _APM_SECTOR {
	ULONG Signature;
	ULONG ApmTableSectors; // aka partition count
	ULONG SectorStart;
	ULONG SectorCount;
	char Name[32];
	char Type[32];
	ULONG RelativeSectorStartData;
	ULONG SectorCountData;
	ULONG Status; // "only used by A/UX"
	ULONG RelativeSectorStartBoot;
	ULONG LengthBoot;
	ULONG BootCodeBaseAddress[2];
	ULONG BootCodeEntryPoint[2];
	ULONG BootCodeChecksum;
	char BootCodeArchitecture[16];
	BYTE UnusedData[0x178];
} APM_SECTOR, *PAPM_SECTOR;
_Static_assert(sizeof(APM_SECTOR) == 0x200);

typedef struct ARC_BE ARC_PACKED _APM_SECTOR2K {
	APM_SECTOR Sector;
	BYTE UnusedData[0x600];
} APM_SECTOR2K, *PAPM_SECTOR2K;
_Static_assert(sizeof(APM_SECTOR2K) == 0x800);

enum {
	APM_VALID_SIGNATURE = 0x504D0000,
	DDT_VALID_SIGNATURE = 0x4552,
	APM_SECTOR_SIZE = 0x200,
	APM_SECTOR_SIZE_CD = 0x800,
};

static void ApmpInit(PAPM_SECTOR Apm, ULONG ApmPartitionCount, ULONG SectorStart, ULONG SectorCount, bool BootCode) {
	Apm->Signature = APM_VALID_SIGNATURE;
	Apm->ApmTableSectors = ApmPartitionCount;
	Apm->SectorStart = SectorStart;
	Apm->SectorCount = SectorCount;
	Apm->SectorCountData = SectorCount;
	// osx disk utility does this for some reason??
	memset(&Apm->UnusedData[0x14C], 0xFF, 4);
	Apm->Status = 0x7f;
	if (BootCode) {
		Apm->Status |= 0x300;
	}
}

static void ApmpInitVoid(PAPM_SECTOR Apm, ULONG ApmPartitionCount) {
	Apm->Signature = APM_VALID_SIGNATURE;
	Apm->ApmTableSectors = ApmPartitionCount;
	strncpy(Apm->Type, "Apple_Void", sizeof(Apm->Type));
}

static ULONG ApmpRolw1(USHORT Value) {
	return (Value << 1) | (Value >> 15);
}

// same algorithm as in OSX MediaKit
static ULONG ApmpDriverChecksum(PUCHAR Buffer, ULONG Length) {
	ULONG Checksum = 0;
	for (ULONG i = 0; i < Length; i++) {
		Checksum = ApmpRolw1((USHORT)(Checksum + Buffer[i]));
	}
	if (Checksum == 0) return 0xFFFF;
	return (USHORT)Checksum;
}

static void usage(char* arg0) {
	printf("oldiso: add APM + bootable HFS partition to ISO image\n");
	printf("usage: %s iso hfsimg Driver43.ptDR Driver43.CDrv DriverATAPI.ptDR DriverATAPI.ATPI Patches\n", arg0);
}

#define BAD_ARGS() do { \
	usage(argv[0]); \
	return -1; \
} while (0)

#define READ_FILE(var, i, len) \
	PUCHAR var = NULL; \
	do { \
		FILE* fFile = fopen(argv[i], "rb"); \
		if (fFile == NULL) { \
			printf("Could not open %s\n", argv[i]); \
			return -2; \
		} \
		fseek(fFile, 0, SEEK_END); \
		\
		__auto_type lenFile = ftello(fFile); \
		fseek(fFile, 0, SEEK_SET); \
		if (lenFile != (len)) { \
			if (lenFile > 0xFFFFFFFF) lenFile = 0xFFFFFFFF; \
			printf("%s is %d bytes, expected %d\n", argv[i], lenFile, (len)); \
			return -2; \
		} \
		var = (PUCHAR)malloc((len)); \
		if (var == NULL) { \
			printf("Could not allocate memory for %s\n", argv[i]); \
			return -2; \
		} \
		if (fread( var, 1, (len), fFile) != (len)) { \
			printf("Could not read %s\n", argv[i]); \
			return -2; \
		} \
		fclose(fFile); \
	} while (0)

#define READ_FILE_UNKLEN(var, varLen, i) \
	PUCHAR var = NULL; \
	ULONG varLen = 0; \
	do { \
		FILE* fFile = fopen(argv[i], "rb"); \
		if (fFile == NULL) { \
			printf("Could not open %s\n", argv[i]); \
			return -2; \
		} \
		fseek(fFile, 0, SEEK_END); \
		\
		__auto_type lenFile = ftello(fFile); \
		if (varLen > 0xFFFFFFFF) { \
			printf("%s cannot be over 4GB\n", argv[i]); \
			return -2; \
		} \
		if ((varLen % APM_SECTOR_SIZE_CD) != 0) { \
			printf("%s size must be a multiple of 2048 bytes\n", argv[i]); \
			return -2; \
		} \
		varLen = (ULONG)lenFile; \
		fseek(fFile, 0, SEEK_SET); \
		var = (PUCHAR)malloc(varLen); \
		if (var == NULL) { \
			printf("Could not allocate memory for %s\n", argv[i]); \
			return -2; \
		} \
		if (fread( var, 1, varLen, fFile) != varLen) { \
			printf("Could not read %s\n", argv[i]); \
			return -2; \
		} \
		fclose(fFile); \
	} while (0)

#define READ_FILE_MAXLEN_IMPL(var, varLen, i, len) \
	PUCHAR var = NULL; \
	ULONG varLen = 0; \
	do { \
		FILE* fFile = fopen(argv[i], "rb"); \
		if (fFile == NULL) { \
			printf("Could not open %s\n", argv[i]); \
			return -2; \
		} \
		fseek(fFile, 0, SEEK_END); \
		\
		__auto_type lenFile = ftello(fFile); \
		if (varLen > len) { \
			printf("%s cannot be over %d bytes\n", argv[i], len); \
			return -2; \
		} \
		if ((varLen % APM_SECTOR_SIZE_CD) != 0) { \
			printf("%s size must be a multiple of 2048 bytes\n", argv[i]); \
			return -2; \
		} \
		varLen = (ULONG)lenFile; \
		fseek(fFile, 0, SEEK_SET); \
		var = (PUCHAR)malloc((len)); \
		if (var == NULL) { \
			printf("Could not allocate memory for %s\n", argv[i]); \
			return -2; \
		} \
		memset(var, 0, len); \
		if (fread( var, 1, varLen, fFile) != varLen) { \
			printf("Could not read %s\n", argv[i]); \
			return -2; \
		} \
		fclose(fFile); \
	} while (0)

#define READ_FILE_MAXLEN(var, i, length) READ_FILE_MAXLEN_IMPL(p##var, len##var, i, length)

enum {
	LEN_DRV1 = 0x7000,
	LEN_DRV2 = 0x11800,
	LEN_DRV2_CAP = (LEN_DRV2 > 0xF800 ? 0xF800 : LEN_DRV2),
	LEN_PATCH = 0x40000,
	LEN_ALL_DRIVERS = LEN_DRV1 + LEN_DRV2 + LEN_DRV1 + LEN_DRV2 + LEN_PATCH,
	LEN_APM = 63 * APM_SECTOR_SIZE,
	SECTLEN_APM = 63,
	SECTLEN_DRV1 = LEN_DRV1 / APM_SECTOR_SIZE,
	SECTLEN_DRV2 = LEN_DRV2 / APM_SECTOR_SIZE,
	SECTLEN_PATCH = LEN_PATCH / APM_SECTOR_SIZE,
	
	SECTLEN_CD_APM = 15,
	SECTLEN_CD_DRV1 = LEN_DRV1 / APM_SECTOR_SIZE_CD,
	SECTLEN_CD_DRV2 = LEN_DRV2 / APM_SECTOR_SIZE_CD,
	SECTLEN_CD_PATCH = LEN_PATCH / APM_SECTOR_SIZE_CD,
};

#define APM_PART_TO_CD_APM_PART(i) ((i) - ((i) / 4))

#define FOR_EACH_APM(i) for ( \
	PAPM_SECTOR pApm = &Apm[i]; \
	pApm != NULL; \
	pApm = (pApm == &Apm[i] ? &Apm2K[APM_PART_TO_CD_APM_PART(i)].Sector : NULL) \
	)

int main(int argc, char** argv) {
	if (argc < 8) {
		BAD_ARGS();
	}
	
	// open the files
	FILE* fIso = fopen(argv[1], "r+b");
	if (fIso == NULL) {
		printf("Could not open %s\n", argv[1]);
		BAD_ARGS();
	}
	
	READ_FILE_UNKLEN(pHfsPart, lenHfsPart, 2);
	READ_FILE_MAXLEN(43ptDR, 3, LEN_DRV1);
	READ_FILE_MAXLEN(43CDrv, 4, LEN_DRV2);
	READ_FILE_MAXLEN(ATAptDR, 5, LEN_DRV1);
	READ_FILE_MAXLEN(ATAATPI, 6, LEN_DRV2);
	READ_FILE(pPatches, 7, LEN_PATCH);
	// if length of the main driver is too large, ptDR will crash
	// this works around someone passing whole driver partition images
	if (len43CDrv > LEN_DRV2_CAP) len43CDrv = LEN_DRV2_CAP;
	if (lenATAATPI > LEN_DRV2_CAP) lenATAATPI = LEN_DRV2_CAP;
	
	// how big is the ISO(bytes)?
	fseek(fIso, 0, SEEK_END);
	__auto_type lenIso = ftello(fIso);
	if (lenIso < 0x8400) {
		printf("%s is %d bytes, needs to be more than %d bytes\n", argv[1], lenIso, 0x8400);
		return -3;
	}
	if ((lenIso % APM_SECTOR_SIZE_CD) != 0) {
		printf("%s size must be a multiple of 2048 bytes\n", argv[1]);
		return -3;
	}
	// make sure there's an iso header here
	{
		BYTE hdr[5];
		fseek(fIso, 0x8001, SEEK_SET);
		fread(hdr, 5, 1, fIso);
		fseek(fIso, 0, SEEK_SET);
		if (memcmp(hdr, "CD001", sizeof(hdr)) != 0) {
			printf("%s doesn't appear to be an ISO\n", argv[1]);
			return -3;
		}
		fread(hdr, 2, 1, fIso);
		fseek(fIso, 0, SEEK_SET);
		if (hdr[0] != 0 || hdr[1] != 0) {
			printf("%s appears to already be a hybrid ISO\n", argv[1]);
			return -3;
		}
	}
	
	uint64_t TotalLength = (uint64_t)lenIso + lenHfsPart + LEN_ALL_DRIVERS;
	if ((TotalLength % APM_SECTOR_SIZE_CD) != 0) {
		TotalLength += APM_SECTOR_SIZE_CD - (TotalLength % APM_SECTOR_SIZE_CD);
	}
	ULONG TotalLengthSectors = (ULONG)(TotalLength / APM_SECTOR_SIZE);
	ULONG TotalLengthSectorsCd = (ULONG)(TotalLength / APM_SECTOR_SIZE_CD);
	ULONG PartitionLengths[] = {
		SECTLEN_APM,
		(lenIso - LEN_APM - APM_SECTOR_SIZE) / APM_SECTOR_SIZE,
		SECTLEN_DRV1,
		SECTLEN_DRV2,
		SECTLEN_DRV1,
		SECTLEN_DRV2,
		SECTLEN_PATCH,
		lenHfsPart / APM_SECTOR_SIZE
	};
	ULONG PartitionLengthsCd[] = {
		SECTLEN_CD_APM,
		(lenIso - LEN_APM - APM_SECTOR_SIZE) / APM_SECTOR_SIZE_CD,
		SECTLEN_CD_DRV1,
		SECTLEN_CD_DRV2,
		SECTLEN_CD_DRV1,
		SECTLEN_CD_DRV2,
		SECTLEN_CD_PATCH,
		lenHfsPart / APM_SECTOR_SIZE_CD
	};
	ULONG PartitionOffsets[8];
	ULONG PartitionOffsetsCd[8];
	_Static_assert(sizeof(PartitionOffsets) == sizeof(PartitionLengths));
	_Static_assert(sizeof(PartitionOffsets) == sizeof(PartitionLengthsCd));
	_Static_assert(sizeof(PartitionOffsetsCd) == sizeof(PartitionLengthsCd));
	PartitionOffsets[0] = 1;
	PartitionOffsetsCd[0] = 1;
	for (int i = 1; i < 8; i++) {
		PartitionOffsets[i] = PartitionOffsets[i - 1] + PartitionLengths[i - 1];
		PartitionOffsetsCd[i] = PartitionOffsetsCd[i - 1] + PartitionLengthsCd[i - 1];
	}
		
	
	// init DDT, APM entries
	UCHAR BlankSector[APM_SECTOR_SIZE] = {0};
	MBR_SECTOR Mbr;
	memset((void*)(size_t)&Mbr, 0, sizeof(Mbr));
	
	// DDT
	Mbr.ApmDdt.Signature = DDT_VALID_SIGNATURE;
	Mbr.ApmDdt.SectorSize = APM_SECTOR_SIZE_CD;
	Mbr.ApmDdt.SectorCount = TotalLengthSectorsCd;
	Mbr.ApmDdt.DeviceType = 1;
	Mbr.ApmDdt.DeviceId = 1;
	Mbr.ApmDdt.DriverCount = 4;
	Mbr.ApmDdt.Drivers[0].SectorStart = PartitionOffsetsCd[2];
	Mbr.ApmDdt.Drivers[0].SectorCount = PartitionLengthsCd[2];
	Mbr.ApmDdt.Drivers[0].OsType = 0x0001;
	Mbr.ApmDdt.Drivers[1].SectorStart = PartitionOffsetsCd[3];
	Mbr.ApmDdt.Drivers[1].SectorCount = PartitionLengthsCd[3];
	Mbr.ApmDdt.Drivers[1].OsType = 0xFFFF;
	Mbr.ApmDdt.Drivers[2].SectorStart = PartitionOffsetsCd[4];
	Mbr.ApmDdt.Drivers[2].SectorCount = PartitionLengthsCd[4];
	Mbr.ApmDdt.Drivers[2].OsType = 0x0701;
	Mbr.ApmDdt.Drivers[3].SectorStart = PartitionOffsetsCd[5];
	Mbr.ApmDdt.Drivers[3].SectorCount = PartitionLengthsCd[5];
	Mbr.ApmDdt.Drivers[3].OsType = 0xF8FF;
	
	// APM
	enum {
		ApmPartitionsCountBeforeFixup = 8,
		// fixup requires one blank (except for count and type) "Void" partition where 512 and 2048 byte sectors overlap
		ApmPartitionsCount = ApmPartitionsCountBeforeFixup + (ApmPartitionsCountBeforeFixup / 4),
		ApmPartitionsWritten512 = (ApmPartitionsCount + (4 - (ApmPartitionsCount & 3))) - 1,
	};
	_Static_assert(ApmPartitionsWritten512 >= ApmPartitionsCount);
	APM_SECTOR Apm[ApmPartitionsWritten512];
	APM_SECTOR2K Apm2K[ApmPartitionsCountBeforeFixup];
	memset((void*)(size_t)&Apm, 0, sizeof(Apm));
	memset((void*)(size_t)&Apm2K, 0, sizeof(Apm2K));
	_Static_assert((sizeof(Mbr) + sizeof(Apm2K) + sizeof(Apm)) < 0x8000);
	_Static_assert(((sizeof(Apm) + sizeof(Mbr)) % APM_SECTOR_SIZE_CD) == 0);
	// Partition 1: partition tables themselves
	ApmpInit(&Apm[0], ApmPartitionsCount, PartitionOffsets[0], PartitionLengths[0], false);
	ApmpInit(&Apm2K[0].Sector, ApmPartitionsCount, PartitionOffsetsCd[0], PartitionLengthsCd[0], false);
	FOR_EACH_APM(0) {
		strncpy(pApm->Name, "Apple", sizeof(Apm[0].Name));
		strncpy(pApm->Type, "Apple_partition_map", sizeof(Apm[0].Type));
		pApm->Status = 3;
	}
	// Partition 2: ISO9660
	ApmpInit(&Apm[1], ApmPartitionsCount, PartitionOffsets[1], PartitionLengths[1], false);
	ApmpInit(&Apm2K[1].Sector, ApmPartitionsCount, PartitionOffsetsCd[1], PartitionLengthsCd[1], false);
	FOR_EACH_APM(1) {
		strncpy(pApm->Name, "Extra", sizeof(Apm[1].Name));
		strncpy(pApm->Type, "CD_partition_scheme", sizeof(Apm[1].Type));
		pApm->Status = 0;
	}
	// Partition 3: SCSI4.3 ptDR
	ApmpInit(&Apm[2], ApmPartitionsCount, PartitionOffsets[2], PartitionLengths[2], true);
	ApmpInit(&Apm2K[2].Sector, ApmPartitionsCount, PartitionOffsetsCd[2], PartitionLengthsCd[2], true);
	FOR_EACH_APM(2) {
		strncpy(pApm->Name, "Macintosh", sizeof(Apm[2].Name));
		strncpy(pApm->Type, "Apple_Driver43", sizeof(Apm[2].Type));
		memcpy(pApm->UnusedData, "ptDR", sizeof("ptDR"));
		pApm->LengthBoot = len43ptDR;
		pApm->BootCodeChecksum = ApmpDriverChecksum(p43ptDR, len43ptDR);
	}
	// Partition 4: void
	ApmpInitVoid(&Apm[3], ApmPartitionsCount);
	// Partition 5(4): SCSI4.3 CDrv
	ApmpInit(&Apm[4], ApmPartitionsCount, PartitionOffsets[3], PartitionLengths[3], true);
	ApmpInit(&Apm2K[3].Sector, ApmPartitionsCount, PartitionOffsetsCd[3], PartitionLengthsCd[3], true);
	FOR_EACH_APM(4) {
		strncpy(pApm->Name, "Macintosh", sizeof(Apm[4].Name));
		strncpy(pApm->Type, "Apple_Driver43_CD", sizeof(Apm[4].Type));
		memcpy(pApm->UnusedData, "CDrv", sizeof("CDrv"));
		pApm->LengthBoot = len43CDrv;
		pApm->BootCodeChecksum = ApmpDriverChecksum(p43CDrv, len43CDrv);
	}
	// Partition 6(5): ATAPI ptDR
	ApmpInit(&Apm[5], ApmPartitionsCount, PartitionOffsets[4], PartitionLengths[4], true);
	ApmpInit(&Apm2K[4].Sector, ApmPartitionsCount, PartitionOffsetsCd[4], PartitionLengthsCd[4], true);
	FOR_EACH_APM(5) {
		strncpy(pApm->Name, "Macintosh", sizeof(Apm[5].Name));
		strncpy(pApm->Type, "Apple_Driver_ATAPI", sizeof(Apm[5].Type));
		memcpy(pApm->UnusedData, "ptDR", sizeof("ptDR"));
		pApm->LengthBoot = lenATAptDR;
		pApm->BootCodeChecksum = ApmpDriverChecksum(pATAptDR, lenATAptDR);
	}
	// Partition 7(6): ATAPI ATPI
	ApmpInit(&Apm[6], ApmPartitionsCount, PartitionOffsets[5], PartitionLengths[5], true);
	ApmpInit(&Apm2K[5].Sector, ApmPartitionsCount, PartitionOffsetsCd[5], PartitionLengthsCd[5], true);
	FOR_EACH_APM(6) {
		strncpy(pApm->Name, "Macintosh", sizeof(Apm[6].Name));
		strncpy(pApm->Type, "Apple_Driver_ATAPI", sizeof(Apm[6].Type));
		memcpy(pApm->UnusedData, "ATPI", sizeof("ATPI"));
		pApm->LengthBoot = lenATAATPI;
		pApm->BootCodeChecksum = ApmpDriverChecksum(pATAATPI, lenATAATPI);
	}
	// Partition 8: void
	ApmpInitVoid(&Apm[7], ApmPartitionsCount);
	// Partition 9(7): Apple Patch partition
	ApmpInit(&Apm[8], ApmPartitionsCount, PartitionOffsets[6], PartitionLengths[6], false);
	ApmpInit(&Apm2K[6].Sector, ApmPartitionsCount, PartitionOffsetsCd[6], PartitionLengthsCd[6], false);
	FOR_EACH_APM(8) {
		strncpy(pApm->Name, "Patch Partition", sizeof(Apm[8].Name));
		strncpy(pApm->Type, "Apple_Patches", sizeof(Apm[8].Type));
		pApm->Status = 1;
	}
	// Partition 10(8): HFS boot partition
	ApmpInit(&Apm[9], ApmPartitionsCount, PartitionOffsets[7], PartitionLengths[7], false);
	ApmpInit(&Apm2K[7].Sector, ApmPartitionsCount, PartitionOffsetsCd[7], PartitionLengthsCd[7], false);
	FOR_EACH_APM(9) {
		strncpy(pApm->Name, "Windows NT", sizeof(Apm[9].Name));
		strncpy(pApm->Type, "Apple_HFS", sizeof(Apm[9].Type));
		pApm->Status = 0x40000033;
	}
	
	// Write DDT then APM
	if (fwrite((void*)(size_t)&Mbr, 1, sizeof(Mbr), fIso) != sizeof(Mbr)) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite((void*)(size_t)&Apm, 1, sizeof(Apm), fIso) != sizeof(Apm)) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite((void*)(size_t)&Apm2K, 1, sizeof(Apm2K), fIso) != sizeof(Apm2K)) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	
	// Seek to end, write partition 3+ data out
	fseek(fIso, 0, SEEK_END);
	if (fwrite(p43ptDR, 1, LEN_DRV1, fIso) != LEN_DRV1) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite(p43CDrv, 1, LEN_DRV2, fIso) != LEN_DRV2) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite(pATAptDR, 1, LEN_DRV1, fIso) != LEN_DRV1) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite(pATAATPI, 1, LEN_DRV2, fIso) != LEN_DRV2) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite(pPatches, 1, LEN_PATCH, fIso) != LEN_PATCH) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	if (fwrite(pHfsPart, 1, lenHfsPart, fIso) != lenHfsPart) {
		printf("Could not write to %s\n", argv[1]);
		return -4;
	}
	
	fflush(fIso);
	fclose(fIso);
	return 0;
}
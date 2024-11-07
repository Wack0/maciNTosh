// Defines runtime variables, shared from ARC fw to OS.
#pragma once

// if we're building for NT, then we're LE.
#ifndef ARC_LE
#define __BUILDING_FOR_NT__
#define ARC_LE
#define ARC_BE __attribute__((scalar_storage_order("big-endian")))
#define ARC_ALIGNED(x) __attribute__((aligned(x)))
#define ARC_PACKED __attribute__((packed))
#define BIT(x) (1 << (x))

#define RUNTIME_BLOCK (*(PVOID**)(0x8000403C))

typedef struct ARC_BE _U32BE {
	ULONG v;
} U32BE, *PU32BE;

static inline BOOLEAN _Mmio_IsLittleEndian(void) {
#ifdef __FORCE_MSR_LE__ // used for HALs where the status of device endianness is known at compile time
#ifdef __BIG_ENDIAN_SYSTEM__ // uni-north removed the endianness switch bit from bandit
	return FALSE;
#else
	return TRUE;
#endif
#else
	// All systems are running MSR_LE.
	// NT does not use any IBAT other than IBAT0.
	// Therefore, use IBAT3L bit 0. (bit 31, from a little endian viewpoint)
	// The HAL on a fully big-endian system can set this bit as early as possible;
	// IBAT3U is left at zero, to ensure the BAT won't ever be used.
	ULONG _ibat3l;
	__asm__ __volatile( "mfspr %0, 535" : "=r" ((_ibat3l)) );
	return (_ibat3l & 1) == 0;
#if 0 // old code checking MSR_LE
	ULONG _msr;
	__asm__ __volatile( "mfmsr %0" : "=r" ((_msr)) );
	return (_msr & 1) != 0;
#endif
#endif
}

#else
static inline BOOLEAN _Mmio_IsLittleEndian(void) {
#ifdef __BIG_ENDIAN_SYSTEM__
	return 0;
#else
	return __LITTLE_ENDIAN__;
#endif
}
#endif

// Given the address, and the length to access, munge the address to counteract what the CPU does with MSR_LE enabled.
// Length must be 1, 2, 4, or 8, this won't be checked for.
static inline ULONG _Mmio_MungeAddressConstant(ULONG Length) {
	// 1 => 7, 2 => 6, 4 => 4, 8 => 0
	// this is enough, and should be calculated at compile time :)
	return (8 - Length);
}
static inline PVOID _Mmio_MungeAddressForBig(PVOID Address, ULONG Length) {
	// do nothing for 64 bits.
	if (Length == 8) return Address;
	
	ULONG Addr32 = (ULONG)Address;
	ULONG AlignOffset = Addr32 & (Length - 1);
	if (AlignOffset == 0) {
		// Aligned access, just XOR with munge constant.
		return (PVOID)(Addr32 ^ _Mmio_MungeAddressConstant(Length));
	}
	
	// Unaligned access.
	// Convert the address to an aligned address.
	Addr32 &= ~(Length - 1);
	// XOR with munge constant
	Addr32 ^= _Mmio_MungeAddressConstant(Length);
	// And subtract the align offset.
	return (PVOID)(Addr32 - AlignOffset);
}

static inline UCHAR MmioRead8(PVOID addr) {
	if (!_Mmio_IsLittleEndian()) addr = _Mmio_MungeAddressForBig(addr, 1);
	return *(volatile UCHAR*)addr;
}

static inline ULONG MmioRead32(PVOID addr)
{
	ULONG x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; lwbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
		return x;
	}
	addr = _Mmio_MungeAddressForBig(addr, 4);
	__asm__ __volatile__(
		"sync ; lwz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
	return x;
}

static inline void MmioWrite32(PVOID addr, ULONG x)
{
	if (_Mmio_IsLittleEndian()) {
		__asm__ __volatile__(
			"sync ; stwbrx %0,0,%1 ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	addr = _Mmio_MungeAddressForBig(addr, 4);
	__asm__ __volatile__(
		"sync ; stw %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
}

static inline ULONG MmioRead32L(PVOID addr)
{
	ULONG x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; lwz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
		return x;
	}
	addr = _Mmio_MungeAddressForBig(addr, 4);
	__asm__ __volatile__(
		"sync ; lwbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
	return x;
}

static inline void MmioWrite32L(PVOID addr, ULONG x)
{
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; stw %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	addr = _Mmio_MungeAddressForBig(addr, 4);
	__asm__ __volatile__(
		"sync ; stwbrx %0,0,%1 ; eieio" : : "r"(x), "r"(addr));
}

static inline USHORT MmioRead16(PVOID addr)
{
	USHORT x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; lhbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
		return x;
	}
	addr = _Mmio_MungeAddressForBig(addr, 2);
	__asm__ __volatile__(
		"sync ; lhz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
	return x;
}

static inline void MmioWrite16(PVOID addr, USHORT x)
{
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; sthbrx %0,0,%1 ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	addr = _Mmio_MungeAddressForBig(addr, 2);
	__asm__ __volatile__(
		"sync ; sth %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
}

static inline USHORT MmioRead16L(PVOID addr)
{
	USHORT x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; lhz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
		return x;
	}
	addr = _Mmio_MungeAddressForBig(addr, 2);
	__asm__ __volatile__(
		"sync ; lhbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
	return x;
}

static inline void MmioWrite16L(PVOID addr, USHORT x)
{
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sync ; sth %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	addr = _Mmio_MungeAddressForBig(addr, 2);
	__asm__ __volatile__(
		"sync ; sthbrx %0,0,%1 ; eieio" : : "r"(x), "r"(addr));
}

static inline void MmioWrite8(PVOID addr, UCHAR x) {
	if (!_Mmio_IsLittleEndian()) addr = _Mmio_MungeAddressForBig(addr, 1);
	__asm__ __volatile__(
		"sync ; stb %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
}

#define __MMIO_BUF_READ_BODY(func) \
	for (ULONG readCount = 0; readCount < len; readCount++) { \
		buf[readCount] = func(addr); \
	}

#define __MMIO_BUF_WRITE_BODY(func) \
	for (ULONG writeCount = 0; writeCount < len; writeCount++) { \
		func(addr, buf[writeCount]); \
	}

static inline void MmioReadBuf8(PVOID addr, PUCHAR buf, ULONG len) {
	__MMIO_BUF_READ_BODY(*(volatile UCHAR * const));
}

static inline void MmioReadBuf16(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_READ_BODY(MmioRead16);
}

static inline void MmioReadBuf16L(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_READ_BODY(MmioRead16L);
}

static inline void MmioReadBuf32(PVOID addr, PULONG buf, ULONG len) {
	__MMIO_BUF_READ_BODY(MmioRead32);
}

static inline void MmioReadBuf32L(PVOID addr, PULONG buf, ULONG len) {
	__MMIO_BUF_READ_BODY(MmioRead32L);
}

static inline void MmioWriteBuf8(PVOID addr, PUCHAR buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite8);
}

static inline void MmioWriteBuf16(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite16);
}

static inline void MmioWriteBuf16L(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite16L);
}

static inline void MmioWriteBuf32(PVOID addr, PULONG buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite32);
}

static inline void MmioWriteBuf32L(PVOID addr, PULONG buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite32L);
}

static inline ULONG NativeRead32(PVOID addr)
{
	ULONG x;
	if (_Mmio_IsLittleEndian()) return *(PULONG)addr;
	if (!_Mmio_IsLittleEndian()) addr = _Mmio_MungeAddressForBig(addr, 4);
	__asm__ __volatile__(
		"sync ; lwz %0,0(%1)" : "=r"(x) : "b"(addr));
	return x;
}

static inline void NativeWrite32(PVOID addr, ULONG x)
{
	if (_Mmio_IsLittleEndian()) {
		*(PULONG)addr = x;
		return;
	}
	if (!_Mmio_IsLittleEndian()) addr = _Mmio_MungeAddressForBig(addr, 4);
	__asm__ __volatile__(
		"stw %0,0(%1)" : : "r"(x), "b"(addr));
}

static inline USHORT NativeRead16(PVOID addr)
{
	USHORT x;
	if (_Mmio_IsLittleEndian()) return *(PUSHORT)addr;
	if (!_Mmio_IsLittleEndian()) addr = _Mmio_MungeAddressForBig(addr, 2);
	__asm__ __volatile__(
		"lhz %0,0(%1)" : "=r"(x) : "b"(addr));
	return x;
}

static inline void NativeWrite16(PVOID addr, USHORT x)
{
	if (_Mmio_IsLittleEndian()) {
		*(PUSHORT)addr = x;
		return;
	}
	if (!_Mmio_IsLittleEndian()) addr = _Mmio_MungeAddressForBig(addr, 2);
	__asm__ __volatile__(
		"sth %0,0(%1)" : : "r"(x), "b"(addr));
}

//#ifndef RUNTIME_NO_ARC
//#include <arc.h>
//#define RUNTIME_BLOCK (*(PVOID**)((ULONG)SYSTEM_BLOCK + sizeof(SYSTEM_PARAMETER_BLOCK)))
//#endif

enum {
	RUNTIME_FRAME_BUFFER,
	RUNTIME_IN_EMULATOR,
	RUNTIME_DECREMENTER_FREQUENCY,
	RUNTIME_RAMDISK,
	RUNTIME_HAS_CUDA,
	RUNTIME_PCI_INTERRUPTS,
	RUNTIME_HAS_NO_ADB
};

typedef struct ARC_LE _FRAME_BUFFER {
	union ARC_LE {
		PVOID Pointer;
		ULONG PointerArc;
	};
	ULONG Length;
	ULONG Width;
	ULONG Height;
	ULONG Stride;
} FRAME_BUFFER, *PFRAME_BUFFER;

typedef struct ARC_LE _MEMORY_AREA {
	ULONG PointerArc;
	ULONG Length;
} MEMORY_AREA, *PMEMORY_AREA;

typedef struct ARC_LE _RAMDISK_DESC {
	ULONG ControllerKey;
	MEMORY_AREA Buffer;
} RAMDISK_DESC, *PRAMDISK_DESC;

typedef struct ARC_LE _PCI_INTERRUPT {
	UCHAR Slot;
	UCHAR Vector;
} PCI_INTERRUPT, *PPCI_INTERRUPT;

#ifndef __BUILDING_FOR_NT__
typedef struct ARC_LE {
	U32LE RuntimePointers[16];
	FRAME_BUFFER RuntimeFb;
	RAMDISK_DESC Ramdisk;
	PCI_INTERRUPT PciInterrupts[32];
} RUNTIME_AREA, * PRUNTIME_AREA;

#define s_RuntimeArea ((PRUNTIME_AREA)0x80005000)
#define s_RuntimePointers s_RuntimeArea->RuntimePointers
#define s_RuntimeFb s_RuntimeArea->RuntimeFb
#define s_RuntimeRamdisk s_RuntimeArea->Ramdisk
#define s_RuntimePciInt s_RuntimeArea->PciInterrupts
#endif

#define STACK_ALIGN(type, name, cnt, alignment)		UCHAR _al__##name[((sizeof(type)*(cnt)) + (alignment) + (((sizeof(type)*(cnt))%(alignment)) > 0 ? ((alignment) - ((sizeof(type)*(cnt))%(alignment))) : 0))]; \
													type *name = (type*)(((ULONG)(_al__##name)) + ((alignment) - (((ULONG)(_al__##name))&((alignment)-1))))
#define IDENTIFIER_MIO "VME"
// Defines runtime variables, shared from ARC fw to OS.
#pragma once

// if we're building for NT, then we're LE.
#ifndef ARC_LE
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
#ifdef __FORCE_MSR_LE__ // used for HALs where it is known at compile time
	return TRUE;
#else
	ULONG _msr;
	__asm__ __volatile( "mfmsr %0" : "=r" ((_msr)) );
	return (_msr & 1) != 0;
#endif
}

// following macro emits an "EMU_OP .",
// loader hooks will patch any EMU_OP to nop
// and skip following instruction for emu/aot.
#define FOLLOWING_ACCESS_BIG_ASM ".long 0xe0000000 \n "
#define FOLLOWING_ACCESS_BIG __asm__ __volatile__ ( FOLLOWING_ACCESS_BIG_ASM )
#else
static inline BOOLEAN _Mmio_IsLittleEndian(void) {
	return __LITTLE_ENDIAN__;
}
#define FOLLOWING_ACCESS_BIG_ASM
#endif

static inline UCHAR MmioRead8(PVOID addr) {
	return *(volatile UCHAR*)addr;
}

static inline ULONG MmioRead32(PVOID addr)
{
	ULONG x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"lwbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
		return x;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"lwz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
	return x;
}

static inline void MmioWrite32(PVOID addr, ULONG x)
{
	if (_Mmio_IsLittleEndian()) {
		__asm__ __volatile__(
			"stwbrx %0,0,%1 ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"stw %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
}

static inline ULONG MmioRead32L(PVOID addr)
{
	ULONG x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"lwz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
		return x;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"lwbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
	return x;
}

static inline void MmioWrite32L(PVOID addr, ULONG x)
{
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"stw %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"stwbrx %0,0,%1 ; eieio" : : "r"(x), "r"(addr));
}

static inline USHORT MmioRead16(PVOID addr)
{
	USHORT x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"lhbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
		return x;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"lhz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
	return x;
}

static inline void MmioWrite16(PVOID addr, USHORT x)
{
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sthbrx %0,0,%1 ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"sth %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
}

static inline USHORT MmioRead16L(PVOID addr)
{
	USHORT x;
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"lhz %0,0(%1) ; sync" : "=r"(x) : "b"(addr));
		return x;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"lhbrx %0,0,%1 ; sync" : "=r"(x) : "r"(addr));
	return x;
}

static inline void MmioWrite16L(PVOID addr, USHORT x)
{
	if (_Mmio_IsLittleEndian()) {
	__asm__ __volatile__(
		"sth %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
		return;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"sthbrx %0,0,%1 ; eieio" : : "r"(x), "r"(addr));
}

static inline void MmioWrite8(PVOID addr, UCHAR x) {

	__asm__ __volatile__(
		"stb %0,0(%1) ; eieio" : : "r"(x), "b"(addr));
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

static inline void MmioReadBuf32(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_READ_BODY(MmioRead32);
}

static inline void MmioReadBuf32L(PVOID addr, PUSHORT buf, ULONG len) {
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

static inline void MmioWriteBuf32(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite32);
}

static inline void MmioWriteBuf32L(PVOID addr, PUSHORT buf, ULONG len) {
	__MMIO_BUF_WRITE_BODY(MmioWrite32L);
}

static inline ULONG NativeRead32(PVOID addr)
{
	ULONG x;
	if (_Mmio_IsLittleEndian()) return *(PULONG)addr;
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"lwz %0,0(%1)" : "=r"(x) : "b"(addr));
	return x;
}

static inline void NativeWrite32(PVOID addr, ULONG x)
{
	if (_Mmio_IsLittleEndian()) {
		*(PULONG)addr = x;
		return;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"stw %0,0(%1)" : : "r"(x), "b"(addr));
}

static inline USHORT NativeRead16(PVOID addr)
{
	USHORT x;
	if (_Mmio_IsLittleEndian()) return *(PUSHORT)addr;
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
		"lhz %0,0(%1)" : "=r"(x) : "b"(addr));
	return x;
}

static inline void NativeWrite16(PVOID addr, USHORT x)
{
	if (_Mmio_IsLittleEndian()) {
		*(PUSHORT)addr = x;
		return;
	}
	__asm__ __volatile__(
		FOLLOWING_ACCESS_BIG_ASM
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
	RUNTIME_HAS_CUDA
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

#ifndef FOLLOWING_ACCESS_BIG
typedef struct ARC_LE {
	U32LE RuntimePointers[16];
	FRAME_BUFFER RuntimeFb;
	RAMDISK_DESC Ramdisk;
} RUNTIME_AREA, * PRUNTIME_AREA;

#define s_RuntimeArea ((PRUNTIME_AREA)0x80005000)
#define s_RuntimePointers s_RuntimeArea->RuntimePointers
#define s_RuntimeFb s_RuntimeArea->RuntimeFb
#define s_RuntimeRamdisk s_RuntimeArea->Ramdisk
#endif

#define STACK_ALIGN(type, name, cnt, alignment)		UCHAR _al__##name[((sizeof(type)*(cnt)) + (alignment) + (((sizeof(type)*(cnt))%(alignment)) > 0 ? ((alignment) - ((sizeof(type)*(cnt))%(alignment))) : 0))]; \
													type *name = (type*)(((ULONG)(_al__##name)) + ((alignment) - (((ULONG)(_al__##name))&((alignment)-1))))
#define IDENTIFIER_MIO "VME"
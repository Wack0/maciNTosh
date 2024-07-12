#ifndef __PROCESSOR_H__
#define __PROCESSOR_H__

#include "types.h"
#include "asm.h"

#define __stringify(rn)								#rn
#define ATTRIBUTE_ALIGN(v)							__attribute__((aligned(v)))
// courtesy of Marcan
#define STACK_ALIGN(type, name, cnt, alignment)		u8 _al__##name[((sizeof(type)*(cnt)) + (alignment) + (((sizeof(type)*(cnt))%(alignment)) > 0 ? ((alignment) - ((sizeof(type)*(cnt))%(alignment))) : 0))]; \
													type *name = (type*)(((u32)(_al__##name)) + ((alignment) - (((u32)(_al__##name))&((alignment)-1))))

#define _sync() __asm__ __volatile__ ("sync")
#define _nop() __asm__ __volatile__ ("nop")
#define ppcsync() __asm__ __volatile__ ("sc")
#define ppchalt() ({						\
	__asm__ __volatile__ ("sync");			\
	while(1) {								\
		__asm__ __volatile__ ("nop");		\
		__asm__ __volatile__ ("li 3,0");	\
		__asm__ __volatile__ ("nop");		\
	}										\
})

#define mfpvr() ({u32 _rval; \
		__asm__ __volatile__ ("mfpvr %0" : "=r"(_rval)); _rval;})

#define mfdcr(_rn) ({u32 _rval; \
		__asm__ __volatile__ ("mfdcr %0," __stringify(_rn) \
             : "=r" (_rval)); _rval;})
#define mtdcr(rn, val)  __asm__ __volatile__ ("mtdcr " __stringify(rn) ",%0" : : "r" (val))

#define mfmsr()   ({u32 _rval; \
		__asm__ __volatile__ ("mfmsr %0" : "=r" (_rval)); _rval;})
#define mtmsr(val)  __asm__ __volatile__ ("mtmsr %0" : : "r" (val))

#define mfdec()   ({u32 _rval; \
		__asm__ __volatile__ ("mfdec %0" : "=r" (_rval)); _rval;})
#define mtdec(_val)  __asm__ __volatile__ ("mtdec %0" : : "r" (_val))

#define mfspr(_rn) \
({	u32 _rval = 0; \
	__asm__ __volatile__ ("mfspr %0," __stringify(_rn) \
	: "=r" (_rval));\
	_rval; \
})

#define mtspr(_rn, _val) __asm__ __volatile__ ("mtspr " __stringify(_rn) ",%0" : : "r" (_val))

#define mfwpar()		mfspr(WPAR)
#define mtwpar(_val)	mtspr(WPAR,_val)

#define mfmmcr0()		mfspr(MMCR0)
#define mtmmcr0(_val)	mtspr(MMCR0,_val)
#define mfmmcr1()		mfspr(MMCR1)
#define mtmmcr1(_val)	mtspr(MMCR1,_val)

#define mfpmc1()		mfspr(PMC1)
#define mtpmc1(_val)	mtspr(PMC1,_val)
#define mfpmc2()		mfspr(PMC2)
#define mtpmc2(_val)	mtspr(PMC2,_val)
#define mfpmc3()		mfspr(PMC3)
#define mtpmc3(_val)	mtspr(PMC3,_val)
#define mfpmc4()		mfspr(PMC4)
#define mtpmc4(_val)	mtspr(PMC4,_val)

#define mfhid0()		mfspr(HID0)
#define mthid0(_val)	mtspr(HID0,_val)
#define mfhid1()		mfspr(HID1)
#define mthid1(_val)	mtspr(HID1,_val)
#define mfhid2()		mfspr(HID2)
#define mthid2(_val)	mtspr(HID2,_val)
#define mfhid4()		mfspr(HID4)
#define mthid4(_val)	mtspr(HID4,_val)

#define __lhbrx(base,index)			\
({	u16 res;				\
	__asm__ volatile ("lhbrx	%0,%1,%2" : "=r"(res) : "b%"(index), "r"(base) : "memory"); \
	res; })

#define __lwbrx(base,index)			\
({	u32 res;				\
	__asm__ volatile ("lwbrx	%0,%1,%2" : "=r"(res) : "b%"(index), "r"(base) : "memory"); \
	res; })

#define __sthbrx(base,index,value)	\
	__asm__ volatile ("sthbrx	%0,%1,%2" : : "r"(value), "b%"(index), "r"(base) : "memory")

#define __stwbrx(base,index,value)	\
	__asm__ volatile ("stwbrx	%0,%1,%2" : : "r"(value), "b%"(index), "r"(base) : "memory")

#define cntlzw(_val) ({u32 _rval; \
					  __asm__ __volatile__ ("cntlzw %0, %1" : "=r"((_rval)) : "r"((_val))); _rval;})

#define _CPU_MSR_GET( _msr_value ) \
  do { \
    _msr_value = 0; \
    __asm__ __volatile__  ("mfmsr %0" : "=&r" ((_msr_value)) : "0" ((_msr_value))); \
  } while (0)

#define _CPU_MSR_SET( _msr_value ) \
{ __asm__ __volatile__  ("mtmsr %0" : "=&r" ((_msr_value)) : "0" ((_msr_value))); }

#define _CPU_ISR_Enable() \
	do { \
		u32 _val = 0; \
		__asm__ __volatile__ ( \
			"mfmsr %0\n" \
			"ori %0,%0,0x8000\n" \
			"mtmsr %0" \
			: "=&r" ((_val)) : "0" ((_val)) \
			: : "memory" \
		); \
	} while (0)

#define _CPU_ISR_Disable( _isr_cookie ) \
	do { \
		u32 _disable_mask = 0; \
		_isr_cookie = 0; \
		__asm__ __volatile__ ( \
			"mfmsr %0\n" \
			"rlwinm %1,%0,0,17,15\n" \
			"mtmsr %1\n" \
			"extrwi %0,%0,1,16" \
			: "=&r" ((_isr_cookie)), "=&r" ((_disable_mask)) \
			: "0" ((_isr_cookie)), "1" ((_disable_mask)) \
			: "memory" \
		); \
	} while (0)

#define _CPU_ISR_Restore( _isr_cookie )  \
	do { \
		u32 _enable_mask = 0; \
		__asm__ __volatile__ ( \
			"cmpwi %0,0\n" \
			"beq 1f\n" \
			"mfmsr %1\n" \
			"ori %1,%1,0x8000\n" \
			"mtmsr %1\n" \
			"1:" \
			: "=r"((_isr_cookie)),"=&r" ((_enable_mask)) \
			: "0"((_isr_cookie)),"1" ((_enable_mask)) \
			: "memory" \
		); \
	} while (0)

#define _CPU_ISR_Flash( _isr_cookie ) \
	do { \
		u32 _flash_mask = 0; \
		__asm__ __volatile__ ( \
			"cmpwi %0,0\n" \
			"beq 1f\n" \
			"mfmsr %1\n" \
			"ori %1,%1,0x8000\n" \
			"mtmsr %1\n" \
			"rlwinm %1,%1,0,17,15\n" \
			"mtmsr %1\n" \
			"1:" \
			: "=r" ((_isr_cookie)), "=&r" ((_flash_mask)) \
			: "0" ((_isr_cookie)), "1" ((_flash_mask)) \
			: "memory" \
		); \
	} while (0)

#define _CPU_FPR_Enable() \
{ u32 _val = 0; \
	  __asm__ __volatile__  ("mfmsr %0; ori %0,%0,0x2000; mtmsr %0" : \
					"=&r" (_val) : "0" (_val));\
}

#define _CPU_FPR_Disable() \
{ u32 _val = 0; \
	  __asm__ __volatile__  ("mfmsr %0; rlwinm %0,%0,0,19,17; mtmsr %0" : \
					"=&r" (_val) : "0" (_val));\
}

#ifdef __cplusplus
   extern "C" {
#endif /* __cplusplus */

static inline u16 bswap16(u16 val)
{
	u16 tmp = val;
	return __lhbrx(&tmp,0);
}

static inline u32 bswap32(u32 val)
{
	u32 tmp = val;
	return __lwbrx(&tmp,0);
}

static inline u64 bswap64(u64 val)
{
	union ullc {
		u64 ull;
		u32 ul[2];
	} outv;
	u64 tmp = val;

	outv.ul[0] = __lwbrx(&tmp,4);
	outv.ul[1] = __lwbrx(&tmp,0);

	return outv.ull;
}

// Basic I/O

static inline u32 read32(u32 addr)
{
	u32 x;
	__asm__ __volatile__ ("lwz %0,0(%1) ; sync" : "=r"(x) : "b"(0xc0000000 | addr));
	return x;
}

static inline void write32(u32 addr, u32 x)
{
	__asm__ ("stw %0,0(%1) ; eieio" : : "r"(x), "b"(0xc0000000 | addr));
}

static inline void mask32(u32 addr, u32 clear, u32 set)
{
	write32(addr, (read32(addr)&(~clear)) | set);
}

static inline u16 read16(u32 addr)
{
	u16 x;
	__asm__ __volatile__ ("lhz %0,0(%1) ; sync" : "=r"(x) : "b"(0xc0000000 | addr));
	return x;
}

static inline void write16(u32 addr, u16 x)
{
	__asm__ ("sth %0,0(%1) ; eieio" : : "r"(x), "b"(0xc0000000 | addr));
}

static inline u8 read8(u32 addr)
{
	u8 x;
	__asm__ __volatile__ ("lbz %0,0(%1) ; sync" : "=r"(x) : "b"(0xc0000000 | addr));
	return x;
}

static inline void write8(u32 addr, u8 x)
{
	__asm__ ("stb %0,0(%1) ; eieio" : : "r"(x), "b"(0xc0000000 | addr));
}

static inline void writef32(u32 addr, f32 x)
{
	__asm__ ("stfs %0,0(%1) ; eieio" : : "f"(x), "b"(0xc0000000 | addr));
}

#ifdef __cplusplus
   }
#endif /* __cplusplus */

#endif

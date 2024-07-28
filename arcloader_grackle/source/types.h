#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef _MSC_VER // Microsoft compilers

#define GET_ARG_COUNT(...)  INTERNAL_EXPAND_ARGS_PRIVATE(INTERNAL_ARGS_AUGMENTER(__VA_ARGS__))

#define INTERNAL_ARGS_AUGMENTER(...) unused, __VA_ARGS__
#define INTERNAL_EXPAND(x) x
#define INTERNAL_EXPAND_ARGS_PRIVATE(...) INTERNAL_EXPAND(INTERNAL_GET_ARG_COUNT_PRIVATE(__VA_ARGS__, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define INTERNAL_GET_ARG_COUNT_PRIVATE(_1_, _2_, _3_, _4_, _5_, _6_, _7_, _8_, _9_, _10_, _11_, _12_, _13_, _14_, _15_, _16_, _17_, _18_, _19_, _20_, _21_, _22_, _23_, _24_, _25_, _26_, _27_, _28_, _29_, _30_, _31_, _32_, _33_, _34_, _35_, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, count, ...) count

#else // Non-Microsoft compilers

#define GET_ARG_COUNT(...) INTERNAL_GET_ARG_COUNT_PRIVATE(0, ## __VA_ARGS__, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define INTERNAL_GET_ARG_COUNT_PRIVATE(_0, _1_, _2_, _3_, _4_, _5_, _6_, _7_, _8_, _9_, _10_, _11_, _12_, _13_, _14_, _15_, _16_, _17_, _18_, _19_, _20_, _21_, _22_, _23_, _24_, _25_, _26_, _27_, _28_, _29_, _30_, _31_, _32_, _33_, _34_, _35_, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, count, ...) count

#endif

#define IN
#define OUT
#define INOUT
#define OPTIONAL

#define VOID void

#define ARC_BIT(x) (1 << (x))
#define ARC_MB(x) ((x) * 1024 * 1024)

#ifdef __INTELLISENSE__
#define ARC_LE
#define ARC_BE
#define ARC_PACKED
#define ARC_FORCEINLINE
#define ARC_ALIGNED(x)
#define ARC_NORETURN
#define ARC_NOINLINE
#else
#define ARC_LE __attribute__((scalar_storage_order("little-endian")))
#define ARC_BE
#define ARC_PACKED __attribute__((packed))
#define ARC_FORCEINLINE __attribute__((always_inline))
#define ARC_ALIGNED(x) __attribute__((aligned(x)))
#define ARC_NORETURN __attribute__((noreturn))
#define ARC_NOINLINE __attribute__((noinline))
#endif

typedef void* PVOID;
typedef char CHAR, * PCHAR;
typedef uint8_t UCHAR, * PUCHAR, BYTE, * PBYTE, BOOLEAN, * PBOOLEAN;
typedef int16_t CSHORT, * PCSHORT, SHORT, * PSHORT;
typedef uint16_t WORD, * PWORD, USHORT, * PUSHORT;
typedef int32_t LONG, * PLONG;
typedef uint32_t ULONG, * PULONG;

typedef uint16_t WCHAR, * PWCHAR;

typedef uint8_t u8;

// Specify LARGE_INTEGER as 64-bit values are word-swapped by callers
typedef union ARC_LE _LARGE_INTEGER {
    struct ARC_LE {
        ULONG LowPart;
        LONG HighPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER, * PLARGE_INTEGER;

typedef union ARC_BE _LARGE_INTEGER_BIG {
    struct ARC_BE {
        LONG HighPart;
        ULONG LowPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER_BIG, * PLARGE_INTEGER_BIG;

typedef LARGE_INTEGER PHYSICAL_ADDRESS, * PPHYSICAL_ADDRESS;

static inline ARC_FORCEINLINE int64_t LargeIntegerToInt64(LARGE_INTEGER li) {
    return li.QuadPart;
}

#define INT32_TO_LARGE_INTEGER(i32) { .LowPart = (i32), .HighPart = 0 }
//#define INT64_TO_LARGE_INTEGER(i64) (LARGE_INTEGER){ .LowPart = ((int64_t)(i64) & 0xffffffff), .HighPart = (((int64_t)(i64) >> 32) & 0xffffffff) }
#define INT64_TO_LARGE_INTEGER(i64) (LARGE_INTEGER){ .QuadPart = (i64) }

static inline ARC_FORCEINLINE LARGE_INTEGER Int64ToLargeInteger(int64_t i64) {
    LARGE_INTEGER little;
    little.QuadPart = i64;
    return little;
}

typedef struct ARC_LE {
    LONG v;
} S32LE, * PS32LE;

typedef struct {
    LONG v;
} S32BE, * PS32BE;

typedef struct ARC_LE {
    uint64_t v;
} U64LE, * PU64LE;

typedef struct {
    uint64_t v;
} U64BE, * PU64BE;

typedef struct ARC_LE {
    ULONG v;
} U32LE, * PU32LE;

typedef struct {
    ULONG v;
} U32BE, * PU32BE;

typedef struct ARC_LE {
    USHORT v;
} U16LE, * PU16LE;

typedef struct {
    USHORT v;
} U16BE, * PU16BE;

typedef struct ARC_LE {
    SHORT v;
} S16LE, * PS16LE;

typedef struct {
    SHORT v;
} S16BE, * PS16BE;

static inline ARC_FORCEINLINE ULONG SwapEndianness32(ULONG value) {
    return __builtin_bswap32(value);
}
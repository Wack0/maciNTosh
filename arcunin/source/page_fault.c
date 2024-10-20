// Page fault handler, adapted from OpenBIOS:

/*
 *   Creation Date: <1999/11/07 19:02:11 samuel>
 *   Time-stamp: <2004/01/07 19:42:36 samuel>
 *
 *	<ofmem.c>
 *
 *	OF Memory manager
 *
 *   Copyright (C) 1999-2004 Samuel Rydh (samuel@ibrium.se)
 *   Copyright (C) 2004 Stefan Reinauer
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "arc.h"

#define SDR1_HTABORG_MASK 0xffff0000
#define SEGR_BASE		0x400		/* segment number range to use, must be synced with crt0.s  */

/* Hardware Page Table Entry */
// please note: hardware page tables are always read in big endian mode
// so the two 32 bit values are swapped
// therefore the following definition is correct!
// bit field is reversed due to endianness
typedef struct mPTE {
	unsigned long pp:2;	/* Page protection */
	unsigned long  :1;	/* Unused */
	unsigned long g:1;	/* Guarded */
	unsigned long m:1;	/* Memory coherence */
	unsigned long i:1;	/* Cache inhibited */
	unsigned long w:1;	/* Write-thru cache mode */
	unsigned long c:1;	/* Changed */
	unsigned long r:1;	/* Referenced */
	unsigned long    :3;	/* Unused */
	unsigned long rpn:20;	/* Real (physical) page number */


	unsigned long api:6;	/* Abbreviated page index */
	unsigned long h:1;	/* Hash algorithm indicator */
	unsigned long vsid:24;	/* Virtual segment identifier */
	unsigned long v:1;	/* Entry is valid */
} mPTE_t;


static inline unsigned long mfsdr1(void)
{
    unsigned long sdr1;
    asm volatile("mfsdr1 %0" : "=r" (sdr1));
    return sdr1;
}

static inline void mtsdr1(unsigned long sdr1)
{
    asm volatile("mtsdr1 %0" :: "r" (sdr1));
}

#if 1
static inline unsigned long mfsrin(unsigned long seg)
{
    unsigned long srin;
    asm volatile("mfsrin %0, %1" : "=r" (srin) : "r" (seg));
    return srin;
}
#else
static inline unsigned long mfsrin(unsigned long seg)
{
    unsigned long srin;
    seg >>= 24;
    #define MFSRIN_CASE(x) case x: asm volatile("mfsr %0, " #x : "=r" (srin)); break
    switch (seg) {
	MFSRIN_CASE(0x0);
	MFSRIN_CASE(0x1);
	MFSRIN_CASE(0x2);
	MFSRIN_CASE(0x3);
	MFSRIN_CASE(0x4);
	MFSRIN_CASE(0x5);
	MFSRIN_CASE(0x6);
	MFSRIN_CASE(0x7);
	MFSRIN_CASE(0x8);
	MFSRIN_CASE(0x9);
	MFSRIN_CASE(0xa);
	MFSRIN_CASE(0xb);
	MFSRIN_CASE(0xc);
	MFSRIN_CASE(0xd);
	MFSRIN_CASE(0xe);
	MFSRIN_CASE(0xf);
    }
    #undef MFSRIN_CASE(x)
    return srin;
}
#endif

static inline unsigned long
get_hash_base(void)
{
    return (mfsdr1() & SDR1_HTABORG_MASK);
}

static ULONG ea_to_phys(ULONG vaddr, PULONG mode) {
	// If here, using an address not mapped by a BAT.
	// 1:1 phys:virt
	*mode = 0x6A; // WIm GxPp, I/O
	return vaddr;
}

/* Converts a global variable (from .data or .bss) into a pointer that
   can be accessed from real mode */
static void *
global_ptr_real(void *p)
{
    return (void*)((ULONG)p & ~0x80000000);
}

/* Return the next slot to evict, in the range of [0..7] */
static int
next_evicted_slot(void)
{
    static int next_grab_slot;
    int *next_grab_slot_va;
    int r;

    next_grab_slot_va = global_ptr_real(&next_grab_slot);
    r = *next_grab_slot_va;
    *next_grab_slot_va = (r + 1) % 8;

    return r;
}

#if 0
static void
hash_page(unsigned long ea, ULONG phys, ULONG mode)
{
    unsigned long *upte, cmp, hash1;
    int i, vsid, found;
    mPTE_t *pp;

    unsigned long srin = mfsrin(ea);
    unsigned long page_index = (ea >> 12) & 0xffff;
    unsigned long sdr1_val = mfsdr1();

    vsid = srin & 0x00FFFFFF;
    cmp = ARC_BIT(31) | (vsid << 7) | (page_index >> 10);

    hash1 = (srin & 0x7FFFF);
    hash1 ^= page_index;

    unsigned long pteg_addr = sdr1_val & 0xFE000000;
    pteg_addr |= (sdr1_val & 0x01FF0000) | (((sdr1_val & 0x1FF) << 16) & ((hash1 & 0x7FC00) << 6));
    pteg_addr |= (hash1 & 0x3FF) << 6;

    pp = (mPTE_t*)(pteg_addr);
    upte = (unsigned long*)pp;

    /* replace old translation */
    for (found = 0, i = 0; !found && i < 8; i++)
        if (cmp == upte[i*2 + 1])
            found = 1;

    /* otherwise use a free slot */
    if (!found) {
        for (i = 0; !found && i < 8; i++)
            if (!pp[i].v)
                found = 1;
    }

    /* out of slots, just evict one */
    if (!found)
        i = next_evicted_slot() + 1;
    i--;
    upte[i * 2 + 1] = cmp;
    upte[i * 2] = (phys & ~0xfff) | mode;

    asm volatile("tlbie %0" :: "r"(ea));
}
#else
static void
hash_page(unsigned long ea, ULONG phys, ULONG mode)
{
    ULONG i;
    ULONG sdr1 = mfsdr1();
    ULONG sr = mfsrin(ea);
    ULONG vsid = sr & 0x0fffffff;
    ULONG physbase = sdr1 & ~0xffff;
    ULONG hashmask = ((sdr1 & 0x1ff) << 10) | 0x3ff;
    ULONG valo = (vsid << 28) | (ea & 0xfffffff);
    ULONG hash = (vsid & 0x7ffff) ^ ((valo >> 12) & 0xffff);
    ULONG ptegaddr = ((hashmask & hash) * 64) + physbase;

    ULONG cmp = ARC_BIT(31) | (vsid << 7) | ((ea >> 22) & 0x3f);
    mPTE_t* pp = (mPTE_t*)ptegaddr;
    PULONG upte = (PULONG)ptegaddr;
    UCHAR found = 0;

    /* replace old translation */
    for (i = 0; i < 8; i++) {
        if (cmp != upte[i*2 + 1]) continue;
        found = 1;
        break;
    }

    /* otherwise use a free slot */
    if (!found) {
        for (i = 0; i < 8; i++) {
            if (pp[i].v) continue;
            found = 1;
            break;
        }
    }

    /* out of slots, just evict one */
    if (!found)
        i = next_evicted_slot();
    upte[i * 2 + 1] = cmp;
    upte[i * 2] = (phys & ~0xfff) | mode;

    asm volatile("tlbie %0" :: "r"(ea));
}
#endif

#define	DBAT3U		"542"
#define	DBAT3L		"543"

static void
map_bat(ULONG physaddr) {
	physaddr &= 0xF0000000;
	ULONG batL = (physaddr | 0x002A);
	ULONG batU = (physaddr | 0x1FFF);
	ULONG zero = 0;
	// commit registers before doing anything
	asm volatile("" :: "r" (batL), "r" (batU), "r" (zero));
	asm volatile("isync");
	asm volatile("mtspr " DBAT3U ", %0" :: "r" (zero));
	asm volatile("mtspr " DBAT3L ", %0" :: "r" (batL));
	asm volatile("mtspr " DBAT3U ", %0" :: "r" (batU));
	asm volatile("isync");
}

void
dsi_exception(void)
{
    unsigned long dar, dsisr;
    ULONG mode;
    ULONG phys;

    asm volatile("mfdar %0" : "=r" (dar) : );
    asm volatile("mfdsisr %0" : "=r" (dsisr) : );

#if 0
    phys = ea_to_phys(dar, &mode);
    hash_page(dar, phys, mode);
#endif
    // map a bat for virt:phys 1:1
    if (dar < 0x90000000) while (1); // infinite loop for any vaddr outside of the valid range
    map_bat(dar);
}

void
isi_exception(void)
{
    unsigned long nip, srr1;
    ULONG mode;
    ULONG phys;

    asm volatile("mfsrr0 %0" : "=r" (nip) : );
    asm volatile("mfsrr1 %0" : "=r" (srr1) : );

#if 0
    phys = ea_to_phys(nip, &mode);
    hash_page(nip, phys, mode);
#endif
}


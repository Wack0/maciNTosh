// Comes from NT tree, also used in CE tree;
// slightly modified for our purposes.
#pragma once
#define EMU_FORM 1

#ifndef _LANGUAGE_ASSEMBLY
#include "types.h"

// PowerPC instruction format
typedef union ARC_LE _PPC_INSTRUCTION {
	ULONG Long;
	UCHAR Byte[4];

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit30 : 1;
		ULONG bit6_29 : 24;
		ULONG bit0_5 : 6;
	} i_f;

#define Primary_Op  i_f.bit0_5

#define Iform_LI  i_f.bit6_29
#define Iform_AA  i_f.bit30
#define Iform_LK  i_f.bit31

	struct ARC_LE {
		ULONG bit6_31 : 26;
		ULONG bit0_5 : 6;
	} emu_f;

#if EMU_FORM
#define Emu_Offset emu_f.bit6_31
#else
#define Emu_Offset Iform_LI
#endif

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit30 : 1;
		LONG bit16_29 : 14;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} b_f;

#define Bform_BO  b_f.bit6_10
#define Bform_BI  b_f.bit11_15
#define Bform_BD  b_f.bit16_29
#define Bform_AA  b_f.bit30
#define Bform_LK  b_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit30 : 1;
		ULONG bit6_29 : 24;
		ULONG bit0_5 : 6;
	} sc_f;

#define SCform_XO  sc_f.bit30

	struct ARC_LE {
		LONG bit16_31 : 16;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} d_f1;
	struct ARC_LE {
		ULONG bit16_31 : 16;
		ULONG bit11_15 : 5;
		ULONG bit10 : 1;
		ULONG bit9 : 1;
		ULONG bit6_8 : 3;
		ULONG bit0_5 : 6;
	} d_f2;

#define Dform_RT   d_f1.bit6_10
#define Dform_RS   d_f1.bit6_10
#define Dform_TO   d_f1.bit6_10
#define Dform_FRT  d_f1.bit6_10
#define Dform_FRS  d_f1.bit6_10
#define Dform_BF   d_f2.bit6_8
#define Dform_L    d_f2.bit10
#define Dform_RA   d_f1.bit11_15
#define Dform_D    d_f1.bit16_31
#define Dform_SI   d_f1.bit16_31
#define Dform_UI   d_f2.bit16_31

	struct ARC_LE {
		ULONG bit30_31 : 2;
		LONG bit16_29 : 14;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} ds_f;

#define DSform_RT  ds_f.bit6_10
#define DSform_RS  ds_f.bit6_10
#define DSform_RA  ds_f.bit11_15
#define DSform_DS  ds_f.bit16_29
#define DSform_XO  ds_f.bit30_31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit21_30 : 10;
		ULONG bit16_20 : 5;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} x_f1;
	struct ARC_LE {
		ULONG bit20_31 : 12;
		ULONG bit16_19 : 4;
		ULONG bit14_15 : 2;
		ULONG bit11_13 : 3;
		ULONG bit10 : 1;
		ULONG bit9 : 1;
		ULONG bit6_8 : 3;
		ULONG bit0_5 : 6;
	} x_f2;
	struct ARC_LE {
		ULONG bit16_31 : 16;
		ULONG bit12_15 : 4;
		ULONG bit0_11 : 12;
	} x_f3;

#define Xform_RT   x_f1.bit6_10
#define Xform_RS   x_f1.bit6_10
#define Xform_TO   x_f1.bit6_10
#define Xform_FRT  x_f1.bit6_10
#define Xform_FRS  x_f1.bit6_10
#define Xform_BT   x_f1.bit6_10
#define Xform_BF   x_f2.bit6_8
#define Xform_L    x_f2.bit10
#define Xform_RA   x_f1.bit11_15
#define Xform_FRA  x_f1.bit11_15
#define Xform_BFA  x_f2.bit11_13
#define Xform_SR   x_f3.bit12_15
#define Xform_RB   x_f1.bit16_20
#define Xform_NB   x_f1.bit16_20
#define Xform_SH   x_f1.bit16_20
#define Xform_FRB  x_f1.bit16_20
#define Xform_U    x_f2.bit16_19
#define Xform_XO   x_f1.bit21_30
#define Xform_RC   x_f1.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit21_30 : 10;
		ULONG bit16_20 : 5;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} xl_f1;
	struct ARC_LE {
		ULONG bit14_31 : 18;
		ULONG bit11_13 : 3;
		ULONG bit9_10 : 2;
		ULONG bit6_8 : 3;
		ULONG bit0_5 : 6;
	} xl_f2;

#define XLform_LK   xl_f1.bit31
#define XLform_XO   xl_f1.bit21_30
#define XLform_BB   xl_f1.bit16_20
#define XLform_BA   xl_f1.bit11_15
#define XLform_BI   xl_f1.bit11_15
#define XLform_BFA  xl_f2.bit11_13
#define XLform_BT   xl_f1.bit6_10
#define XLform_BO   xl_f1.bit6_10
#define XLform_BF   xl_f2.bit6_8

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit21_30 : 10;
		ULONG bit11_20 : 10;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} xfx_f1;
	struct ARC_LE {
		ULONG bit20_31 : 12;
		ULONG bit12_19 : 8;
		ULONG bit6_11 : 6;
		ULONG bit0_5 : 6;
	} xfx_f2;

#define XFXform_RT  xfx_f1.bit6_10
#define XFXform_RS  xfx_f1.bit6_10
#define XFXform_spr xfx_f1.bit11_20
#define XFXform_tbr xfx_f1.bit11_20
#define XFXform_spr xfx_f1.bit11_20
#define XFXform_FXM xfx_f2.bit12_19
#define XFXform_XO  xfx_f1.bit21_30

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit21_30 : 10;
		ULONG bit16_20 : 5;
		ULONG bit15 : 1;
		ULONG bit7_14 : 8;
		ULONG bit6 : 1;
		ULONG bit0_5 : 6;
	} xfl_f;

#define XFLform_FLM xfl_f.bit7_14
#define XFLform_FRB xfl_f.bit16_20
#define XFLform_XO  xfl_f.bit21_30
#define XFLform_RC  xfl_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit30 : 1;
		ULONG bit21_29 : 9;
		ULONG bit16_20 : 5;
		ULONG bit10_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} xs_f;

#define XSform_RS  xs_f.bit6_10
#define XSform_RA  xs_f.bit11_15
#define XSform_sh1 xs_f.bit16_20
#define XSform_XO  xs_f.bit21_29
#define XSform_sh2 xs_f.bit30
#define XSform_RC  xs_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit22_30 : 9;
		ULONG bit21 : 1;
		ULONG bit16_20 : 5;
		ULONG bit10_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} xo_f;

#define XOform_RT  xo_f.bit6_10
#define XOform_RA  xo_f.bit11_15
#define XOform_RB  xo_f.bit16_20
#define XOform_OE  xo_f.bit21
#define XOform_XO  xo_f.bit22_30
#define XOform_RC  xo_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit26_30 : 5;
		ULONG bit21_25 : 5;
		ULONG bit16_20 : 5;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} a_f;

#define Aform_FRT  a_f.bit6_10
#define Aform_FRA  a_f.bit11_15
#define Aform_FRB  a_f.bit16_20
#define Aform_FRC  a_f.bit21_25
#define Aform_XO   a_f.bit26_30
#define Aform_RC   a_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit26_30 : 5;
		ULONG bit21_25 : 5;
		ULONG bit16_20 : 5;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} m_f;

#define Mform_RS  m_f.bit6_10
#define Mform_RA  m_f.bit11_15
#define Mform_RB  m_f.bit16_20
#define Mform_SH  m_f.bit16_20
#define Mform_MB  m_f.bit21_25
#define Mform_ME  m_f.bit26_30
#define Mform_RC  m_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit30 : 1;
		ULONG bit27_29 : 3;
		ULONG bit21_26 : 6;
		ULONG bit16_20 : 5;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} md_f;

#define MDform_RS   md_f.bit6_10
#define MDform_RA   md_f.bit11_15
#define MDform_sh1  md_f.bit16_20
#define MDform_mb   md_f.bit21_26
#define MDform_me   md_f.bit21_26
#define MDform_XO   md_f.bit27_29
#define MDform_sh2  md_f.bit30
#define MDform_RC   md_f.bit31

	struct ARC_LE {
		ULONG bit31 : 1;
		ULONG bit27_30 : 4;
		ULONG bit21_26 : 6;
		ULONG bit16_20 : 5;
		ULONG bit11_15 : 5;
		ULONG bit6_10 : 5;
		ULONG bit0_5 : 6;
	} mds_f;

#define MDSform_RS  mds_f.bit6_10
#define MDSform_RA  mds_f.bit11_15
#define MDSform_RB  mds_f.bit16_20
#define MDSform_mb  mds_f.bit21_26
#define MDSform_me  mds_f.bit21_26
#define MDSform_XO  mds_f.bit27_30
#define MDSform_RC  mds_f.bit31

} PPC_INSTRUCTION, * PPPC_INSTRUCTION;

typedef union _PPC_INSTRUCTION_BIG {
	ULONG Long;
	UCHAR Byte[4];

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_29 : 24;
		ULONG bit30 : 1;
		ULONG bit31 : 1;
	} i_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_31 : 26;
	} emu_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		LONG bit16_29 : 14;
		ULONG bit30 : 1;
		ULONG bit31 : 1;
	} b_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_29 : 24;
		ULONG bit30 : 1;
		ULONG bit31 : 1;
	} sc_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		LONG bit16_31 : 16;
	} d_f1;
	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_8 : 3;
		ULONG bit9 : 1;
		ULONG bit10 : 1;
		ULONG bit11_15 : 5;
		ULONG bit16_31 : 16;
	} d_f2;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		LONG bit16_29 : 14;
		ULONG bit30_31 : 2;
	} ds_f;
	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_30 : 10;
		ULONG bit31 : 1;
	} x_f1;
	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_8 : 3;
		ULONG bit9 : 1;
		ULONG bit10 : 1;
		ULONG bit11_13 : 3;
		ULONG bit14_15 : 2;
		ULONG bit16_19 : 4;
		ULONG bit20_31 : 12;
	} x_f2;
	struct {
		ULONG bit0_11 : 12;
		ULONG bit12_15 : 4;
		ULONG bit16_31 : 16;
	} x_f3;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_30 : 10;
		ULONG bit31 : 1;
	} xl_f1;
	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_8 : 3;
		ULONG bit9_10 : 2;
		ULONG bit11_13 : 3;
		ULONG bit14_31 : 18;
	} xl_f2;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_20 : 10;
		ULONG bit21_30 : 10;
		ULONG bit31 : 1;
	} xfx_f1;
	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_11 : 6;
		ULONG bit12_19 : 8;
		ULONG bit20_31 : 12;
	} xfx_f2;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6 : 1;
		ULONG bit7_14 : 8;
		ULONG bit15 : 1;
		ULONG bit16_20 : 5;
		ULONG bit21_30 : 10;
		ULONG bit31 : 1;
	} xfl_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit10_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_29 : 9;
		ULONG bit30 : 1;
		ULONG bit31 : 1;
	} xs_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit10_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21 : 1;
		ULONG bit22_30 : 9;
		ULONG bit31 : 1;
	} xo_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_25 : 5;
		ULONG bit26_30 : 5;
		ULONG bit31 : 1;
	} a_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_25 : 5;
		ULONG bit26_30 : 5;
		ULONG bit31 : 1;
	} m_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_26 : 6;
		ULONG bit27_29 : 3;
		ULONG bit30 : 1;
		ULONG bit31 : 1;
	} md_f;

	struct {
		ULONG bit0_5 : 6;
		ULONG bit6_10 : 5;
		ULONG bit11_15 : 5;
		ULONG bit16_20 : 5;
		ULONG bit21_26 : 6;
		ULONG bit27_30 : 4;
		ULONG bit31 : 1;
	} mds_f;

} PPC_INSTRUCTION_BIG, * PPPC_INSTRUCTION_BIG;
#endif

// PowerPC opcodes

// Use known invalid instructions.
#define EMU_OP       56 // paired single instruction on Gekko derivatives, user manuals for other G3/G4/G5 says this is invalid everywhere else too
#define EMU_SWAP32_OP 1 // guaranteed to be invalid everywhere according to all G3/G4/G5 user manuals.
#if 0 // no longer used
#define EMU_COPY_OP  1 // again, guaranteed to be invalid everywhere according to all G3/G4/G5 user manuals.
#define EMU_TRAP_OP  EMU_COPY_OP
#endif

#if EMU_FORM
#define EMU_LEN     (ARC_BIT(26) - 1)
#else
#define EMU_LEN     (ARC_BIT(24) - 1)
#endif
#define TDI_OP       2
#define TWI_OP       3
#define MULLI_OP     7
#define SUBFIC_OP    8
#define CMPLI_OP    10
#define CMPI_OP     11
#define ADDIC_OP    12
#define ADDIC_RC_OP 13
#define ADDI_OP     14
#define ADDIS_OP    15
#define BC_OP       16
#define SC_OP       17
#define B_OP        18

#define X19_OP      19    // Extended ops for primary code 19:
#define   MCRF_OP       0
#define   BCLR_OP      16
#define   CRNOR_OP     33
#define   RFI_OP       50
#define   CRANDC_OP   129
#define   ISYNC_OP    150
#define   CRXOR_OP    193
#define	  CRNAND_OP   225
#define	  CRAND_OP    257
#define	  CREQV_OP    289
#define	  CRORC_OP    417
#define	  CROR_OP     449
#define	  BCCTR_OP    528

#define RLWIMI_OP   20
#define RLWINM_OP   21
#define RLWNM_OP    23
#define ORI_OP      24
#define	ORIS_OP	    25
#define XORI_OP	    26
#define	XORIS_OP    27
#define	ANDI_RC_OP  28
#define ANDIS_RC_OP 29

#define X30_OP	    30    // Extended ops for primary code 30:
#define   RLDICL_OP     0
#define   RLDICR_OP	1
#define	  RLDIC_OP	2
#define	  RLDIMI_OP	3
#define   RLDCL_OP	8
#define   RLDCR_OP	9

#define X31_OP      31    // Extended ops for primary code 31:
#define   CMP_OP	0
#define	  TW_OP		4
#define	  SUBFC_OP	8
#define	  MULHDU_OP	9
#define	  ADDC_OP      10
#define	  MULHWU_OP    11
#define	  MFCR_OP      19
#define	  LWARX_OP     20
#define	  LDX_OP       21
#define	  LWZX_OP      23
#define	  SLW_OP       24
#define	  CNTLZW_OP    26
#define	  SLD_OP       27
#define	  AND_OP       28
#define	  CMPL_OP      32
#define	  SUBF_OP      40
#define	  LDUX_OP      53
#define	  DCBST_OP     54
#define	  LWZUX_OP     55
#define	  CNTLZD_OP    58
#define	  ANDC_OP      60
#define	  TD_OP	       68
#define	  MULHD_OP     73
#define	  MULHW_OP     75
#define	  MFMSR_OP     83
#define	  LDARX_OP     84
#define	  DCBF_OP      86
#define	  LBZX_OP      87
#define	  NEG_OP      104
#define	  LBZUX_OP    119
#define	  NOR_OP      124
#define	  SUBFE_OP    136
#define	  ADDE_OP     138
#define	  MTCRF_OP    144
#define	  MTMSR_OP    146
#define	  STDX_OP     149
#define	  STWCX_RC_OP 150	 
#define	  STWX_OP     151
#define	  STDUX_OP    181
#define	  STWUX_OP    183
#define	  SUBFZE_OP   200
#define	  ADDZE_OP    202
#define	  MTSR_OP     210
#define	  STDCX_RC_OP 214
#define	  STBX_OP     215
#define	  SUBFME_OP   232
#define	  MULLD_OP    233
#define	  ADDME_OP    234
#define	  MULLW_OP    235
#define	  MTSRIN_OP   242
#define	  DCBTST_OP   246
#define	  STBUX_OP    247
#define	  ADD_OP      266
#define	  DCBT_OP     278
#define	  LHZX_OP     279
#define	  EQV_OP      284
#define	  TLBIE_OP    306
#define	  ECIWX_OP    310
#define	  LHZUX_OP    311
#define	  XOR_OP      316
#define	  MFSPR_OP    339
#define	  LWAX_OP     341
#define	  LHAX_OP     343
#define	  TLBIA_OP    370
#define	  MFTB_OP     371
#define	  LWAUX_OP    373
#define	  LHAUX_OP    375
#define	  STHX_OP     407
#define	  ORC_OP      412
#define	  SRADI_OP    413
#define	  SLBIE_OP    434
#define	  ECOWX_OP    438
#define	  STHUX_OP    439
#define	  OR_OP	      444
#define	  DIVDU_OP    457
#define	  DIVWU_OP    459
#define	  MTSPR_OP    467
#define	  DCBI_OP     470
#define	  NAND_OP     476
#define	  DIVD_OP     489
#define	  DIVW_OP     491
#define	  SLBIA_OP    498
#define	  MCRXR_OP    512
#define	  LSWX_OP     533
#define	  LWBRX_OP    534
#define	  LFSX_OP     535
#define	  SRW_OP      536
#define	  SRD_OP      539
#define	  TLBSYNC_OP  566
#define	  LFSUX_OP    567
#define	  MFSR_OP     595
#define	  LSWI_OP     597
#define	  SYNC_OP     598
#define	  LFDX_OP     599
#define	  LFDUX_OP    631
#define	  MFSRIN_OP   659
#define	  STSWX_OP    661
#define	  STWBRX_OP   662
#define	  STFSX_OP    663
#define	  STFSUX_OP   695
#define	  STSWI_OP    725
#define	  STFDX_OP    727
#define	  STFDUX_OP   759
#define	  LHBRX_OP    790
#define	  SRAW_OP     792
#define	  SRAD_OP     794
#define	  SRAWI_OP    824
#define	  EIEIO_OP    854
#define	  STHBRX_OP   918
#define	  EXTSH_OP    922
#define	  EXTSB_OP    954
#define	  ICBI_OP     982
#define	  STFIWX_OP   983
#define	  EXTSW_OP    986
#define	  DCBZ_OP    1014

#define LWZ_OP      32
#define LWZU_OP	    33
#define	LBZ_OP	    34
#define	LBZU_OP	    35
#define	STW_OP	    36
#define	STWU_OP	    37
#define	STB_OP	    38
#define	STBU_OP	    39
#define	LHZ_OP	    40
#define	LHZU_OP	    41
#define	LHA_OP	    42
#define	LHAU_OP	    43
#define	STH_OP	    44
#define	STHU_OP	    45
#define	LFS_OP	    48
#define	LFSU_OP	    49
#define	LFD_OP	    50
#define	LFDU_OP	    51
#define STFS_OP	    52
#define	STFSU_OP    53
#define	STFD_OP	    54
#define	STFDU_OP    55

#define X58_OP      58    // Extended ops for primary code 58:
#define   LD_OP	        0
#define	  LDU_OP	1
#define	  LWA_OP	2

#define X59_OP      59    // Extended ops for primary code 59:
#define	  FDIVS_OP     18
#define	  FSUBS_OP     20
#define	  FADDS_OP     21
#define	  FSQRTS_OP    22
#define	  FRES_OP      24
#define	  FMULS_OP     25
#define	  FMSUBS_OP    28
#define   FMADDS_OP    29
#define	  FNMSUBS_OP   30
#define	  FNMADDS_OP   31

#define X62_OP      62    // Extended ops for primary code 62:
#define   STD_OP        0
#define   STDU_OP       1

#define X63_OP	    63	  // Extended ops for primary code 63:
#define   FCMPU_OP      0
#define	  FRSP_OP      12
#define	  FCTIW_OP     14
#define	  FCTIWZ_OP    15
#define	  FDIV_OP      18
#define	  FSUB_OP      20
#define	  FADD_OP      21
#define	  FSQRT_OP     22
#define	  FSEL_OP      23
#define	  FMUL_OP      25
#define	  FSQRTE_OP    26
#define	  FMSUB_OP     28
#define   FMADD_OP     29
#define	  FNMSUB_OP    30
#define	  FNMADD_OP    31
#define	  FCMPO_OP     32
#define	  MTFSB1_OP    38
#define	  FNEG_OP      40
#define	  MCRFS_OP     64
#define	  MTFSB0_OP    70
#define	  FMR_OP       72
#define	  MTFSFI_OP   134
#define	  FNABS_OP    136
#define	  FABS_OP     264
#define	  MFFS_OP     583
#define	  MTFSF_OP    711
#define	  FCTID_OP    814
#define	  FCTIDZ_OP   815
#define	  FCFID_OP    846

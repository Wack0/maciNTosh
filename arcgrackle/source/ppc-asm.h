#ifdef __STDC__
#define XGLUE(a,b) a##b
#else
#define XGLUE(a,b) a/**/b
#endif

#define GLUE(a,b) XGLUE(a,b)

#define FUNC_NAME(name) GLUE(__USER_LABEL_PREFIX__,name)
#if defined __PIC__ || defined __pic__
#define JUMP_TARGET(name) FUNC_NAME(name@plt)
#else
#define JUMP_TARGET(name) FUNC_NAME(name)
#endif
#define FUNC_START(name) \
	.type FUNC_NAME(name),@function; \
	.globl FUNC_NAME(name); \
FUNC_NAME(name):

#define HIDDEN_FUNC(name) \
  FUNC_START(name) \
  .hidden FUNC_NAME(name);

#define FUNC_END(name) \
GLUE(.L,name): \
	.size FUNC_NAME(name),GLUE(.L,name)-FUNC_NAME(name)

# define CFI_STARTPROC			.cfi_startproc
# define CFI_ENDPROC			.cfi_endproc
# define CFI_OFFSET(reg, off)		.cfi_offset reg, off
# define CFI_DEF_CFA_REGISTER(reg)	.cfi_def_cfa_register reg
# define CFI_RESTORE(reg)		.cfi_restore reg
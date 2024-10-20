
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

# You can override the CFLAGS and C compiler externally,
# e.g. make PLATFORM=cortex-m3
CFLAGS += -g -Os -Wall -Werror -I include -DBASELIBC_INTERNAL -DWITH_MALLOC -DWITH_STDIO -DPRINTF_LONG_SUPPORT

PLATFORM ?= ppc

ifeq ($(PLATFORM),cortex-m3)
  CFLAGS  += -fno-common -Os
  CC      = arm-none-eabi-gcc
  AR      = arm-none-eabi-ar
  CFLAGS += -mcpu=cortex-m3 -mthumb
endif
ifeq ($(PLATFORM),mipsel)
  CC	  = ../2019.02-05/bin/mips-img-elf-gcc
  AR      = ../2019.02-05/bin/mips-img-elf-gcc-ar
  CFLAGS += -G0 -mno-gpopt -march=r4000 -Os -mel -mgp32 -mfp32 -mabi=32
endif
ifeq ($(PLATFORM),ppc)
  CC	  = $(DEVKITPPC)/bin/powerpc-eabi-gcc
  AR      = $(DEVKITPPC)/bin/powerpc-eabi-gcc-ar
  CFLAGS += -mcpu=750 -m32 -mhard-float -mno-eabi -mno-sdata -mlittle
endif

# With this, the makefile should work on Windows also.
ifdef windir
  RM = del
endif

# Just include all the source files in the build.
CSRC = $(wildcard src/*.c)
OBJS = $(CSRC:.c=.o)

# And the files for the test suite
TESTS_CSRC = $(wildcard tests/*_tests.c)
TESTS_OBJS = $(TESTS_CSRC:.c=)

# Some of the files uses "templates", i.e. common pieces
# of code included from multiple files.
CFLAGS += -Isrc/templates

all: libcbase.a

clean:
	$(RM) $(OBJS) $(TESTS_OBJS) libcbase.a

libcbase.a: $(OBJS)
	$(RM) $@
	$(AR) ru $@ $^

run_tests: $(TESTS_OBJS)
	$(foreach f,$^,$f)

tests/%: tests/%.c tests/tests_glue.c libcbase.a
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(strip $(DEVKITPPC)),)
$(error "Set DEVKITPPC in your environment.")
endif

PREFIX = $(DEVKITPPC)/bin/powerpc-eabi-

CFLAGS = -mcpu=750 -m32 -mhard-float -mno-eabi -mno-sdata
CFLAGS += -ffreestanding -ffunction-sections -fdata-sections
CFLAGS += -I../baselibc/include
CFLAGS += -Wall -Wextra -Os
ASFLAGS =
LDFLAGS = -mcpu=750 -m32 -n -nostartfiles -nodefaultlibs -Wl,-gc-sections

CFLAGS += -mno-eabi -mno-sdata -Os -ffreestanding
CFLAGS += -Wall -Wextra
DEFINES =  -DWITH_STDIO -DBASELIBC_INTERNAL
LDFLAGS += -nostartfiles -nodefaultlibs -L../baselibc

LDSCRIPT = of.ld
TARGET = stage1.elf

LDSCRIPT_XCOFF = of-xcoff.ld
LDFLAGS_XCOFF := $(LDFLAGS)
TARGET_XCOFF = boot

FILES = $(wildcard source/*.S) $(wildcard source/*.c)
OBJSx = $(FILES:source/%.S=build/%.o)
OBJS = $(OBJSx:source/%.c=build/%.o)
LIBS = -lcbase -lgcc

AR = $(PREFIX)ar
AS = $(PREFIX)as
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
RANLIB = $(PREFIX)ranlib
STRIP = $(PREFIX)strip

HOSTCC ?= gcc

ifeq ($(NOMAPFILE),)
LDFLAGS_XCOFF += -Wl,-Map,$(TARGET_XCOFF).map
LDFLAGS += -Wl,-Map,$(TARGET).map
endif

ifneq ($(LDSCRIPT),)
LDFLAGS_XCOFF += -Wl,-T$(LDSCRIPT_XCOFF)
LDFLAGS += -Wl,-T$(LDSCRIPT)
endif

DEPDIR = .deps

all: $(TARGET)
	@[ -d $(DIR_BUILD) ] || mkdir $(DIR_BUILD)

oldworld: $(TARGET_XCOFF).xcf
	@[ -d $(DIR_BUILD) ] || mkdir $(DIR_BUILD)

$(TARGET): $(OBJS)
	@echo "  LINK      $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	@cp $(TARGET) $(TARGET:%.elf=%_unstripped.elf)
	@$(STRIP) $(TARGET)

$(TARGET_XCOFF).elf: $(OBJS)
	@echo "  LINK      $@"
	@$(CC) $(LDFLAGS_XCOFF) $(OBJS) $(LIBS) -o $@
	@cp $(TARGET_XCOFF).elf $(TARGET_XCOFF)_unstripped.elf
	@$(STRIP) $(TARGET_XCOFF).elf

$(TARGET_XCOFF).xcf: $(TARGET_XCOFF).elf hack-coff
	@echo "  OBJCOPY   $@"
	@rm -f $@
	@$(OBJCOPY) -O aixcoff-rs6000 -j .text -j .data -j .bss $< $@
	@./hack-coff $@

hack-coff: hack-coff.c
	@echo "  HOSTCC    $@"
	@$(HOSTCC) -o $@ $<

ifneq ($(LDSCRIPT),)
$(TARGET): $(LDSCRIPT)
endif

build/%.o: source/%.c
	@echo "  COMPILE   $<"
	@mkdir -p $(DEPDIR)
	@mkdir -p build
	@$(CC) $(CFLAGS) $(DEFINES) -Wp,-MMD,$(DEPDIR)/$(*F).d,-MQ,"$@",-MP -c $< -o $@

build/%.o: source/%.s
	@echo "  ASSEMBLE  $<"
	@mkdir -p build
	@$(CC) $(CFLAGS) $(DEFINES) $(ASFLAGS) -c $< -o $@

build/%.o: source/%.S
	@echo "  ASSEMBLE  $<"
	@mkdir -p build
	@$(CC) $(CFLAGS) $(DEFINES) $(ASFLAGS) -c $< -o $@

clean:
	rm -rf $(DEPDIR)
	rm -f $(TARGET) $(TARGET:%.elf=%_unstripped.elf) $(TARGET).map $(OBJS)

-include $(DEPDIR)/*

.PHONY: clean

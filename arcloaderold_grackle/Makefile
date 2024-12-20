ifeq ($(strip $(RETRO68)),)
$(error "Set RETRO68 in your environment.")
endif

TOOLPATH = $(RETRO68)/bin
PREFIX = $(TOOLPATH)/powerpc-apple-macos-

CFLAGS = -mcpu=603 -Wall -Wextra -Os
ASFLAGS =
LDFLAGS =
LIBS = -lDisplayLib -lNameRegistryLib

REZFLAGS = -I$(RETRO68)/RIncludes -tzsys -cMACS

TARGET = stage1

FILES = $(wildcard source/*.S) $(wildcard source/*.c)
OBJSx = $(FILES:source/%.S=build/%.o)
OBJS = $(OBJSx:source/%.c=build/%.o)

AR = $(PREFIX)ar
AS = $(PREFIX)as
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
RANLIB = $(PREFIX)ranlib
STRIP = $(PREFIX)strip
MAKEPEF = $(TOOLPATH)/MakePEF
REZ = $(TOOLPATH)/Rez

ifeq ($(NOMAPFILE),)
LDFLAGS += -Wl,-Map,$(TARGET).map
endif

ifneq ($(LDSCRIPT),)
LDFLAGS += -Wl,-T$(LDSCRIPT)
endif

DEPDIR = .deps

all: $(TARGET).img
	@[ -d $(DIR_BUILD) ] || mkdir $(DIR_BUILD)

addstage2: $(TARGET).img
	@$(TOOLPATH)/hmount $<
	@$(TOOLPATH)/hcopy -r ../arcgrackle/stage2.elf :stage2.elf
	@$(TOOLPATH)/humount $<

$(TARGET).img: $(TARGET).rsrc.bin apple/boot1.bin
	@echo "  IMG       $@"
	@dd if=/dev/zero of=$@ bs=256K count=1
	@$(TOOLPATH)/hformat -l 'Windows NT' $@
	@dd conv=notrunc if=apple/boot1.bin of=$@
	@$(TOOLPATH)/hmount $@
	@$(TOOLPATH)/hcopy -m $< :System
	@touch Finder
	@$(TOOLPATH)/hcopy -r Finder :Finder
	@rm Finder
	@$(TOOLPATH)/hattrib -t FNDR -c MACS :Finder
	@$(TOOLPATH)/hattrib -b :
	@$(TOOLPATH)/humount $@

$(TARGET).rsrc.bin: source/$(TARGET).r apple/boot1.bin $(TARGET).bin
	@echo "  REZ       $@"
	@$(REZ) $(REZFLAGS) source/$(TARGET).r -o System.bin
	@mv System.bin $@

$(TARGET).bin: $(TARGET).pef apple/boot2stub.bin
	@echo "  MAKEBIN   $@"
	@cat apple/boot2stub.bin $(TARGET).pef > $@

$(TARGET).pef: $(TARGET).xcoff
	@echo "  MAKEPEF   $@"
	@$(MAKEPEF) $(TARGET).xcoff -o $@

$(TARGET).xcoff: $(OBJS)
	@echo "  LINK      $@"
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

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
	rm -f $(TARGET).* $(OBJS)

-include $(DEPDIR)/*

.PHONY: clean

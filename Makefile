.SUFFIXES:

ifeq ($(OS), win32)
CROSS_COMPILE := i586-mingw32msvc-
SUFFIX := .exe
else
SUFFIX := 
endif

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
DEL := rm -f

EXTRA_DEFS := -D_FILE_OFFSET_BITS=64 -Iinclude
CFLAGS := -g -pipe -O2 -Wall \
	-Wsign-compare -Wcast-align \
	-Waggregate-return \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wmissing-declarations \
	-Wmissing-noreturn \
	-finline-functions \
	-Wmissing-format-attribute \
	-fwrapv \
	-Iinclude \
	$(EXTRA_DEFS) 

EMU_BIN := emu$(SUFFIX)
EMU_OBJ := emu.o

ASM_BIN := asm$(SUFFIX)
ASM_OBJ := asm.o

ALL_BIN := $(EMU_BIN) $(ASM_BIN)
ALL_OBJ := $(EMU_OBJ) $(ASM_OBJ)
ALL_DEP := $(patsubst %.o, .%.d, $(ALL_OBJ))
ALL_TARGETS := $(ALL_BIN)

TARGET: all

.PHONY: all clean

all: $(ALL_BIN)

ifeq ($(filter clean, $(MAKECMDGOALS)),clean)
CLEAN_DEP := clean
else
CLEAN_DEP :=
endif

%.o %.d: %.c $(CLEAN_DEP) $(ROOT_DEP) Makefile
	@echo " [C] $<"
	@$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, .%.d, $@) \
		-MT $(patsubst .%.d, %.o, $@) \
		-c -o $(patsubst .%.d, %.o, $@) $<

$(EMU_BIN): $(EMU_OBJ)
	@echo " [LINK] $@"
	@$(CC) $(CFLAGS) -o $@ $^

$(ASM_BIN): $(ASM_OBJ)
	@echo " [LINK] $@"
	@$(CC) $(CFLAGS) -o $@ $^

clean:
	$(DEL) $(ALL_TARGETS) $(ALL_OBJ) $(ALL_DEP)

ifneq ($(MAKECMDGOALS),clean)
-include $(ALL_DEP)
endif

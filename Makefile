#---------------------------------------------------------------------------------
# NoteRoom - Makefile für 3DSX & CIA
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO nicht gesetzt!")
endif

# Pfade
DEVKITARM := $(DEVKITPRO)/devkitARM
CTRULIB   := $(DEVKITPRO)/libctru
PORTLIBS  := $(DEVKITPRO)/portlibs/3ds
TOOLS     := $(DEVKITPRO)/tools/bin

TARGET     := NoteRoom
APP_TITLE  := NoteRoom
APP_AUTHOR := SprtnDio

# Tools
CC         := $(DEVKITARM)/bin/arm-none-eabi-gcc
_3DSX      := $(TOOLS)/3dsxtool
SMDH       := $(TOOLS)/smdhtool
MKROM      := $(TOOLS)/makerom
BANNERTOOL := $(TOOLS)/bannertool

# Flags
ARCH      := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
INCLUDE   := -Iinclude -I$(CTRULIB)/include -I$(PORTLIBS)/include
LIBDIRS   := -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS      := -lcitro2d -lcitro3d -lctru -lm

CFLAGS    := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__ -std=gnu11
LDFLAGS   := -specs=3dsx.specs -g $(ARCH) $(LIBDIRS) $(LIBS)

# --- REGELN ---
.PHONY: all clean cia

all: $(TARGET).3dsx $(TARGET).cia

# 3DSX Target
$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@echo "📦 Erstelle 3DSX..."
	@$(_3DSX) $(TARGET).elf $(TARGET).3dsx --smdh=$(TARGET).smdh
	@echo "✅ 3DSX fertig!"

# Banner
banner.bin: banner.png audio.wav
	@echo "🖼️  Erstelle animiertes Banner mit Sound..."
	@$(BANNERTOOL) makebanner -i banner.png -a audio.wav -o banner.bin

# CIA
cia: $(TARGET).cia

$(TARGET).cia: $(TARGET).elf $(TARGET).smdh banner.bin App.rsf prebuilt_homebrew_logo-padded.lz11
	@echo "📀 Erstelle CIA mit Makerom..."
	@$(MKROM) -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf App.rsf -icon $(TARGET).smdh -banner banner.bin -logo prebuilt_homebrew_logo-padded.lz11 -target t
	@echo "✅ CIA fertig: $(TARGET).cia"

# ELF Linken (jetzt mit secrets.o)
$(TARGET).elf: source/main.o source/secrets.o
	@echo "🔗 Verlinke ELF..."
	@$(CC) -o $@ $^ $(LDFLAGS)

# Kompilieren main.c
source/main.o: source/main.c
	@echo "🔧 Kompiliere main.c..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Kompilieren secrets.c (NEU)
source/secrets.o: source/secrets.c
	@echo "🔧 Kompiliere secrets.c..."
	@$(CC) $(CFLAGS) -c $< -o $@

# SMDH
$(TARGET).smdh: icon.png
	@echo "🎨 Erstelle Icon (SMDH)..."
	@$(SMDH) --create "$(APP_TITLE)" "NoteRoom - Chatroom Client" "$(APP_AUTHOR)" icon.png $@

clean:
	@echo "🗑️  Cleanup..."
	@rm -f source/*.o *.elf *.3dsx *.smdh *.cia banner.bin
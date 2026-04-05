#------------------------------------#
# NoteRoom - Makefile für 3DSX & CIA #
#------------------------------------#

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO nicht gesetzt!")
endif

DEVKITARM := $(DEVKITPRO)/devkitARM
CTRULIB   := $(DEVKITPRO)/libctru
PORTLIBS  := $(DEVKITPRO)/portlibs/3ds
TOOLS     := $(DEVKITPRO)/tools/bin

TARGET     := NoteRoom
APP_TITLE  := NoteRoom
APP_AUTHOR := SprtnDio

CC         := $(DEVKITARM)/bin/arm-none-eabi-gcc
_3DSX      := $(TOOLS)/3dsxtool
SMDH       := $(TOOLS)/smdhtool
MKROM      := $(TOOLS)/makerom
BANNERTOOL := $(TOOLS)/bannertool

ARCH      := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
INCLUDE   := -Iinclude -I$(CTRULIB)/include -I$(PORTLIBS)/include
LIBDIRS   := -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS      := -lcitro2d -lcitro3d -lctru -lm

# -Wno-unused-variable unterdrückt Warnungen über unbenutzte Variablen (z.B. kHeld)
CFLAGS    := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__ -std=gnu11 -Wno-unused-variable
LDFLAGS   := -specs=3dsx.specs -g $(ARCH) $(LIBDIRS) $(LIBS)

# Alle .c-Dateien im source/-Ordner (inkl. rooms.c, secrets.c usw.)
SOURCES := $(wildcard source/*.c)
OBJECTS := $(SOURCES:.c=.o)

# --- REGELN ---
.PHONY: all clean cia

all: $(TARGET).3dsx $(TARGET).cia

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@echo "📦 Erstelle 3DSX..."
	@$(_3DSX) $(TARGET).elf $(TARGET).3dsx --smdh=$(TARGET).smdh
	@echo "✅ 3DSX fertig!"

banner.bin: banner.png audio.wav
	@echo "🖼️  Erstelle animiertes Banner mit Sound..."
	@$(BANNERTOOL) makebanner -i banner.png -a audio.wav -o banner.bin

$(TARGET).cia: $(TARGET).elf $(TARGET).smdh banner.bin App.rsf
	@echo "📀 Erstelle CIA mit Makerom..."
	@$(MKROM) -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf App.rsf -icon $(TARGET).smdh -banner banner.bin -target t
	@echo "✅ CIA fertig: $(TARGET).cia"

$(TARGET).elf: $(OBJECTS)
	@echo "🔗 Verlinke ELF..."
	@$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	@echo "🔧 Kompiliere $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).smdh: icon.png
	@echo "🎨 Erstelle Icon (SMDH)..."
	@$(SMDH) --create "$(APP_TITLE)" "World-wide Chatroom Client" "$(APP_AUTHOR)" icon.png $@

clean:
	@echo "🗑️  Cleanup..."
	@rm -f $(OBJECTS) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia banner.bin
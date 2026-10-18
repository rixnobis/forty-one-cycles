TARGET = forty-one
TYPE = ps-exe

SRCS = \
nugget/psmplayer/psmplayer.c \
assets-psm.o \
assets-vab.o \
src/main.cpp \
src/s00_coldboot.cpp \
src/s01_starfield.cpp \
src/s02_copper.cpp \
src/s03_rotozoom.cpp \
src/s04_balls.cpp \
src/s05_plasma.cpp \
src/s06_tunnel.cpp \
src/s07_recap.cpp \
src/s08_credits.cpp \

ifeq ($(TEST),true)
CPPFLAGS = -Werror
endif
CPPFLAGS += -Isrc -Inugget $(EXTRA)
CXXFLAGS = -std=c++20

# Starting bar for fast iteration: make DEBUG_SKIP=65
ifdef DEBUG_SKIP
CPPFLAGS += -DDEBUG_SKIP=$(DEBUG_SKIP)
endif
# MIRROR_MASK overrides the note mirror mask (see src/main.cpp)
ifdef MIRROR_MASK
CPPFLAGS += -DMIRROR_MASK=$(MIRROR_MASK)
endif

include nugget/psyqo/psyqo.mk

# assets/forty-one.{psm,vab} come from tools/build-music.sh
assets-psm.o: assets/forty-one.psm
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@
	$(PREFIX)-objcopy --redefine-sym _binary_assets_forty_one_psm_start=_binary_music_psm_start --redefine-sym _binary_assets_forty_one_psm_end=_binary_music_psm_end $@

assets-vab.o: assets/forty-one.vab
	$(PREFIX)-objcopy -I binary --set-section-alignment .data=4 --rename-section .data=.rodata,alloc,load,readonly,data,contents -O $(FORMAT) -B mips $< $@
	$(PREFIX)-objcopy --redefine-sym _binary_assets_forty_one_vab_start=_binary_music_vab_start --redefine-sym _binary_assets_forty_one_vab_end=_binary_music_vab_end $@

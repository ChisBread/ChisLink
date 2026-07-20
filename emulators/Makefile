.SUFFIXES:

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
LIBGBA ?= $(DEVKITPRO)/libgba

ROOT := ..
FLASH_STORAGE := $(ROOT)/chislink-mcu/flash_storage
POCKETNES := pocketnes/pocketnes_mb.gba
GOOMBACOLOR := goombacolor/goombacolor_mb.gba

.PHONY: all clean sync-flash-storage build-pocketnes build-goombacolor

all: build-pocketnes build-goombacolor

build-pocketnes:
	$(MAKE) -C pocketnes -f Makefile_mb DEVKITARM=$(DEVKITARM) LIBGBA=$(LIBGBA)

build-goombacolor:
	$(MAKE) -C goombacolor TARGET=goombacolor_mb BUILD=build_chislink \
		DEFINES="-DCHISLINK -DCHISLINK_GOOMBACOLOR" \
		DEVKITARM=$(DEVKITARM) LIBGBA=$(LIBGBA)

sync-flash-storage: all | $(FLASH_STORAGE)
	cp $(POCKETNES) $(FLASH_STORAGE)/pocketnes_mb.gba
	cp $(GOOMBACOLOR) $(FLASH_STORAGE)/goombacolor_mb.gba

$(FLASH_STORAGE):
	mkdir -p $@

clean:
	$(MAKE) -C pocketnes -f Makefile_mb clean \
		DEVKITARM=$(DEVKITARM) LIBGBA=$(LIBGBA)
	$(MAKE) -C goombacolor clean TARGET=goombacolor_mb BUILD=build_chislink \
		DEFINES="-DCHISLINK -DCHISLINK_GOOMBACOLOR" \
		DEVKITARM=$(DEVKITARM) LIBGBA=$(LIBGBA)

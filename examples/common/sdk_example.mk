.SUFFIXES:

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
LIBGBA ?= $(DEVKITPRO)/libgba

ifeq ($(wildcard $(DEVKITARM)/gba_rules),)
$(error "Cannot find devkitARM gba_rules. Set DEVKITPRO=/opt/devkitpro or DEVKITARM=<path to>/devkitARM")
endif

include $(DEVKITARM)/gba_rules

EXAMPLE_NAME ?= sdk_example
GAME_TITLE ?= CLSDKEX
TARGET := $(EXAMPLE_NAME)
BUILD := build

PREFIX ?= arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy

ROOT := ../..
COMMON := ../common
RUNTIME := $(ROOT)/gba/runtime
SDK := $(ROOT)/gba/sdk
PROTO := $(ROOT)/common/chislink_proto

INCLUDES := src \
	$(COMMON) \
	$(RUNTIME)/include \
	$(SDK)/include \
	$(PROTO)/include \
	$(LIBGBA)/include

CFLAGS := -std=c99 -Wall -Wextra -Os -flto \
	-mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mthumb-interwork \
	-ffunction-sections -fdata-sections -fno-builtin \
	$(EXTRA_CFLAGS)
DEPFLAGS := -MMD -MP

LDFLAGS := -specs=gba_mb.specs -mthumb -mthumb-interwork \
	-Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map
LIBDIRS := -L$(LIBGBA)/lib
LIBS := -lgba

CFILES := $(wildcard src/*.c) \
	$(COMMON)/example_common.c \
	$(RUNTIME)/src/dma.c \
	$(RUNTIME)/src/hw.c \
	$(RUNTIME)/src/sio.c \
	$(RUNTIME)/src/text.c \
	$(RUNTIME)/src/video.c \
	$(SDK)/src/ble.c \
	$(SDK)/src/ble_link_io.c \
	$(SDK)/src/cart_file.c \
	$(SDK)/src/cart_gba.c \
	$(SDK)/src/cart_nor.c \
	$(SDK)/src/client.c \
	$(SDK)/src/copy.c \
	$(SDK)/src/file.c \
	$(SDK)/src/gba_sio_transport.c \
	$(SDK)/src/net.c \
	$(SDK)/src/socket_compat.c \
	$(SDK)/src/storage_client.c \
	$(SDK)/src/stream.c \
	$(SDK)/src/stream_client.c \
	$(SDK)/src/wire.c \
	$(PROTO)/src/gamedb.c \
	$(PROTO)/src/proto.c

OFILES := $(addprefix $(BUILD)/,$(notdir $(CFILES:.c=.o)))
INCLUDE_FLAGS := $(foreach dir,$(INCLUDES),-I$(dir))
vpath %.c $(sort $(dir $(CFILES)))

.PHONY: all clean

all: $(TARGET).mb.gba

-include $(OFILES:.o=.d)

$(TARGET).mb.gba: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	gbafix $@ -t$(GAME_TITLE) -cCLSE -m01

$(TARGET).elf: $(OFILES) | $(BUILD)
	$(CC) $(CFLAGS) $(OFILES) $(LDFLAGS) $(LIBDIRS) $(LIBS) -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).gba $(TARGET).mb.gba $(EXTRA_CLEAN)

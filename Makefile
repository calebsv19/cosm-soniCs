APP_NAME := daw_app
BUILD_DIR := build
SRC_DIR := src
SDLAPP_DIR := SDLApp

SRCS := \
	$(SDLAPP_DIR)/sdl_app_framework.c \
	$(SRC_DIR)/app/main.c \
	$(SRC_DIR)/config/config.c \
	$(SRC_DIR)/audio/device_sdl.c \
	$(SRC_DIR)/audio/audio_queue.c \
	$(SRC_DIR)/audio/ringbuf.c \
	$(SRC_DIR)/audio/media_clip.c \
	$(SRC_DIR)/engine/engine.c \
	$(SRC_DIR)/engine/graph.c \
	$(SRC_DIR)/engine/buffer_pool.c \
	$(SRC_DIR)/engine/source_tone.c \
	$(SRC_DIR)/engine/sampler.c \
	$(SRC_DIR)/ui/panes.c \
	$(SRC_DIR)/ui/layout.c \
	$(SRC_DIR)/ui/layout_config.c \
	$(SRC_DIR)/ui/library_browser.c \
	$(SRC_DIR)/ui/timeline_view.c \
	$(SRC_DIR)/ui/font5x7.c \
	$(SRC_DIR)/ui/transport.c \
	$(SRC_DIR)/input/input_manager.c

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic

SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)

CPPFLAGS := -Iinclude -I$(SDLAPP_DIR) $(SDL2_CFLAGS)
LDFLAGS := $(SDL2_LIBS)

APP_BIN := $(BUILD_DIR)/$(APP_NAME)

.PHONY: all clean run

all: $(APP_BIN)

$(APP_BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(APP_BIN)
	$(APP_BIN)

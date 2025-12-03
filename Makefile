APP_NAME := daw_app
BUILD_DIR := build
SRC_DIR := src
SDLAPP_DIR := SDLApp

# --- Auto-discover all effect sources (non-recursive per known subdir)
# NOTE: the quotes in compile rules already protect the & in "filter&tone".
EFFECTS_DIRS := \
	$(SRC_DIR)/effects \
	$(SRC_DIR)/effects/basics \
	$(SRC_DIR)/effects/delay \
	$(SRC_DIR)/effects/distortion \
	$(SRC_DIR)/effects/dynamics \
	$(SRC_DIR)/effects/eqs \
	$(SRC_DIR)/effects/filter&tone \
	$(SRC_DIR)/effects/modulation \
	$(SRC_DIR)/effects/reverb

EFFECTS_SRCS := $(foreach d,$(EFFECTS_DIRS),$(wildcard $(d)/*.c))
EFFECTS_SRCS := $(filter-out $(SRC_DIR)/effects/effects_manager.c,$(EFFECTS_SRCS))

# --- The rest of your sources (unchanged, but with hard-coded FX removed)
SRCS := \
	$(SDLAPP_DIR)/sdl_app_framework.c \
	$(SRC_DIR)/app/main.c \
	$(SRC_DIR)/config/config.c \
	$(SRC_DIR)/audio/device_sdl.c \
	$(SRC_DIR)/audio/audio_queue.c \
	$(SRC_DIR)/audio/ringbuf.c \
	$(SRC_DIR)/audio/media_clip.c \
	$(SRC_DIR)/audio/media_cache.c \
	$(SRC_DIR)/engine/engine.c \
	$(SRC_DIR)/engine/graph.c \
	$(SRC_DIR)/engine/buffer_pool.c \
	$(SRC_DIR)/engine/source_tone.c \
	$(SRC_DIR)/engine/sampler.c \
	$(SRC_DIR)/effects/effects_manager.c \
	$(SRC_DIR)/session/session_serialization.c \
	$(SRC_DIR)/ui/panes.c \
	$(SRC_DIR)/ui/layout.c \
	$(SRC_DIR)/ui/layout_config.c \
	$(SRC_DIR)/ui/library_browser.c \
	$(SRC_DIR)/ui/timeline_view.c \
	$(SRC_DIR)/ui/font5x7.c \
	$(SRC_DIR)/ui/transport.c \
	$(SRC_DIR)/ui/clip_inspector.c \
	$(SRC_DIR)/ui/effects_panel.c \
	$(SRC_DIR)/input/input_manager.c \
	$(SRC_DIR)/input/timeline/timeline_input.c \
	$(SRC_DIR)/input/timeline/timeline_selection.c \
	$(SRC_DIR)/input/timeline/timeline_drag.c \
	$(SRC_DIR)/input/inspector_input.c \
	$(SRC_DIR)/input/transport_input.c \
	$(SRC_DIR)/input/effects_panel_input.c \
	$(EFFECTS_SRCS)

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJS_QUOTED := $(foreach obj,$(OBJS),"$(obj)")

CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -MMD -MP

SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)

CPPFLAGS := -Iinclude -Iextern -I$(SDLAPP_DIR) $(SDL2_CFLAGS)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDFLAGS := $(SDL2_LIBS) -framework AudioToolbox -framework CoreFoundation
else
LDFLAGS := $(SDL2_LIBS)
endif

APP_BIN := $(BUILD_DIR)/$(APP_NAME)

TEST_SRCS := \
	tests/session_serialization_test.c

TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_SRCS))
TEST_BIN := $(BUILD_DIR)/tests/session_serialization_test

CACHE_TEST_SRCS := \
	tests/media_cache_stress_test.c

CACHE_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CACHE_TEST_SRCS))
CACHE_TEST_BIN := $(BUILD_DIR)/tests/media_cache_stress_test

OVERLAP_TEST_SRCS := \
	tests/clip_overlap_priority_test.c

OVERLAP_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(OVERLAP_TEST_SRCS))
OVERLAP_TEST_BIN := $(BUILD_DIR)/tests/clip_overlap_priority_test

SMOKE_TEST_SRCS := \
	tests/engine_smoke_test.c

SMOKE_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SMOKE_TEST_SRCS))
SMOKE_TEST_BIN := $(BUILD_DIR)/tests/engine_smoke_test

# ---- Engine test support: keep your existing set, but replace the giant FX list
# with the auto-discovered EFFECTS_SRCS so it always stays in sync.
ENGINE_TEST_SUPPORT_OBJS := \
	$(BUILD_DIR)/src/engine/engine.o \
	$(BUILD_DIR)/src/engine/graph.o \
	$(BUILD_DIR)/src/engine/buffer_pool.o \
	$(BUILD_DIR)/src/engine/source_tone.o \
	$(BUILD_DIR)/src/engine/sampler.o \
	$(BUILD_DIR)/src/effects/effects_manager.o \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(EFFECTS_SRCS)) \
	$(BUILD_DIR)/src/audio/media_cache.o \
	$(BUILD_DIR)/src/audio/media_clip.o \
	$(BUILD_DIR)/src/audio/audio_queue.o \
	$(BUILD_DIR)/src/audio/ringbuf.o \
	$(BUILD_DIR)/src/audio/device_sdl.o \
	$(BUILD_DIR)/src/config/config.o \
	$(BUILD_DIR)/src/input/timeline/timeline_drag.o

APP_DEPS := $(OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)
CACHE_TEST_DEPS := $(CACHE_TEST_OBJS:.o=.d)
OVERLAP_TEST_DEPS := $(OVERLAP_TEST_OBJS:.o=.d)
SMOKE_TEST_DEPS := $(SMOKE_TEST_OBJS:.o=.d)
ENGINE_TEST_SUPPORT_DEPS := $(ENGINE_TEST_SUPPORT_OBJS:.o=.d)
ALL_DEPS := $(APP_DEPS) $(TEST_DEPS) $(CACHE_TEST_DEPS) $(OVERLAP_TEST_DEPS) $(SMOKE_TEST_DEPS) $(ENGINE_TEST_SUPPORT_DEPS)

.PHONY: all clean run test-session test-cache test-overlap test-smoke

all: $(APP_BIN)

$(APP_BIN): $(OBJS)
	@mkdir -p "$(dir $@)"
	$(CC) $(OBJS_QUOTED) -o "$@" $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

clean:
	rm -rf $(BUILD_DIR)

run: $(APP_BIN)
	$(APP_BIN)

test-session: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS) $(BUILD_DIR)/src/session/session_serialization.o $(BUILD_DIR)/src/config/config.o
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-cache: $(CACHE_TEST_BIN)
	$(CACHE_TEST_BIN)

$(CACHE_TEST_BIN): $(CACHE_TEST_OBJS) $(BUILD_DIR)/src/audio/media_cache.o $(BUILD_DIR)/src/audio/media_clip.o
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-overlap: $(OVERLAP_TEST_BIN)
	$(OVERLAP_TEST_BIN)

$(OVERLAP_TEST_BIN): $(OVERLAP_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS)
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-smoke: $(SMOKE_TEST_BIN)
	$(SMOKE_TEST_BIN)

$(SMOKE_TEST_BIN): $(SMOKE_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS)
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

-include $(ALL_DEPS)

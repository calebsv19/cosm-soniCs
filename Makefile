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
	$(SRC_DIR)/audio/wav_writer.c \
	$(SRC_DIR)/audio/media_cache.c \
	$(SRC_DIR)/engine/engine.c \
	$(SRC_DIR)/engine/graph.c \
	$(SRC_DIR)/engine/buffer_pool.c \
	$(SRC_DIR)/time/tempo.c \
	$(SRC_DIR)/engine/source_tone.c \
	$(SRC_DIR)/engine/sampler.c \
	$(SRC_DIR)/effects/effects_manager.c \
	$(SRC_DIR)/session/session_document.c \
	$(SRC_DIR)/session/session_validation.c \
	$(SRC_DIR)/session/session_io_write.c \
	$(SRC_DIR)/session/session_io_read.c \
	$(SRC_DIR)/session/session_io_json.c \
	$(SRC_DIR)/session/session_io_read_parse.c \
	$(SRC_DIR)/session/session_apply.c \
	$(SRC_DIR)/session/project_manager.c \
	$(SRC_DIR)/ui/panes.c \
	$(SRC_DIR)/ui/layout.c \
	$(SRC_DIR)/ui/layout_config.c \
	$(SRC_DIR)/ui/library_browser.c \
	$(SRC_DIR)/ui/timeline_waveform.c \
	$(SRC_DIR)/ui/timeline_view.c \
	$(SRC_DIR)/ui/font.c \
	$(SRC_DIR)/ui/transport.c \
	$(SRC_DIR)/ui/clip_inspector.c \
	$(SRC_DIR)/ui/effects_panel/panel.c \
	$(SRC_DIR)/ui/effects_panel/slot_view.c \
	$(SRC_DIR)/ui/effects_panel/list_view.c \
	$(SRC_DIR)/input/input_manager.c \
	$(SRC_DIR)/input/library_input.c \
	$(SRC_DIR)/input/timeline/timeline_clipboard.c \
	$(SRC_DIR)/input/timeline/timeline_drop.c \
	$(SRC_DIR)/input/timeline/timeline_geometry.c \
	$(SRC_DIR)/input/timeline/timeline_input.c \
	$(SRC_DIR)/input/timeline/timeline_input_keyboard.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse_click.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse_drag.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse_scroll.c \
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

UNAME_S := $(shell uname -s)
SDL_CONFIG := $(shell command -v sdl2-config 2>/dev/null)
SDL2_CFLAGS :=
SDL2_LDFLAGS :=
SDL2_LIBS := -lSDL2 -lSDL2_ttf
SDL2_FRAMEWORKS :=

ifeq ($(UNAME_S),Darwin)
	# Prefer explicit Homebrew prefixes for both Apple Silicon (/opt/homebrew) and Intel (/usr/local).
	ifneq ($(wildcard /opt/homebrew/include/SDL2/SDL.h),)
		SDL2_CFLAGS += -I/opt/homebrew/include -D_THREAD_SAFE
		SDL2_LDFLAGS += -L/opt/homebrew/lib
	endif
	ifneq ($(wildcard /usr/local/include/SDL2/SDL.h),)
		SDL2_CFLAGS += -I/usr/local/include -D_THREAD_SAFE
		SDL2_LDFLAGS += -L/usr/local/lib
	endif

	# Framework fallback when headers are installed as frameworks.
	ifeq ($(strip $(SDL2_CFLAGS)),)
		ifneq ($(wildcard /Library/Frameworks/SDL2.framework/Headers/SDL.h),)
			SDL2_CFLAGS += -F/Library/Frameworks -I/Library/Frameworks/SDL2.framework/Headers -D_THREAD_SAFE
			SDL2_LDFLAGS += -F/Library/Frameworks
			SDL2_LIBS :=
			SDL2_FRAMEWORKS := -framework SDL2
		endif
	endif

	# Last-resort: sdl2-config if nothing else is found.
	ifeq ($(strip $(SDL2_CFLAGS)),)
		ifneq ($(SDL_CONFIG),)
			SDL2_CFLAGS += $(shell $(SDL_CONFIG) --cflags)
			SDL2_LDFLAGS += $(shell $(SDL_CONFIG) --libs)
		endif
	endif
else
	# Non-macOS: prefer sdl2-config, then pkg-config, then a basic system fallback.
	ifneq ($(SDL_CONFIG),)
		SDL2_CFLAGS += $(shell $(SDL_CONFIG) --cflags)
		SDL2_LDFLAGS += $(shell $(SDL_CONFIG) --libs)
	else
		SDL_PKGCONFIG := $(shell pkg-config --exists sdl2 >/dev/null 2>&1 && echo yes)
		ifeq ($(SDL_PKGCONFIG),yes)
			SDL2_CFLAGS += $(shell pkg-config --cflags sdl2)
			SDL2_LDFLAGS += $(shell pkg-config --libs sdl2)
		else
			SDL2_CFLAGS += -I/usr/include/SDL2
			SDL2_LDFLAGS += -L/usr/lib
		endif
	endif
endif

CPPFLAGS := -Iinclude -Iextern -I$(SDLAPP_DIR) $(SDL2_CFLAGS)

LDFLAGS := $(SDL2_LDFLAGS) $(SDL2_LIBS) $(SDL2_FRAMEWORKS)
ifeq ($(UNAME_S),Darwin)
LDFLAGS += -framework AudioToolbox -framework CoreFoundation
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

$(TEST_BIN): $(TEST_OBJS) \
	$(BUILD_DIR)/src/session/session_document.o \
	$(BUILD_DIR)/src/session/session_validation.o \
	$(BUILD_DIR)/src/session/session_io_write.o \
	$(BUILD_DIR)/src/session/session_io_read.o \
	$(BUILD_DIR)/src/session/session_io_json.o \
	$(BUILD_DIR)/src/session/session_io_read_parse.o \
	$(BUILD_DIR)/src/session/session_apply.o \
	$(BUILD_DIR)/src/config/config.o
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

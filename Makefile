APP_NAME := daw_app
BUILD_DIR := build
SRC_DIR := src
SDLAPP_DIR := SDLApp
VK_RENDERER_DIR := ../shared/vk_renderer
CORE_BASE_DIR := ../shared/core/core_base
CORE_IO_DIR := ../shared/core/core_io
CORE_DATA_DIR := ../shared/core/core_data
CORE_PACK_DIR := ../shared/core/core_pack
CORE_TIME_DIR := ../shared/core/core_time
CORE_THEME_DIR := ../shared/core/core_theme
CORE_FONT_DIR := ../shared/core/core_font
CORE_QUEUE_DIR := ../shared/core/core_queue
CORE_SCHED_DIR := ../shared/core/core_sched
CORE_JOBS_DIR := ../shared/core/core_jobs
CORE_WAKE_DIR := ../shared/core/core_wake
CORE_KERNEL_DIR := ../shared/core/core_kernel
KIT_VIZ_DIR := ../shared/kit/kit_viz

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
	$(SRC_DIR)/effects/metering \
	$(SRC_DIR)/effects/modulation \
	$(SRC_DIR)/effects/reverb

EFFECTS_SRCS := $(foreach d,$(EFFECTS_DIRS),$(wildcard $(d)/*.c))
EFFECTS_SRCS := $(filter-out $(SRC_DIR)/effects/effects_manager.c,$(EFFECTS_SRCS))

# --- The rest of your sources (unchanged, but with hard-coded FX removed)
SRCS := \
	$(SDLAPP_DIR)/sdl_app_framework.c \
	$(SRC_DIR)/core/loop/daw_mainthread_wake.c \
	$(SRC_DIR)/core/loop/daw_mainthread_timer.c \
	$(SRC_DIR)/core/loop/daw_mainthread_jobs.c \
	$(SRC_DIR)/core/loop/daw_mainthread_messages.c \
	$(SRC_DIR)/core/loop/daw_mainthread_kernel.c \
	$(SRC_DIR)/core/loop/daw_render_invalidation.c \
	$(SRC_DIR)/app/main.c \
	$(SRC_DIR)/config/config.c \
	$(SRC_DIR)/audio/device_sdl.c \
	$(SRC_DIR)/audio/audio_queue.c \
	$(SRC_DIR)/audio/ringbuf.c \
	$(SRC_DIR)/audio/media_clip.c \
	$(SRC_DIR)/audio/wav_writer.c \
	$(SRC_DIR)/audio/media_cache.c \
	$(SRC_DIR)/audio/media_registry.c \
	$(SRC_DIR)/export/daw_pack_export.c \
	$(SRC_DIR)/engine/audio_source.c \
	$(SRC_DIR)/engine/engine_core.c \
	$(SRC_DIR)/engine/engine_io.c \
	$(SRC_DIR)/engine/engine_fx.c \
	$(SRC_DIR)/engine/engine_transport.c \
	$(SRC_DIR)/engine/engine_tracks.c \
	$(SRC_DIR)/engine/engine_clips.c \
	$(SRC_DIR)/engine/engine_audio.c \
	$(SRC_DIR)/engine/engine_meter.c \
	$(SRC_DIR)/engine/engine_scope_host.c \
	$(SRC_DIR)/engine/engine_eq.c \
	$(SRC_DIR)/engine/engine_spectrum.c \
	$(SRC_DIR)/engine/engine_spectrogram.c \
	$(SRC_DIR)/engine/automation.c \
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
	$(SRC_DIR)/undo/undo_manager.c \
	$(SRC_DIR)/ui/panes.c \
	$(SRC_DIR)/ui/layout.c \
	$(SRC_DIR)/ui/layout_config.c \
	$(SRC_DIR)/ui/library_browser.c \
	$(SRC_DIR)/ui/timeline_waveform.c \
	$(SRC_DIR)/ui/kit_viz_waveform_adapter.c \
	$(SRC_DIR)/ui/kit_viz_fx_preview_adapter.c \
	$(SRC_DIR)/ui/kit_viz_meter_adapter.c \
	$(SRC_DIR)/ui/waveform_render.c \
	$(SRC_DIR)/ui/beat_grid.c \
	$(SRC_DIR)/ui/time_grid.c \
	$(SRC_DIR)/ui/timeline_view.c \
	$(SRC_DIR)/ui/font.c \
	$(SRC_DIR)/ui/shared_theme_font_adapter.c \
	$(SRC_DIR)/ui/transport.c \
	$(SRC_DIR)/ui/clip_inspector.c \
	$(SRC_DIR)/ui/effects_panel/panel.c \
	$(SRC_DIR)/ui/effects_panel/slot_view.c \
	$(SRC_DIR)/ui/effects_panel/slot_preview.c \
	$(SRC_DIR)/ui/effects_panel/slot_layout.c \
	$(SRC_DIR)/ui/effects_panel/slot_widgets.c \
	$(SRC_DIR)/ui/effects_panel/spec_panel.c \
	$(SRC_DIR)/ui/effects_panel/list_view.c \
	$(SRC_DIR)/ui/effects_panel/eq_detail_view.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_view.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_correlation.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_levels.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_lufs.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_mid_side.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_spectrogram.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_vectorscope.c \
	$(SRC_DIR)/ui/effects_panel/track_snapshot_view.c \
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
	$(SRC_DIR)/input/timeline/timeline_snap.c \
	$(SRC_DIR)/input/timeline/timeline_selection.c \
	$(SRC_DIR)/input/timeline/timeline_drag.c \
	$(SRC_DIR)/input/automation_input.c \
	$(SRC_DIR)/input/tempo_overlay_input.c \
	$(SRC_DIR)/input/inspector_input.c \
	$(SRC_DIR)/input/inspector_fade_input.c \
	$(SRC_DIR)/input/transport_input.c \
	$(SRC_DIR)/input/effects_panel_input.c \
	$(SRC_DIR)/input/effects_panel_eq_detail_input.c \
	$(SRC_DIR)/input/effects_panel_track_snapshot.c \
	$(SRC_DIR)/render/timer_hud_adapter.c \
	$(EFFECTS_SRCS)

VK_RENDERER_SRCS := $(shell find $(VK_RENDERER_DIR)/src -type f -name '*.c')
TIMER_HUD_DIR := ../shared/timer_hud
TIMER_HUD_SRCS := $(shell find $(TIMER_HUD_DIR)/src -type f -name '*.c')
TIMER_HUD_EXTERNAL_SRCS := $(TIMER_HUD_DIR)/external/cJSON.c
SRCS += $(VK_RENDERER_SRCS) $(TIMER_HUD_SRCS) $(TIMER_HUD_EXTERNAL_SRCS)
CORE_BASE_SRCS := $(CORE_BASE_DIR)/src/core_base.c
CORE_IO_SRCS := $(CORE_IO_DIR)/src/core_io.c
CORE_DATA_SRCS := $(CORE_DATA_DIR)/src/core_data.c
CORE_PACK_SRCS := $(CORE_PACK_DIR)/src/core_pack.c
CORE_TIME_SRCS := $(CORE_TIME_DIR)/src/core_time.c
ifeq ($(shell uname -s),Darwin)
CORE_TIME_SRCS += $(CORE_TIME_DIR)/src/core_time_mac.c
else
CORE_TIME_SRCS += $(CORE_TIME_DIR)/src/core_time_posix.c
endif
CORE_TIME_TEST_SUPPORT_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CORE_TIME_SRCS))
CORE_THEME_SRCS := $(CORE_THEME_DIR)/src/core_theme.c
CORE_FONT_SRCS := $(CORE_FONT_DIR)/src/core_font.c
CORE_QUEUE_SRCS := $(CORE_QUEUE_DIR)/src/core_queue.c
CORE_SCHED_SRCS := $(CORE_SCHED_DIR)/src/core_sched.c
CORE_JOBS_SRCS := $(CORE_JOBS_DIR)/src/core_jobs.c
CORE_WAKE_SRCS := $(CORE_WAKE_DIR)/src/core_wake.c
CORE_KERNEL_SRCS := $(CORE_KERNEL_DIR)/src/core_kernel.c
SRCS += $(CORE_BASE_SRCS) $(CORE_IO_SRCS) $(CORE_DATA_SRCS) $(CORE_PACK_SRCS) $(CORE_TIME_SRCS) $(CORE_THEME_SRCS) $(CORE_FONT_SRCS) $(CORE_QUEUE_SRCS) $(CORE_SCHED_SRCS) $(CORE_JOBS_SRCS) $(CORE_WAKE_SRCS) $(CORE_KERNEL_SRCS)
KIT_VIZ_SRCS := $(KIT_VIZ_DIR)/src/kit_viz.c
SRCS += $(KIT_VIZ_SRCS)

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
VULKAN_CFLAGS :=
VULKAN_LIBS :=

ifeq ($(UNAME_S),Darwin)
	VULKAN_CFLAGS := $(shell pkg-config --cflags vulkan 2>/dev/null)
	VULKAN_LIBS := $(shell pkg-config --libs vulkan 2>/dev/null)
	ifeq ($(strip $(VULKAN_CFLAGS)$(VULKAN_LIBS)),)
		VULKAN_CFLAGS := -I/opt/homebrew/include
		VULKAN_LIBS := -L/opt/homebrew/lib -lvulkan
	endif

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
	VULKAN_CFLAGS := $(shell pkg-config --cflags vulkan 2>/dev/null)
	VULKAN_LIBS := $(shell pkg-config --libs vulkan 2>/dev/null)
	ifeq ($(strip $(VULKAN_CFLAGS)$(VULKAN_LIBS)),)
		VULKAN_CFLAGS := -I/usr/include
		VULKAN_LIBS := -lvulkan
	endif

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

CPPFLAGS := -Iinclude -Iextern -I$(SDLAPP_DIR) -I$(VK_RENDERER_DIR)/include -I$(TIMER_HUD_DIR)/include -I$(TIMER_HUD_DIR)/external -I$(CORE_BASE_DIR)/include -I$(CORE_IO_DIR)/include -I$(CORE_DATA_DIR)/include -I$(CORE_PACK_DIR)/include -I$(CORE_TIME_DIR)/include -I$(CORE_THEME_DIR)/include -I$(CORE_FONT_DIR)/include -I$(CORE_QUEUE_DIR)/include -I$(CORE_SCHED_DIR)/include -I$(CORE_JOBS_DIR)/include -I$(CORE_WAKE_DIR)/include -I$(CORE_KERNEL_DIR)/include -I$(KIT_VIZ_DIR)/include $(SDL2_CFLAGS) $(VULKAN_CFLAGS) -DVK_RENDERER_SHADER_ROOT=\"$(abspath $(VK_RENDERER_DIR))\" -include $(VK_RENDERER_DIR)/include/vk_renderer_sdl.h

LDFLAGS := $(SDL2_LDFLAGS) $(SDL2_LIBS) $(SDL2_FRAMEWORKS) $(VULKAN_LIBS)
ifeq ($(UNAME_S),Darwin)
	CFLAGS += -DVK_USE_PLATFORM_METAL_EXT
	LDFLAGS += -framework AudioToolbox -framework CoreFoundation -framework Metal -framework QuartzCore -framework Cocoa -framework IOKit -framework CoreVideo
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

KITVIZ_ADAPTER_TEST_SRCS := \
	tests/kit_viz_waveform_adapter_test.c

KITVIZ_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/kit_viz_waveform_adapter_test

WAVEFORM_PACK_WARMSTART_TEST_SRCS := \
	tests/waveform_cache_pack_warmstart_test.c

WAVEFORM_PACK_WARMSTART_TEST_BIN := $(BUILD_DIR)/tests/waveform_cache_pack_warmstart_test

KITVIZ_FX_PREVIEW_ADAPTER_TEST_SRCS := \
	tests/kit_viz_fx_preview_adapter_test.c

KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/kit_viz_fx_preview_adapter_test

KITVIZ_METER_ADAPTER_TEST_SRCS := \
	tests/kit_viz_meter_adapter_test.c

KITVIZ_METER_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/kit_viz_meter_adapter_test

SHARED_THEME_FONT_ADAPTER_TEST_SRCS := \
	tests/shared_theme_font_adapter_test.c

SHARED_THEME_FONT_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/shared_theme_font_adapter_test

# ---- Engine test support: keep your existing set, but replace the giant FX list
# with the auto-discovered EFFECTS_SRCS so it always stays in sync.
ENGINE_TEST_SUPPORT_OBJS := \
	$(BUILD_DIR)/src/engine/engine_core.o \
	$(BUILD_DIR)/src/engine/audio_source.o \
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
	$(CORE_TIME_TEST_SUPPORT_OBJS) \
	$(BUILD_DIR)/src/input/timeline/timeline_drag.o

APP_DEPS := $(OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)
CACHE_TEST_DEPS := $(CACHE_TEST_OBJS:.o=.d)
OVERLAP_TEST_DEPS := $(OVERLAP_TEST_OBJS:.o=.d)
SMOKE_TEST_DEPS := $(SMOKE_TEST_OBJS:.o=.d)
ENGINE_TEST_SUPPORT_DEPS := $(ENGINE_TEST_SUPPORT_OBJS:.o=.d)
ALL_DEPS := $(APP_DEPS) $(TEST_DEPS) $(CACHE_TEST_DEPS) $(OVERLAP_TEST_DEPS) $(SMOKE_TEST_DEPS) $(ENGINE_TEST_SUPPORT_DEPS)

.PHONY: all clean run run-ide-theme loop-gates loop-gates-strict test-session test-cache test-overlap test-smoke test-kitviz-adapter test-waveform-pack-warmstart test-kitviz-fx-preview-adapter test-kitviz-meter-adapter test-shared-theme-font-adapter

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

run-ide-theme: $(APP_BIN)
	DAW_USE_SHARED_THEME_FONT=1 DAW_USE_SHARED_THEME=1 DAW_USE_SHARED_FONT=1 DAW_THEME_PRESET=ide_gray DAW_FONT_PRESET=ide $(APP_BIN)

loop-gates: $(APP_BIN)
	RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

loop-gates-strict: $(APP_BIN)
	PROFILE=strict STRICT=1 RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

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

test-kitviz-adapter: $(KITVIZ_ADAPTER_TEST_BIN)
	$(KITVIZ_ADAPTER_TEST_BIN)

$(KITVIZ_ADAPTER_TEST_BIN): $(KITVIZ_ADAPTER_TEST_SRCS) src/ui/kit_viz_waveform_adapter.c src/ui/timeline_waveform.c ../shared/kit/kit_viz/src/kit_viz.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I../shared/kit/kit_viz/include -I../shared/core/core_pack/include -I../shared/core/core_io/include -I../shared/core/core_base/include \
		tests/kit_viz_waveform_adapter_test.c src/ui/kit_viz_waveform_adapter.c src/ui/timeline_waveform.c ../shared/kit/kit_viz/src/kit_viz.c ../shared/core/core_pack/src/core_pack.c ../shared/core/core_io/src/core_io.c ../shared/core/core_base/src/core_base.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-waveform-pack-warmstart: $(WAVEFORM_PACK_WARMSTART_TEST_BIN)
	$(WAVEFORM_PACK_WARMSTART_TEST_BIN)

$(WAVEFORM_PACK_WARMSTART_TEST_BIN): $(WAVEFORM_PACK_WARMSTART_TEST_SRCS) src/ui/timeline_waveform.c ../shared/kit/kit_viz/src/kit_viz.c ../shared/core/core_pack/src/core_pack.c ../shared/core/core_io/src/core_io.c ../shared/core/core_base/src/core_base.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I../shared/kit/kit_viz/include -I../shared/core/core_pack/include -I../shared/core/core_io/include -I../shared/core/core_base/include \
		tests/waveform_cache_pack_warmstart_test.c src/ui/timeline_waveform.c ../shared/kit/kit_viz/src/kit_viz.c ../shared/core/core_pack/src/core_pack.c ../shared/core/core_io/src/core_io.c ../shared/core/core_base/src/core_base.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-kitviz-fx-preview-adapter: $(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN)
	$(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN)

$(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN): $(KITVIZ_FX_PREVIEW_ADAPTER_TEST_SRCS) src/ui/kit_viz_fx_preview_adapter.c ../shared/kit/kit_viz/src/kit_viz.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I../shared/kit/kit_viz/include -I../shared/core/core_base/include \
		tests/kit_viz_fx_preview_adapter_test.c src/ui/kit_viz_fx_preview_adapter.c ../shared/kit/kit_viz/src/kit_viz.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-kitviz-meter-adapter: $(KITVIZ_METER_ADAPTER_TEST_BIN)
	$(KITVIZ_METER_ADAPTER_TEST_BIN)

$(KITVIZ_METER_ADAPTER_TEST_BIN): $(KITVIZ_METER_ADAPTER_TEST_SRCS) src/ui/kit_viz_meter_adapter.c ../shared/kit/kit_viz/src/kit_viz.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I../shared/kit/kit_viz/include -I../shared/core/core_base/include \
		tests/kit_viz_meter_adapter_test.c src/ui/kit_viz_meter_adapter.c ../shared/kit/kit_viz/src/kit_viz.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-shared-theme-font-adapter: $(SHARED_THEME_FONT_ADAPTER_TEST_BIN)
	$(SHARED_THEME_FONT_ADAPTER_TEST_BIN)

$(SHARED_THEME_FONT_ADAPTER_TEST_BIN): $(SHARED_THEME_FONT_ADAPTER_TEST_SRCS) src/ui/shared_theme_font_adapter.c ../shared/core/core_theme/src/core_theme.c ../shared/core/core_font/src/core_font.c ../shared/core/core_base/src/core_base.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I../shared/core/core_theme/include -I../shared/core/core_font/include -I../shared/core/core_base/include \
		tests/shared_theme_font_adapter_test.c src/ui/shared_theme_font_adapter.c ../shared/core/core_theme/src/core_theme.c ../shared/core/core_font/src/core_font.c ../shared/core/core_base/src/core_base.c \
		$(SDL2_LDFLAGS) -lSDL2 -o "$@"

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

-include $(ALL_DEPS)

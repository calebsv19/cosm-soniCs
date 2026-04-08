APP_NAME := daw_app
BUILD_DIR := build
SRC_DIR := src
SDLAPP_DIR := SDLApp
SHARED_ROOT ?= third_party/codework_shared
VK_RENDERER_DIR := $(SHARED_ROOT)/vk_renderer
CORE_BASE_DIR := $(SHARED_ROOT)/core/core_base
CORE_IO_DIR := $(SHARED_ROOT)/core/core_io
CORE_DATA_DIR := $(SHARED_ROOT)/core/core_data
CORE_PACK_DIR := $(SHARED_ROOT)/core/core_pack
CORE_TIME_DIR := $(SHARED_ROOT)/core/core_time
CORE_THEME_DIR := $(SHARED_ROOT)/core/core_theme
CORE_FONT_DIR := $(SHARED_ROOT)/core/core_font
CORE_QUEUE_DIR := $(SHARED_ROOT)/core/core_queue
CORE_SCHED_DIR := $(SHARED_ROOT)/core/core_sched
CORE_JOBS_DIR := $(SHARED_ROOT)/core/core_jobs
CORE_WAKE_DIR := $(SHARED_ROOT)/core/core_wake
CORE_KERNEL_DIR := $(SHARED_ROOT)/core/core_kernel
KIT_VIZ_DIR := $(SHARED_ROOT)/kit/kit_viz

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
	$(SRC_DIR)/app/daw_app_main.c \
	$(SRC_DIR)/app/main.c \
	$(SRC_DIR)/config/config.c \
	$(SRC_DIR)/config/data_paths.c \
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
TIMER_HUD_DIR := $(SHARED_ROOT)/timer_hud
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
DIST_DIR := dist
PACKAGE_APP_NAME := soniCs.app
PACKAGE_APP_DIR := $(DIST_DIR)/$(PACKAGE_APP_NAME)
PACKAGE_CONTENTS_DIR := $(PACKAGE_APP_DIR)/Contents
PACKAGE_MACOS_DIR := $(PACKAGE_CONTENTS_DIR)/MacOS
PACKAGE_RESOURCES_DIR := $(PACKAGE_CONTENTS_DIR)/Resources
PACKAGE_FRAMEWORKS_DIR := $(PACKAGE_CONTENTS_DIR)/Frameworks
PACKAGE_INFO_PLIST_SRC := tools/packaging/macos/Info.plist
PACKAGE_LAUNCHER_SRC := tools/packaging/macos/daw-launcher
PACKAGE_DYLIB_BUNDLER := tools/packaging/macos/bundle-dylibs.sh
DESKTOP_APP_DIR ?= $(HOME)/Desktop/$(PACKAGE_APP_NAME)
PACKAGE_ADHOC_SIGN_IDENTITY ?= -
RELEASE_VERSION_FILE ?= VERSION
RELEASE_VERSION ?= $(strip $(shell cat "$(RELEASE_VERSION_FILE)" 2>/dev/null))
ifeq ($(RELEASE_VERSION),)
RELEASE_VERSION := 0.1.0
endif
RELEASE_CHANNEL ?= stable
RELEASE_PRODUCT_NAME := soniCs
RELEASE_PROGRAM_KEY := daw
RELEASE_BUNDLE_ID := com.cosm.sonics
RELEASE_ARTIFACT_BASENAME := $(RELEASE_PRODUCT_NAME)-$(RELEASE_VERSION)-macOS-$(RELEASE_CHANNEL)
RELEASE_DIR := build/release
RELEASE_APP_ZIP := $(RELEASE_DIR)/$(RELEASE_ARTIFACT_BASENAME).zip
RELEASE_MANIFEST := $(RELEASE_DIR)/$(RELEASE_ARTIFACT_BASENAME).manifest.txt
RELEASE_CODESIGN_IDENTITY ?= $(if $(strip $(APPLE_SIGN_IDENTITY)),$(APPLE_SIGN_IDENTITY),$(PACKAGE_ADHOC_SIGN_IDENTITY))
APPLE_SIGN_IDENTITY ?=
APPLE_NOTARY_PROFILE ?=
APPLE_TEAM_ID ?=
STAPLE_MAX_ATTEMPTS ?= 6
STAPLE_RETRY_DELAY_SEC ?= 15

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

PACK_CONTRACT_TEST_SRCS := \
	tests/daw_pack_contract_parity_test.c

PACK_CONTRACT_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(PACK_CONTRACT_TEST_SRCS))
PACK_CONTRACT_TEST_BIN := $(BUILD_DIR)/tests/daw_pack_contract_parity_test

TRACE_CONTRACT_TEST_SRCS := \
	tests/daw_trace_export_contract_test.c

TRACE_CONTRACT_TEST_BIN := $(BUILD_DIR)/tests/daw_trace_export_contract_test

TRACE_ASYNC_CONTRACT_TEST_SRCS := \
	tests/daw_trace_export_async_contract_test.c

TRACE_ASYNC_CONTRACT_TEST_BIN := $(BUILD_DIR)/tests/daw_trace_export_async_contract_test

KITVIZ_FX_PREVIEW_ADAPTER_TEST_SRCS := \
	tests/kit_viz_fx_preview_adapter_test.c

KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/kit_viz_fx_preview_adapter_test

KITVIZ_METER_ADAPTER_TEST_SRCS := \
	tests/kit_viz_meter_adapter_test.c

KITVIZ_METER_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/kit_viz_meter_adapter_test

SHARED_THEME_FONT_ADAPTER_TEST_SRCS := \
	tests/shared_theme_font_adapter_test.c

SHARED_THEME_FONT_ADAPTER_TEST_BIN := $(BUILD_DIR)/tests/shared_theme_font_adapter_test

LAYOUT_SWEEP_TEST_SRCS := \
	tests/layout_text_scaling_sweep_test.c

LAYOUT_SWEEP_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LAYOUT_SWEEP_TEST_SRCS))
LAYOUT_SWEEP_TEST_BIN := $(BUILD_DIR)/tests/layout_text_scaling_sweep_test

DATA_PATH_CONTRACT_TEST_SRCS := \
	tests/daw_data_path_contract_test.c

DATA_PATH_CONTRACT_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DATA_PATH_CONTRACT_TEST_SRCS))
DATA_PATH_CONTRACT_TEST_BIN := $(BUILD_DIR)/tests/daw_data_path_contract_test

# Keep legacy app tests wired to the full non-main app object set so link coverage
# stays in lock-step with engine/runtime refactors.
ENGINE_TEST_SUPPORT_OBJS = $(APP_OBJS_NO_MAIN)

APP_DEPS := $(OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)
CACHE_TEST_DEPS := $(CACHE_TEST_OBJS:.o=.d)
OVERLAP_TEST_DEPS := $(OVERLAP_TEST_OBJS:.o=.d)
SMOKE_TEST_DEPS := $(SMOKE_TEST_OBJS:.o=.d)
PACK_CONTRACT_TEST_DEPS := $(PACK_CONTRACT_TEST_OBJS:.o=.d)
LAYOUT_SWEEP_TEST_DEPS := $(LAYOUT_SWEEP_TEST_OBJS:.o=.d)
DATA_PATH_CONTRACT_TEST_DEPS := $(DATA_PATH_CONTRACT_TEST_OBJS:.o=.d)
ENGINE_TEST_SUPPORT_DEPS := $(ENGINE_TEST_SUPPORT_OBJS:.o=.d)
ALL_DEPS := $(APP_DEPS) $(TEST_DEPS) $(CACHE_TEST_DEPS) $(OVERLAP_TEST_DEPS) $(SMOKE_TEST_DEPS) $(PACK_CONTRACT_TEST_DEPS) $(LAYOUT_SWEEP_TEST_DEPS) $(DATA_PATH_CONTRACT_TEST_DEPS) $(ENGINE_TEST_SUPPORT_DEPS)

APP_OBJS_NO_MAIN := $(filter-out $(BUILD_DIR)/src/app/main.o,$(OBJS))

STABLE_TEST_TARGETS := \
	test-pack-contract \
	test-trace-contract \
	test-trace-async-contract \
	test-kitviz-adapter \
	test-kitviz-fx-preview-adapter \
	test-kitviz-meter-adapter \
	test-waveform-pack-warmstart \
	test-layout-sweep \
	test-data-path-contract \
	test-library-copy-vs-reference-contract

LEGACY_TEST_TARGETS := \
	test-session \
	test-cache \
	test-overlap \
	test-smoke \
	test-shared-theme-font-adapter

.PHONY: all clean run run-ide-theme run-headless-smoke visual-harness package-desktop package-desktop-smoke package-desktop-self-test package-desktop-copy-desktop package-desktop-sync package-desktop-open package-desktop-remove package-desktop-refresh release-contract release-clean release-build release-bundle-audit release-sign release-verify release-verify-signed release-notarize release-staple release-verify-notarized release-artifact release-distribute release-desktop-refresh loop-gates loop-gates-strict test-stable test-legacy test-session test-cache test-overlap test-smoke test-kitviz-adapter test-waveform-pack-warmstart test-pack-contract test-trace-contract test-trace-async-contract test-kitviz-fx-preview-adapter test-kitviz-meter-adapter test-shared-theme-font-adapter test-layout-sweep test-data-path-contract test-library-copy-vs-reference-contract

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

run-headless-smoke: all test-stable
	@echo "daw headless smoke passed (non-interactive)"

visual-harness: $(APP_BIN)
	@echo "visual harness binary ready: $(APP_BIN)"

package-desktop: all
	@echo "Preparing desktop package..."
	@rm -rf "$(PACKAGE_APP_DIR)"
	@mkdir -p "$(PACKAGE_MACOS_DIR)" "$(PACKAGE_RESOURCES_DIR)" "$(PACKAGE_FRAMEWORKS_DIR)"
	@cp "$(PACKAGE_INFO_PLIST_SRC)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@cp "$(APP_BIN)" "$(PACKAGE_MACOS_DIR)/daw-bin"
	@cp "$(PACKAGE_LAUNCHER_SRC)" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@chmod +x "$(PACKAGE_MACOS_DIR)/daw-bin" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@"$(PACKAGE_DYLIB_BUNDLER)" "$(PACKAGE_MACOS_DIR)/daw-bin" "$(PACKAGE_FRAMEWORKS_DIR)"
	@mkdir -p "$(PACKAGE_RESOURCES_DIR)/config" "$(PACKAGE_RESOURCES_DIR)/assets" "$(PACKAGE_RESOURCES_DIR)/include" "$(PACKAGE_RESOURCES_DIR)/shared/assets" "$(PACKAGE_RESOURCES_DIR)/vk_renderer" "$(PACKAGE_RESOURCES_DIR)/shaders"
	@cp -R config/. "$(PACKAGE_RESOURCES_DIR)/config/"
	@cp -R assets/audio "$(PACKAGE_RESOURCES_DIR)/assets/"
	@cp -R include/fonts "$(PACKAGE_RESOURCES_DIR)/include/"
	@cp -R "$(SHARED_ROOT)/assets/fonts" "$(PACKAGE_RESOURCES_DIR)/shared/assets/"
	@cp -R "$(VK_RENDERER_DIR)/shaders" "$(PACKAGE_RESOURCES_DIR)/vk_renderer/"
	@cp -R "$(VK_RENDERER_DIR)/shaders/." "$(PACKAGE_RESOURCES_DIR)/shaders/"
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" "$$dylib"; \
	done
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-bin"
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" "$(PACKAGE_APP_DIR)"
	@codesign --verify --deep --strict "$(PACKAGE_APP_DIR)"
	@echo "Desktop package ready: $(PACKAGE_APP_DIR)"

package-desktop-smoke: package-desktop
	@test -x "$(PACKAGE_MACOS_DIR)/daw-launcher" || (echo "Missing launcher"; exit 1)
	@test -x "$(PACKAGE_MACOS_DIR)/daw-bin" || (echo "Missing daw-bin"; exit 1)
	@test -f "$(PACKAGE_CONTENTS_DIR)/Info.plist" || (echo "Missing Info.plist"; exit 1)
	@test -f "$(PACKAGE_FRAMEWORKS_DIR)/libvulkan.1.dylib" || (echo "Missing bundled libvulkan"; exit 1)
	@test -f "$(PACKAGE_FRAMEWORKS_DIR)/libMoltenVK.dylib" || (echo "Missing bundled libMoltenVK"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/config/engine.cfg" || (echo "Missing config/engine.cfg"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/assets/audio/README.md" || (echo "Missing bundled audio README"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/include/fonts/Montserrat/Montserrat-Regular.ttf" || (echo "Missing bundled Montserrat"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/vk_renderer/shaders/textured.vert.spv" || (echo "Missing bundled vk shaders"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/shaders/textured.vert.spv" || (echo "Missing bundled runtime shader"; exit 1)
	@echo "package-desktop-smoke passed."

package-desktop-self-test: package-desktop-smoke
	@"$(PACKAGE_MACOS_DIR)/daw-launcher" --self-test || (echo "package-desktop self-test failed."; exit 1)
	@echo "package-desktop-self-test passed."

package-desktop-copy-desktop: package-desktop
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@cp -R "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@echo "Copied $(PACKAGE_APP_NAME) to $(DESKTOP_APP_DIR)"

package-desktop-sync: package-desktop-copy-desktop
	@echo "Desktop app sync complete."

package-desktop-open: package-desktop
	@open "$(PACKAGE_APP_DIR)"

package-desktop-remove:
	@rm -rf "$(DESKTOP_APP_DIR)"
	@echo "Removed desktop copy at $(DESKTOP_APP_DIR)"

package-desktop-refresh: package-desktop
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@cp -R "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@echo "Refreshed $(PACKAGE_APP_NAME) at $(DESKTOP_APP_DIR)"

release-contract:
	@echo "RELEASE_PROGRAM_KEY=$(RELEASE_PROGRAM_KEY)"
	@echo "RELEASE_PRODUCT_NAME=$(RELEASE_PRODUCT_NAME)"
	@echo "RELEASE_BUNDLE_ID=$(RELEASE_BUNDLE_ID)"
	@echo "RELEASE_VERSION=$(RELEASE_VERSION)"
	@echo "RELEASE_CHANNEL=$(RELEASE_CHANNEL)"
	@test "$(RELEASE_PRODUCT_NAME)" = "soniCs" || (echo "Unexpected release product"; exit 1)
	@test "$(RELEASE_PROGRAM_KEY)" = "daw" || (echo "Unexpected release program key"; exit 1)
	@test "$(RELEASE_BUNDLE_ID)" = "com.cosm.sonics" || (echo "Unexpected release bundle id"; exit 1)
	@test -f "$(RELEASE_VERSION_FILE)" || (echo "Missing VERSION file"; exit 1)
	@echo "release-contract passed."

release-clean:
	@rm -rf "$(RELEASE_DIR)"
	@echo "release-clean complete."

release-build:
	@$(MAKE) package-desktop-self-test
	@echo "release-build complete."

release-bundle-audit: release-build
	@mkdir -p "$(RELEASE_DIR)"
	@/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$(PACKAGE_CONTENTS_DIR)/Info.plist" > "$(RELEASE_DIR)/bundle_id.txt"
	@test "$$(cat "$(RELEASE_DIR)/bundle_id.txt")" = "$(RELEASE_BUNDLE_ID)" || (echo "Bundle identifier mismatch"; exit 1)
	@otool -L "$(PACKAGE_MACOS_DIR)/daw-bin" > "$(RELEASE_DIR)/otool_daw_bin.txt"
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		out="$(RELEASE_DIR)/otool_$$(basename "$$dylib").txt"; \
		otool -L "$$dylib" > "$$out"; \
	done
	@! rg -q '/opt/homebrew|/usr/local|/Users/' "$(RELEASE_DIR)"/otool_*.txt || (echo "Found non-portable dylib linkage"; exit 1)
	@! rg -q '@rpath/' "$(RELEASE_DIR)"/otool_*.txt || (echo "Found unresolved @rpath dylib linkage"; exit 1)
	@"$(PACKAGE_MACOS_DIR)/daw-launcher" --print-config > "$(RELEASE_DIR)/print_config.txt"
	@rg -q '^DAW_RUNTIME_DIR=' "$(RELEASE_DIR)/print_config.txt" || (echo "Missing DAW_RUNTIME_DIR in launcher config"; exit 1)
	@rg -q '^VK_ICD_FILENAMES=' "$(RELEASE_DIR)/print_config.txt" || (echo "Missing VK_ICD_FILENAMES in launcher config"; exit 1)
	@echo "release-bundle-audit passed."

release-sign: release-bundle-audit
	@test -n "$(RELEASE_CODESIGN_IDENTITY)" || (echo "Missing signing identity"; exit 1)
	@echo "Signing with identity: $(RELEASE_CODESIGN_IDENTITY)"
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$$dylib"; \
	done
	@codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-bin"
	@codesign --force --timestamp --options runtime --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_APP_DIR)"
	@echo "release-sign complete."

release-verify: release-sign
	@codesign --verify --deep --strict "$(PACKAGE_APP_DIR)"
	@set +e; spctl_out="$$(spctl --assess --type execute --verbose=2 "$(PACKAGE_APP_DIR)" 2>&1)"; spctl_rc=$$?; set -e; \
	echo "$$spctl_out"; \
	if [ $$spctl_rc -eq 0 ]; then \
		echo "release-verify passed."; \
	elif printf '%s' "$$spctl_out" | rg -q 'source=Unnotarized Developer ID'; then \
		echo "release-verify passed (pre-notary signed state)."; \
	else \
		echo "release-verify failed."; \
		exit $$spctl_rc; \
	fi

release-verify-signed: release-verify
	@echo "release-verify-signed passed."

release-notarize: release-sign
	@test -n "$(APPLE_NOTARY_PROFILE)" || (echo "Missing APPLE_NOTARY_PROFILE"; exit 1)
	@mkdir -p "$(RELEASE_DIR)"
	@ditto -c -k --keepParent "$(PACKAGE_APP_DIR)" "$(RELEASE_APP_ZIP)"
	@xcrun notarytool submit "$(RELEASE_APP_ZIP)" --keychain-profile "$(APPLE_NOTARY_PROFILE)" --wait --output-format json > "$(RELEASE_DIR)/notary_submit.json"
	@rg -q '"status"[[:space:]]*:[[:space:]]*"Accepted"' "$(RELEASE_DIR)/notary_submit.json" || (cat "$(RELEASE_DIR)/notary_submit.json" && echo "Notary submission was not accepted" && exit 1)
	@echo "release-notarize passed."

release-staple:
	@attempt=1; \
	while [ $$attempt -le $(STAPLE_MAX_ATTEMPTS) ]; do \
		if xcrun stapler staple "$(PACKAGE_APP_DIR)" && xcrun stapler validate "$(PACKAGE_APP_DIR)"; then \
			echo "release-staple passed."; \
			exit 0; \
		fi; \
		echo "staple attempt $$attempt/$(STAPLE_MAX_ATTEMPTS) failed; retrying in $(STAPLE_RETRY_DELAY_SEC)s"; \
		sleep $(STAPLE_RETRY_DELAY_SEC); \
		attempt=$$((attempt+1)); \
	done; \
	echo "release-staple failed."; \
	exit 1

release-verify-notarized: release-staple
	@spctl --assess --type execute --verbose=2 "$(PACKAGE_APP_DIR)"
	@xcrun stapler validate "$(PACKAGE_APP_DIR)"
	@echo "release-verify-notarized passed."

release-artifact: release-verify-notarized
	@mkdir -p "$(RELEASE_DIR)"
	@ditto -c -k --keepParent "$(PACKAGE_APP_DIR)" "$(RELEASE_APP_ZIP)"
	@shasum -a 256 "$(RELEASE_APP_ZIP)" > "$(RELEASE_APP_ZIP).sha256"
	@{ \
		echo "product=$(RELEASE_PRODUCT_NAME)"; \
		echo "program=$(RELEASE_PROGRAM_KEY)"; \
		echo "version=$(RELEASE_VERSION)"; \
		echo "channel=$(RELEASE_CHANNEL)"; \
		echo "bundle_id=$(RELEASE_BUNDLE_ID)"; \
		echo "zip=$(RELEASE_APP_ZIP)"; \
		echo "sha256=$$(cut -d' ' -f1 "$(RELEASE_APP_ZIP).sha256")"; \
	} > "$(RELEASE_MANIFEST)"
	@echo "release-artifact complete: $(RELEASE_APP_ZIP)"

release-distribute: release-artifact
	@echo "release-distribute passed."

release-desktop-refresh: release-distribute
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@cp -R "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@spctl --assess --type execute --verbose=2 "$(DESKTOP_APP_DIR)"
	@echo "release-desktop-refresh passed."

loop-gates: $(APP_BIN)
	RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

loop-gates-strict: $(APP_BIN)
	PROFILE=strict STRICT=1 RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

test-stable:
	@$(MAKE) $(STABLE_TEST_TARGETS)
	@echo "daw stable test lane passed"

test-legacy:
	@set +e; \
	fails=0; \
	for t in $(LEGACY_TEST_TARGETS); do \
		echo "[legacy] running $$t"; \
		$(MAKE) $$t || fails=1; \
	done; \
	if [ $$fails -ne 0 ]; then \
		echo "[legacy] one or more legacy tests failed"; \
		exit 1; \
	fi

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

$(KITVIZ_ADAPTER_TEST_BIN): $(KITVIZ_ADAPTER_TEST_SRCS) src/ui/kit_viz_waveform_adapter.c src/ui/timeline_waveform.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/kit_viz_waveform_adapter_test.c src/ui/kit_viz_waveform_adapter.c src/ui/timeline_waveform.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-waveform-pack-warmstart: $(WAVEFORM_PACK_WARMSTART_TEST_BIN)
	$(WAVEFORM_PACK_WARMSTART_TEST_BIN)

$(WAVEFORM_PACK_WARMSTART_TEST_BIN): $(WAVEFORM_PACK_WARMSTART_TEST_SRCS) src/ui/timeline_waveform.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/waveform_cache_pack_warmstart_test.c src/ui/timeline_waveform.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-layout-sweep: $(LAYOUT_SWEEP_TEST_BIN)
	$(LAYOUT_SWEEP_TEST_BIN)

$(LAYOUT_SWEEP_TEST_BIN): $(LAYOUT_SWEEP_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-data-path-contract: $(DATA_PATH_CONTRACT_TEST_BIN)
	$(DATA_PATH_CONTRACT_TEST_BIN)

test-library-copy-vs-reference-contract: test-data-path-contract
	@echo "test-library-copy-vs-reference-contract: success"

$(DATA_PATH_CONTRACT_TEST_BIN): $(DATA_PATH_CONTRACT_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-pack-contract: $(PACK_CONTRACT_TEST_BIN)
	$(PACK_CONTRACT_TEST_BIN)

$(PACK_CONTRACT_TEST_BIN): $(PACK_CONTRACT_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-trace-contract: $(TRACE_CONTRACT_TEST_BIN)
	$(TRACE_CONTRACT_TEST_BIN)

$(TRACE_CONTRACT_TEST_BIN): $(TRACE_CONTRACT_TEST_SRCS) src/export/daw_trace_export.c $(SHARED_ROOT)/core/core_trace/src/core_trace.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Iinclude -I$(SHARED_ROOT)/core/core_trace/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/daw_trace_export_contract_test.c src/export/daw_trace_export.c $(SHARED_ROOT)/core/core_trace/src/core_trace.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c \
		-lm -o "$@"

test-trace-async-contract: $(TRACE_ASYNC_CONTRACT_TEST_BIN)
	$(TRACE_ASYNC_CONTRACT_TEST_BIN)

$(TRACE_ASYNC_CONTRACT_TEST_BIN): $(TRACE_ASYNC_CONTRACT_TEST_SRCS) src/export/daw_trace_export.c src/export/daw_trace_export_async.c $(SHARED_ROOT)/core/core_workers/src/core_workers.c $(SHARED_ROOT)/core/core_trace/src/core_trace.c $(SHARED_ROOT)/core/core_queue/src/core_queue.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Iinclude -I$(SHARED_ROOT)/core/core_workers/include -I$(SHARED_ROOT)/core/core_trace/include -I$(SHARED_ROOT)/core/core_queue/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/daw_trace_export_async_contract_test.c src/export/daw_trace_export.c src/export/daw_trace_export_async.c $(SHARED_ROOT)/core/core_workers/src/core_workers.c $(SHARED_ROOT)/core/core_trace/src/core_trace.c $(SHARED_ROOT)/core/core_queue/src/core_queue.c $(SHARED_ROOT)/core/core_pack/src/core_pack.c $(SHARED_ROOT)/core/core_io/src/core_io.c $(SHARED_ROOT)/core/core_base/src/core_base.c \
		-lm -lpthread -o "$@"

test-kitviz-fx-preview-adapter: $(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN)
	$(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN)

$(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN): $(KITVIZ_FX_PREVIEW_ADAPTER_TEST_SRCS) src/ui/kit_viz_fx_preview_adapter.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/kit_viz_fx_preview_adapter_test.c src/ui/kit_viz_fx_preview_adapter.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-kitviz-meter-adapter: $(KITVIZ_METER_ADAPTER_TEST_BIN)
	$(KITVIZ_METER_ADAPTER_TEST_BIN)

$(KITVIZ_METER_ADAPTER_TEST_BIN): $(KITVIZ_METER_ADAPTER_TEST_SRCS) src/ui/kit_viz_meter_adapter.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/kit_viz_meter_adapter_test.c src/ui/kit_viz_meter_adapter.c $(SHARED_ROOT)/kit/kit_viz/src/kit_viz.c \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-shared-theme-font-adapter: $(SHARED_THEME_FONT_ADAPTER_TEST_BIN)
	$(SHARED_THEME_FONT_ADAPTER_TEST_BIN)

$(SHARED_THEME_FONT_ADAPTER_TEST_BIN): $(SHARED_THEME_FONT_ADAPTER_TEST_SRCS) src/ui/shared_theme_font_adapter.c $(SHARED_ROOT)/core/core_theme/src/core_theme.c $(SHARED_ROOT)/core/core_font/src/core_font.c $(SHARED_ROOT)/core/core_base/src/core_base.c
	@mkdir -p "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/core/core_theme/include -I$(SHARED_ROOT)/core/core_font/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/shared_theme_font_adapter_test.c src/ui/shared_theme_font_adapter.c $(SHARED_ROOT)/core/core_theme/src/core_theme.c $(SHARED_ROOT)/core/core_font/src/core_font.c $(SHARED_ROOT)/core/core_base/src/core_base.c \
		$(SDL2_LDFLAGS) -lSDL2 -o "$@"

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

-include $(ALL_DEPS)

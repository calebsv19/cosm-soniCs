APP_NAME := daw_app
BUILD_DIR := build
SRC_DIR := src
SDLAPP_DIR := SDLApp
HOST_CC ?= cc
FISICS_CC ?= /Users/calebsv/Desktop/CodeWork/fisiCs/fisics
BUILD_TOOLCHAIN ?= clang
PACKAGE_TOOLCHAIN ?= $(BUILD_TOOLCHAIN)
TEST_TOOLCHAIN ?= clang
RELEASE_TOOLCHAIN ?= clang
PKG_CONFIG ?= pkg-config
TARGET_CONTRACT_HELPER ?= ../bin/desktop_release_target_contract.sh
HOST_ARCH := $(strip $(shell "$(TARGET_CONTRACT_HELPER)" get host_arch))
TARGET_OS_INPUT := $(TARGET_OS)
TARGET_ARCH_INPUT := $(TARGET_ARCH)
TARGET_VARIANT_INPUT := $(TARGET_VARIANT)
TARGET_OS ?= $(strip $(shell TARGET_OS="$(TARGET_OS_INPUT)" TARGET_ARCH="$(TARGET_ARCH_INPUT)" TARGET_VARIANT="$(TARGET_VARIANT_INPUT)" "$(TARGET_CONTRACT_HELPER)" get target_os))
TARGET_ARCH ?= $(strip $(shell TARGET_OS="$(TARGET_OS_INPUT)" TARGET_ARCH="$(TARGET_ARCH_INPUT)" TARGET_VARIANT="$(TARGET_VARIANT_INPUT)" "$(TARGET_CONTRACT_HELPER)" get target_arch))
TARGET_VARIANT ?= $(strip $(shell TARGET_OS="$(TARGET_OS_INPUT)" TARGET_ARCH="$(TARGET_ARCH_INPUT)" TARGET_VARIANT="$(TARGET_VARIANT_INPUT)" "$(TARGET_CONTRACT_HELPER)" get target_variant))
TARGET_TRIPLE := $(strip $(shell TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(TARGET_CONTRACT_HELPER)" get target_triple))
RELEASE_PLATFORM := $(strip $(shell TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(TARGET_CONTRACT_HELPER)" get release_platform))
RELEASE_ARCH := $(strip $(shell TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(TARGET_CONTRACT_HELPER)" get release_arch))
TARGET_HOMEBREW_PREFIX ?= $(strip $(shell TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(TARGET_CONTRACT_HELPER)" get homebrew_prefix))
TARGET_ALT_HOMEBREW_PREFIX ?= $(strip $(shell TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(TARGET_CONTRACT_HELPER)" get alt_homebrew_prefix))
TARGET_PKG_CONFIG_LIBDIR ?= $(TARGET_HOMEBREW_PREFIX)/lib/pkgconfig:$(TARGET_HOMEBREW_PREFIX)/share/pkgconfig
TARGET_DEP_SEARCH_ROOTS ?= $(TARGET_HOMEBREW_PREFIX):$(TARGET_ALT_HOMEBREW_PREFIX)
ARCH_FLAGS := -arch $(TARGET_ARCH)

ifeq ($(BUILD_TOOLCHAIN),clang)
APP_CC := $(HOST_CC)
TOOLCHAIN_DEP :=
else ifeq ($(BUILD_TOOLCHAIN),fisics)
APP_CC := $(FISICS_CC)
TOOLCHAIN_DEP := $(FISICS_CC)
else
$(error Unsupported BUILD_TOOLCHAIN '$(BUILD_TOOLCHAIN)'; expected clang or fisics)
endif

SHARED_ROOT ?= third_party/codework_shared
VK_RENDERER_DIR := $(SHARED_ROOT)/vk_renderer
TIMER_HUD_DIR := $(SHARED_ROOT)/timer_hud
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
CORE_WORKERS_DIR := $(SHARED_ROOT)/core/core_workers
CORE_WAKE_DIR := $(SHARED_ROOT)/core/core_wake
CORE_KERNEL_DIR := $(SHARED_ROOT)/core/core_kernel
CORE_TRACE_DIR := $(SHARED_ROOT)/core/core_trace
KIT_VIZ_DIR := $(SHARED_ROOT)/kit/kit_viz
KIT_RENDER_DIR := $(SHARED_ROOT)/kit/kit_render

TARGET_BUILD_ROOT := $(BUILD_DIR)/targets/$(TARGET_TRIPLE)
TOOLCHAIN_BUILD_ROOT := $(TARGET_BUILD_ROOT)/toolchains
APP_BUILD_ROOT := $(TOOLCHAIN_BUILD_ROOT)/$(BUILD_TOOLCHAIN)
APP_OBJ_DIR := $(APP_BUILD_ROOT)/obj
APP_BIN_DIR := $(APP_BUILD_ROOT)/bin
COMPILER_STAMP_DIR := $(APP_BUILD_ROOT)/compiler
COMPILER_STAMP := $(COMPILER_STAMP_DIR)/$(BUILD_TOOLCHAIN).stamp
HOST_OBJ_DIR := $(TARGET_BUILD_ROOT)/host
TEST_BUILD_ROOT := $(TARGET_BUILD_ROOT)/tests
SHARED_BUILD_DIR := $(TARGET_BUILD_ROOT)/shared

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

APP_SRCS := \
	$(SDLAPP_DIR)/sdl_app_framework.c \
	$(SRC_DIR)/core/loop/daw_mainthread_wake.c \
	$(SRC_DIR)/core/loop/daw_mainthread_timer.c \
	$(SRC_DIR)/core/loop/daw_mainthread_jobs.c \
	$(SRC_DIR)/core/loop/daw_mainthread_messages.c \
	$(SRC_DIR)/core/loop/daw_mainthread_kernel.c \
	$(SRC_DIR)/core/loop/daw_render_invalidation.c \
	$(SRC_DIR)/app/daw_app_main.c \
	$(SRC_DIR)/app/main_bounce.c \
	$(SRC_DIR)/app/main_loop_policy.c \
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
	$(SRC_DIR)/export/daw_trace_export.c \
	$(SRC_DIR)/export/daw_trace_export_async.c \
	$(SRC_DIR)/engine/audio_source.c \
	$(SRC_DIR)/engine/engine_core.c \
	$(SRC_DIR)/engine/engine_io.c \
	$(SRC_DIR)/engine/engine_fx.c \
	$(SRC_DIR)/engine/engine_transport.c \
	$(SRC_DIR)/engine/engine_tracks.c \
	$(SRC_DIR)/engine/engine_clips.c \
	$(SRC_DIR)/engine/engine_clips_automation.c \
	$(SRC_DIR)/engine/engine_clips_no_overlap.c \
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
	$(SRC_DIR)/session/session_io_read_parse_engine.c \
	$(SRC_DIR)/session/session_io_read_parse_effects_panel.c \
	$(SRC_DIR)/session/session_io_read_parse_master_fx.c \
	$(SRC_DIR)/session/session_io_read_parse_track_clips.c \
	$(SRC_DIR)/session/session_io_read_parse_track_fx.c \
	$(SRC_DIR)/session/session_apply.c \
	$(SRC_DIR)/session/project_manager.c \
	$(SRC_DIR)/undo/undo_manager.c \
	$(SRC_DIR)/undo/undo_manager_stack.c \
	$(SRC_DIR)/ui/panes.c \
	$(SRC_DIR)/ui/layout.c \
	$(SRC_DIR)/ui/overlay/layout_modal_overlays.c \
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
	$(SRC_DIR)/ui/overlay/timeline_view_overlays.c \
	$(SRC_DIR)/ui/timeline_view_clip_pass.c \
	$(SRC_DIR)/ui/timeline_view_grid.c \
	$(SRC_DIR)/ui/timeline_view_controls.c \
	$(SRC_DIR)/ui/overlay/timeline_view_runtime_overlays.c \
	$(SRC_DIR)/ui/font.c \
	$(SRC_DIR)/ui/font_bridge.c \
	$(SRC_DIR)/ui/shared_theme_font_adapter.c \
	$(SRC_DIR)/ui/text_draw.c \
	$(SRC_DIR)/ui/transport.c \
	$(SRC_DIR)/ui/clip_inspector.c \
	$(SRC_DIR)/ui/clip_inspector_controls.c \
	$(SRC_DIR)/ui/clip_inspector_waveform.c \
	$(SRC_DIR)/ui/effects_panel/panel.c \
	$(SRC_DIR)/ui/effects_panel/sync.c \
	$(SRC_DIR)/ui/effects_panel/overlay.c \
	$(SRC_DIR)/ui/effects_panel/state_helpers.c \
	$(SRC_DIR)/ui/effects_panel/slot_view.c \
	$(SRC_DIR)/ui/effects_panel/slot_preview.c \
	$(SRC_DIR)/ui/effects_panel/slot_preview_delay.c \
	$(SRC_DIR)/ui/effects_panel/slot_preview_time_domain.c \
	$(SRC_DIR)/ui/effects_panel/slot_preview_eq_curve.c \
	$(SRC_DIR)/ui/effects_panel/slot_layout.c \
	$(SRC_DIR)/ui/effects_panel/slot_widgets.c \
	$(SRC_DIR)/ui/effects_panel/spec_panel.c \
	$(SRC_DIR)/ui/effects_panel/spec_panel_render.c \
	$(SRC_DIR)/ui/effects_panel/list_view.c \
	$(SRC_DIR)/ui/effects_panel/eq_detail_view.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_view.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_history_grid.c \
	$(SRC_DIR)/ui/effects_panel/meter_detail_history_cache.c \
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
	$(SRC_DIR)/input/timeline/timeline_input_mouse_clip_press.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse_tempo_overlay.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse_drag.c \
	$(SRC_DIR)/input/timeline/timeline_input_mouse_scroll.c \
	$(SRC_DIR)/input/timeline/timeline_snap.c \
	$(SRC_DIR)/input/timeline/timeline_selection.c \
	$(SRC_DIR)/input/timeline/timeline_drag.c \
	$(SRC_DIR)/input/automation_input.c \
	$(SRC_DIR)/input/tempo_overlay_input.c \
	$(SRC_DIR)/input/inspector_input.c \
	$(SRC_DIR)/input/inspector_input_numeric_edit.c \
	$(SRC_DIR)/input/inspector_fade_input.c \
	$(SRC_DIR)/input/transport_input.c \
	$(SRC_DIR)/input/effects_panel_input.c \
	$(SRC_DIR)/input/effects_panel_input_helpers.c \
	$(SRC_DIR)/input/effects_panel_eq_detail_input.c \
	$(SRC_DIR)/input/effects_panel_track_snapshot.c \
	$(SRC_DIR)/render/adapters/timer_hud_adapter.c \
	$(EFFECTS_SRCS)

TIMER_HUD_SRCS := $(shell find $(TIMER_HUD_DIR)/src -type f -name '*.c')
TIMER_HUD_EXTERNAL_SRCS := $(TIMER_HUD_DIR)/external/cJSON.c
TIMER_HUD_SUPPORT_SRCS := $(TIMER_HUD_SRCS) $(TIMER_HUD_EXTERNAL_SRCS)

APP_OBJS := $(patsubst %.c,$(APP_OBJ_DIR)/%.o,$(APP_SRCS))
APP_OBJS_QUOTED := $(foreach obj,$(APP_OBJS),"$(obj)")
TIMER_HUD_OBJS := $(patsubst $(TIMER_HUD_DIR)/%.c,$(HOST_OBJ_DIR)/timer_hud/%.o,$(TIMER_HUD_SUPPORT_SRCS))
TIMER_HUD_OBJS_QUOTED := $(foreach obj,$(TIMER_HUD_OBJS),"$(obj)")

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
	VULKAN_CFLAGS := $(shell env PKG_CONFIG_LIBDIR="$(TARGET_PKG_CONFIG_LIBDIR)" $(PKG_CONFIG) --cflags vulkan 2>/dev/null)
	VULKAN_LIBS := $(shell env PKG_CONFIG_LIBDIR="$(TARGET_PKG_CONFIG_LIBDIR)" $(PKG_CONFIG) --libs vulkan 2>/dev/null)
	ifeq ($(strip $(VULKAN_CFLAGS)$(VULKAN_LIBS)),)
		ifneq ($(wildcard $(TARGET_HOMEBREW_PREFIX)/include/vulkan/vulkan.h),)
			VULKAN_CFLAGS := -I$(TARGET_HOMEBREW_PREFIX)/include
			VULKAN_LIBS := -L$(TARGET_HOMEBREW_PREFIX)/lib -lvulkan
		else ifneq ($(wildcard $(TARGET_ALT_HOMEBREW_PREFIX)/include/vulkan/vulkan.h),)
			VULKAN_CFLAGS := -I$(TARGET_ALT_HOMEBREW_PREFIX)/include
			VULKAN_LIBS := -L$(TARGET_ALT_HOMEBREW_PREFIX)/lib -lvulkan
		else
			VULKAN_CFLAGS := -I/usr/include
			VULKAN_LIBS := -lvulkan
		endif
	endif

	ifneq ($(wildcard $(TARGET_HOMEBREW_PREFIX)/include/SDL2/SDL.h),)
		SDL2_CFLAGS += -I$(TARGET_HOMEBREW_PREFIX)/include -D_THREAD_SAFE
		SDL2_LDFLAGS += -L$(TARGET_HOMEBREW_PREFIX)/lib
	endif
	ifneq ($(wildcard $(TARGET_ALT_HOMEBREW_PREFIX)/include/SDL2/SDL.h),)
		SDL2_CFLAGS += -I$(TARGET_ALT_HOMEBREW_PREFIX)/include -D_THREAD_SAFE
		SDL2_LDFLAGS += -L$(TARGET_ALT_HOMEBREW_PREFIX)/lib
	endif

	ifeq ($(strip $(SDL2_CFLAGS)),)
		ifneq ($(wildcard /Library/Frameworks/SDL2.framework/Headers/SDL.h),)
			SDL2_CFLAGS += -F/Library/Frameworks -I/Library/Frameworks/SDL2.framework/Headers -D_THREAD_SAFE
			SDL2_LDFLAGS += -F/Library/Frameworks
			SDL2_LIBS :=
			SDL2_FRAMEWORKS := -framework SDL2
		endif
	endif

	ifeq ($(strip $(SDL2_CFLAGS)),)
		ifneq ($(SDL_CONFIG),)
			SDL2_CFLAGS += $(shell $(SDL_CONFIG) --cflags)
			SDL2_LDFLAGS += $(shell $(SDL_CONFIG) --libs)
		endif
	endif
else
	VULKAN_CFLAGS := $(shell $(PKG_CONFIG) --cflags vulkan 2>/dev/null)
	VULKAN_LIBS := $(shell $(PKG_CONFIG) --libs vulkan 2>/dev/null)
	ifeq ($(strip $(VULKAN_CFLAGS)$(VULKAN_LIBS)),)
		VULKAN_CFLAGS := -I/usr/include
		VULKAN_LIBS := -lvulkan
	endif

	ifneq ($(SDL_CONFIG),)
		SDL2_CFLAGS += $(shell $(SDL_CONFIG) --cflags)
		SDL2_LDFLAGS += $(shell $(SDL_CONFIG) --libs)
	else
		SDL_PKGCONFIG := $(shell $(PKG_CONFIG) --exists sdl2 >/dev/null 2>&1 && echo yes)
		ifeq ($(SDL_PKGCONFIG),yes)
			SDL2_CFLAGS += $(shell $(PKG_CONFIG) --cflags sdl2)
			SDL2_LDFLAGS += $(shell $(PKG_CONFIG) --libs sdl2)
		else
			SDL2_CFLAGS += -I/usr/include/SDL2
			SDL2_LDFLAGS += -L/usr/lib
		endif
	endif
endif

CPPFLAGS := -Iinclude -Iextern -I$(SDLAPP_DIR) -I$(VK_RENDERER_DIR)/include -I$(TIMER_HUD_DIR)/include -I$(TIMER_HUD_DIR)/external -I$(CORE_BASE_DIR)/include -I$(CORE_IO_DIR)/include -I$(CORE_DATA_DIR)/include -I$(CORE_PACK_DIR)/include -I$(CORE_TIME_DIR)/include -I$(CORE_THEME_DIR)/include -I$(CORE_FONT_DIR)/include -I$(CORE_QUEUE_DIR)/include -I$(CORE_SCHED_DIR)/include -I$(CORE_JOBS_DIR)/include -I$(CORE_WORKERS_DIR)/include -I$(CORE_WAKE_DIR)/include -I$(CORE_KERNEL_DIR)/include -I$(CORE_TRACE_DIR)/include -I$(KIT_VIZ_DIR)/include -I$(KIT_RENDER_DIR)/include $(SDL2_CFLAGS) $(VULKAN_CFLAGS) -DVK_RENDERER_SHADER_ROOT=\"$(abspath $(VK_RENDERER_DIR))\" -include $(VK_RENDERER_DIR)/include/vk_renderer_sdl.h

LDFLAGS := $(ARCH_FLAGS) $(SDL2_LDFLAGS) $(SDL2_LIBS) $(SDL2_FRAMEWORKS) $(VULKAN_LIBS)
ifeq ($(UNAME_S),Darwin)
	CFLAGS += -DVK_USE_PLATFORM_METAL_EXT
	LDFLAGS += -framework AudioToolbox -framework CoreFoundation -framework Metal -framework QuartzCore -framework Cocoa -framework IOKit -framework CoreVideo
endif

CORE_BASE_LIB_SRC := $(CORE_BASE_DIR)/build/libcore_base.a
CORE_IO_LIB_SRC := $(CORE_IO_DIR)/build/libcore_io.a
CORE_DATA_LIB_SRC := $(CORE_DATA_DIR)/build/libcore_data.a
CORE_PACK_LIB_SRC := $(CORE_PACK_DIR)/build/libcore_pack.a
CORE_TIME_LIB_SRC := $(CORE_TIME_DIR)/build/libcore_time.a
CORE_THEME_LIB_SRC := $(CORE_THEME_DIR)/build/libcore_theme.a
CORE_FONT_LIB_SRC := $(CORE_FONT_DIR)/build/libcore_font.a
CORE_QUEUE_LIB_SRC := $(CORE_QUEUE_DIR)/build/libcore_queue.a
CORE_SCHED_LIB_SRC := $(CORE_SCHED_DIR)/build/libcore_sched.a
CORE_JOBS_LIB_SRC := $(CORE_JOBS_DIR)/build/libcore_jobs.a
CORE_WORKERS_LIB_SRC := $(CORE_WORKERS_DIR)/build/libcore_workers.a
CORE_WAKE_LIB_SRC := $(CORE_WAKE_DIR)/build/libcore_wake.a
CORE_KERNEL_LIB_SRC := $(CORE_KERNEL_DIR)/build/libcore_kernel.a
CORE_TRACE_LIB_SRC := $(CORE_TRACE_DIR)/build/libcore_trace.a
KIT_VIZ_LIB_SRC := $(KIT_VIZ_DIR)/build/libkit_viz.a
KIT_RENDER_LIB_SRC := $(KIT_RENDER_DIR)/build/vk/libkit_render.a
VK_RENDERER_LIB_SRC := $(VK_RENDERER_DIR)/build/lib/libvkrenderer.a

CORE_BASE_LIB := $(SHARED_BUILD_DIR)/libcore_base.a
CORE_IO_LIB := $(SHARED_BUILD_DIR)/libcore_io.a
CORE_DATA_LIB := $(SHARED_BUILD_DIR)/libcore_data.a
CORE_PACK_LIB := $(SHARED_BUILD_DIR)/libcore_pack.a
CORE_TIME_LIB := $(SHARED_BUILD_DIR)/libcore_time.a
CORE_THEME_LIB := $(SHARED_BUILD_DIR)/libcore_theme.a
CORE_FONT_LIB := $(SHARED_BUILD_DIR)/libcore_font.a
CORE_QUEUE_LIB := $(SHARED_BUILD_DIR)/libcore_queue.a
CORE_SCHED_LIB := $(SHARED_BUILD_DIR)/libcore_sched.a
CORE_JOBS_LIB := $(SHARED_BUILD_DIR)/libcore_jobs.a
CORE_WORKERS_LIB := $(SHARED_BUILD_DIR)/libcore_workers.a
CORE_WAKE_LIB := $(SHARED_BUILD_DIR)/libcore_wake.a
CORE_KERNEL_LIB := $(SHARED_BUILD_DIR)/libcore_kernel.a
CORE_TRACE_LIB := $(SHARED_BUILD_DIR)/libcore_trace.a
KIT_VIZ_LIB := $(SHARED_BUILD_DIR)/libkit_viz.a
KIT_RENDER_LIB := $(SHARED_BUILD_DIR)/libkit_render.a
VK_RENDERER_LIB := $(SHARED_BUILD_DIR)/libvkrenderer.a

APP_SHARED_LIBS := \
	$(CORE_TRACE_LIB) \
	$(CORE_KERNEL_LIB) \
	$(CORE_WAKE_LIB) \
	$(CORE_WORKERS_LIB) \
	$(CORE_JOBS_LIB) \
	$(CORE_SCHED_LIB) \
	$(CORE_QUEUE_LIB) \
	$(CORE_TIME_LIB) \
	$(CORE_PACK_LIB) \
	$(CORE_IO_LIB) \
	$(CORE_DATA_LIB) \
	$(CORE_THEME_LIB) \
	$(CORE_FONT_LIB) \
	$(CORE_BASE_LIB) \
	$(KIT_VIZ_LIB) \
	$(KIT_RENDER_LIB) \
	$(VK_RENDERER_LIB)

APP_BIN := $(APP_BIN_DIR)/$(APP_NAME)
PACKAGE_BIN := $(TARGET_BUILD_ROOT)/toolchains/$(PACKAGE_TOOLCHAIN)/bin/$(APP_NAME)
DIST_DIR := $(TARGET_BUILD_ROOT)/dist
PACKAGE_APP_NAME := soniCs.app
PACKAGE_APP_DIR := $(DIST_DIR)/$(PACKAGE_APP_NAME)
PACKAGE_CONTENTS_DIR := $(PACKAGE_APP_DIR)/Contents
PACKAGE_MACOS_DIR := $(PACKAGE_CONTENTS_DIR)/MacOS
PACKAGE_RESOURCES_DIR := $(PACKAGE_CONTENTS_DIR)/Resources
PACKAGE_FRAMEWORKS_DIR := $(PACKAGE_CONTENTS_DIR)/Frameworks
PACKAGE_INFO_PLIST_SRC := tools/packaging/macos/Info.plist
PACKAGE_LAUNCHER_SRC := tools/packaging/macos/daw-launcher
PACKAGE_DYLIB_BUNDLER := tools/packaging/macos/bundle-dylibs.sh
PACKAGE_APP_ICON_NAME := AppIcon
PACKAGE_APP_ICON_FILE := $(PACKAGE_APP_ICON_NAME).icns
PACKAGE_LOCAL_ICON_DIR := tools/packaging/macos/local_app_icon
PACKAGE_APP_ICON_SRC ?= $(PACKAGE_LOCAL_ICON_DIR)/$(PACKAGE_APP_ICON_FILE)
PACKAGE_APP_ICONSET_SRC ?= $(PACKAGE_LOCAL_ICON_DIR)/$(PACKAGE_APP_ICON_NAME).iconset
PACKAGE_BUNDLED_ICON_PATH := $(PACKAGE_RESOURCES_DIR)/$(PACKAGE_APP_ICON_FILE)
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
RELEASE_ARTIFACT_BASENAME := $(RELEASE_PRODUCT_NAME)-$(RELEASE_VERSION)-$(RELEASE_PLATFORM)-$(RELEASE_ARCH)-$(RELEASE_CHANNEL)
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

TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(TEST_SRCS))
TEST_BIN := $(TEST_BUILD_ROOT)/session_serialization_test

CACHE_TEST_SRCS := \
	tests/media_cache_stress_test.c

CACHE_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(CACHE_TEST_SRCS))
CACHE_TEST_BIN := $(TEST_BUILD_ROOT)/media_cache_stress_test

OVERLAP_TEST_SRCS := \
	tests/clip_overlap_priority_test.c

OVERLAP_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(OVERLAP_TEST_SRCS))
OVERLAP_TEST_BIN := $(TEST_BUILD_ROOT)/clip_overlap_priority_test

SMOKE_TEST_SRCS := \
	tests/engine_smoke_test.c

SMOKE_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(SMOKE_TEST_SRCS))
SMOKE_TEST_BIN := $(TEST_BUILD_ROOT)/engine_smoke_test

KITVIZ_ADAPTER_TEST_SRCS := \
	tests/kit_viz_waveform_adapter_test.c

KITVIZ_ADAPTER_TEST_BIN := $(TEST_BUILD_ROOT)/kit_viz_waveform_adapter_test

WAVEFORM_PACK_WARMSTART_TEST_SRCS := \
	tests/waveform_cache_pack_warmstart_test.c

WAVEFORM_PACK_WARMSTART_TEST_BIN := $(TEST_BUILD_ROOT)/waveform_cache_pack_warmstart_test

PACK_CONTRACT_TEST_SRCS := \
	tests/daw_pack_contract_parity_test.c

PACK_CONTRACT_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(PACK_CONTRACT_TEST_SRCS))
PACK_CONTRACT_TEST_BIN := $(TEST_BUILD_ROOT)/daw_pack_contract_parity_test

TRACE_CONTRACT_TEST_SRCS := \
	tests/daw_trace_export_contract_test.c

TRACE_CONTRACT_TEST_BIN := $(TEST_BUILD_ROOT)/daw_trace_export_contract_test

TRACE_ASYNC_CONTRACT_TEST_SRCS := \
	tests/daw_trace_export_async_contract_test.c

TRACE_ASYNC_CONTRACT_TEST_BIN := $(TEST_BUILD_ROOT)/daw_trace_export_async_contract_test

KITVIZ_FX_PREVIEW_ADAPTER_TEST_SRCS := \
	tests/kit_viz_fx_preview_adapter_test.c

KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN := $(TEST_BUILD_ROOT)/kit_viz_fx_preview_adapter_test

KITVIZ_METER_ADAPTER_TEST_SRCS := \
	tests/kit_viz_meter_adapter_test.c

KITVIZ_METER_ADAPTER_TEST_BIN := $(TEST_BUILD_ROOT)/kit_viz_meter_adapter_test

SHARED_THEME_FONT_ADAPTER_TEST_SRCS := \
	tests/shared_theme_font_adapter_test.c

SHARED_THEME_FONT_ADAPTER_TEST_BIN := $(TEST_BUILD_ROOT)/shared_theme_font_adapter_test

LAYOUT_SWEEP_TEST_SRCS := \
	tests/layout_text_scaling_sweep_test.c

LAYOUT_SWEEP_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(LAYOUT_SWEEP_TEST_SRCS))
LAYOUT_SWEEP_TEST_BIN := $(TEST_BUILD_ROOT)/layout_text_scaling_sweep_test

DATA_PATH_CONTRACT_TEST_SRCS := \
	tests/daw_data_path_contract_test.c

DATA_PATH_CONTRACT_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(DATA_PATH_CONTRACT_TEST_SRCS))
DATA_PATH_CONTRACT_TEST_BIN := $(TEST_BUILD_ROOT)/daw_data_path_contract_test

# Keep legacy app tests wired to the full non-main app object set so link coverage
# stays in lock-step with engine/runtime refactors.
APP_OBJS_NO_MAIN := $(filter-out $(APP_OBJ_DIR)/src/app/main.o,$(APP_OBJS))
ENGINE_TEST_SUPPORT_OBJS = $(APP_OBJS_NO_MAIN) $(TIMER_HUD_OBJS)

APP_DEPS := $(APP_OBJS:.o=.d)
TIMER_HUD_DEPS := $(TIMER_HUD_OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)
CACHE_TEST_DEPS := $(CACHE_TEST_OBJS:.o=.d)
OVERLAP_TEST_DEPS := $(OVERLAP_TEST_OBJS:.o=.d)
SMOKE_TEST_DEPS := $(SMOKE_TEST_OBJS:.o=.d)
PACK_CONTRACT_TEST_DEPS := $(PACK_CONTRACT_TEST_OBJS:.o=.d)
LAYOUT_SWEEP_TEST_DEPS := $(LAYOUT_SWEEP_TEST_OBJS:.o=.d)
DATA_PATH_CONTRACT_TEST_DEPS := $(DATA_PATH_CONTRACT_TEST_OBJS:.o=.d)
ENGINE_TEST_SUPPORT_DEPS := $(ENGINE_TEST_SUPPORT_OBJS:.o=.d)
ALL_DEPS := $(APP_DEPS) $(TIMER_HUD_DEPS) $(TEST_DEPS) $(CACHE_TEST_DEPS) $(OVERLAP_TEST_DEPS) $(SMOKE_TEST_DEPS) $(PACK_CONTRACT_TEST_DEPS) $(LAYOUT_SWEEP_TEST_DEPS) $(DATA_PATH_CONTRACT_TEST_DEPS) $(ENGINE_TEST_SUPPORT_DEPS)

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

.PHONY: all clean run run-ide-theme run-headless-smoke visual-harness package-build-lane package-desktop package-desktop-smoke package-desktop-self-test package-desktop-copy-desktop package-desktop-sync package-desktop-open package-desktop-remove package-desktop-refresh release-contract release-clean release-build release-build-internal release-bundle-audit release-bundle-audit-internal release-sign release-sign-internal release-verify release-verify-internal release-verify-signed release-verify-signed-internal release-notarize release-notarize-internal release-staple release-staple-internal release-verify-notarized release-verify-notarized-internal release-artifact release-artifact-internal release-distribute release-distribute-internal release-desktop-refresh release-desktop-refresh-internal loop-gates loop-gates-strict test test-stable test-legacy test-session test-cache test-overlap test-smoke test-kitviz-adapter test-waveform-pack-warmstart test-pack-contract test-trace-contract test-trace-async-contract test-kitviz-fx-preview-adapter test-kitviz-meter-adapter test-shared-theme-font-adapter test-layout-sweep test-data-path-contract test-library-copy-vs-reference-contract

FORCE:

all: $(APP_BIN)

$(APP_BIN_DIR) $(COMPILER_STAMP_DIR):
	@mkdir -p "$@"

$(COMPILER_STAMP): $(TOOLCHAIN_DEP) | $(COMPILER_STAMP_DIR)
	@printf '%s\n' "$(APP_CC)" > "$@"

SHARED_CC := $(HOST_CC) $(ARCH_FLAGS)

$(SHARED_BUILD_DIR):
	@mkdir -p "$@"

define build_copy_static_lib
$($(1)_LIB): FORCE | $(SHARED_BUILD_DIR)
	$$(MAKE) -C $($(1)_DIR) clean $(2)
	PKG_CONFIG_LIBDIR="$(TARGET_PKG_CONFIG_LIBDIR)" PKG_CONFIG="$(PKG_CONFIG)" $$(MAKE) -C $($(1)_DIR) CC="$$(SHARED_CC)" $(2)
	cp "$$($(1)_LIB_SRC)" "$$@"
endef

$(eval $(call build_copy_static_lib,CORE_BASE,))
$(eval $(call build_copy_static_lib,CORE_IO,))
$(eval $(call build_copy_static_lib,CORE_DATA,))
$(eval $(call build_copy_static_lib,CORE_PACK,))
$(eval $(call build_copy_static_lib,CORE_TIME,))
$(eval $(call build_copy_static_lib,CORE_THEME,))
$(eval $(call build_copy_static_lib,CORE_FONT,))
$(eval $(call build_copy_static_lib,CORE_QUEUE,))
$(eval $(call build_copy_static_lib,CORE_SCHED,))
$(eval $(call build_copy_static_lib,CORE_JOBS,))
$(eval $(call build_copy_static_lib,CORE_WORKERS,))
$(eval $(call build_copy_static_lib,CORE_WAKE,))
$(eval $(call build_copy_static_lib,CORE_KERNEL,))
$(eval $(call build_copy_static_lib,CORE_TRACE,))
$(eval $(call build_copy_static_lib,KIT_VIZ,))
$(eval $(call build_copy_static_lib,KIT_RENDER,KIT_RENDER_ENABLE_VK=1))
$(eval $(call build_copy_static_lib,VK_RENDERER,))

$(APP_BIN): $(APP_OBJS) $(TIMER_HUD_OBJS) $(APP_SHARED_LIBS) | $(APP_BIN_DIR)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(ARCH_FLAGS) $(APP_OBJS_QUOTED) $(TIMER_HUD_OBJS_QUOTED) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

$(APP_OBJ_DIR)/%.o: %.c $(COMPILER_STAMP)
	@mkdir -p "$(dir $@)"
	$(APP_CC) $(CPPFLAGS) $(CFLAGS) $(if $(filter clang,$(BUILD_TOOLCHAIN)),$(ARCH_FLAGS),) -c "$<" -o "$@"

$(HOST_OBJ_DIR)/timer_hud/%.o: $(TIMER_HUD_DIR)/%.c
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -c "$<" -o "$@"

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

package-build-lane:
	@$(MAKE) BUILD_TOOLCHAIN="$(PACKAGE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(PACKAGE_BIN)"

package-desktop: package-build-lane
	@echo "Preparing desktop package..."
	@rm -rf "$(PACKAGE_APP_DIR)"
	@mkdir -p "$(PACKAGE_MACOS_DIR)" "$(PACKAGE_RESOURCES_DIR)" "$(PACKAGE_FRAMEWORKS_DIR)"
	@cp "$(PACKAGE_INFO_PLIST_SRC)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@cp "$(PACKAGE_BIN)" "$(PACKAGE_MACOS_DIR)/daw-bin"
	@cp "$(PACKAGE_LAUNCHER_SRC)" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@chmod +x "$(PACKAGE_MACOS_DIR)/daw-bin" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@if [ -f "$(PACKAGE_APP_ICON_SRC)" ]; then \
		cp "$(PACKAGE_APP_ICON_SRC)" "$(PACKAGE_BUNDLED_ICON_PATH)"; \
		echo "Bundled app icon from $(PACKAGE_APP_ICON_SRC)"; \
	elif [ -d "$(PACKAGE_APP_ICONSET_SRC)" ]; then \
		/usr/bin/iconutil -c icns -o "$(PACKAGE_BUNDLED_ICON_PATH)" "$(PACKAGE_APP_ICONSET_SRC)" || exit 1; \
		echo "Bundled app icon from $(PACKAGE_APP_ICONSET_SRC)"; \
	else \
		echo "warning: no app icon source found at $(PACKAGE_APP_ICON_SRC) or $(PACKAGE_APP_ICONSET_SRC)"; \
	fi
	@PACKAGE_DEP_SEARCH_ROOTS="$(TARGET_DEP_SEARCH_ROOTS)" "$(PACKAGE_DYLIB_BUNDLER)" "$(PACKAGE_MACOS_DIR)/daw-bin" "$(PACKAGE_FRAMEWORKS_DIR)"
	@mkdir -p "$(PACKAGE_RESOURCES_DIR)/config" "$(PACKAGE_RESOURCES_DIR)/assets" "$(PACKAGE_RESOURCES_DIR)/include" "$(PACKAGE_RESOURCES_DIR)/shared/assets" "$(PACKAGE_RESOURCES_DIR)/vk_renderer" "$(PACKAGE_RESOURCES_DIR)/shaders"
	@cp -R config/. "$(PACKAGE_RESOURCES_DIR)/config/"
	@cp -R assets/audio "$(PACKAGE_RESOURCES_DIR)/assets/"
	@cp -R include/fonts "$(PACKAGE_RESOURCES_DIR)/include/"
	@cp -R "$(SHARED_ROOT)/assets/fonts" "$(PACKAGE_RESOURCES_DIR)/shared/assets/"
	@cp -R "$(VK_RENDERER_DIR)/shaders" "$(PACKAGE_RESOURCES_DIR)/vk_renderer/"
	@cp -R "$(VK_RENDERER_DIR)/shaders/." "$(PACKAGE_RESOURCES_DIR)/shaders/"
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$$dylib"; \
	done
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-bin"
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$(PACKAGE_APP_DIR)"
	@codesign --verify --deep --strict "$(PACKAGE_APP_DIR)"
	@echo "Desktop package ready: $(PACKAGE_APP_DIR)"

package-desktop-smoke: package-desktop
	@test -x "$(PACKAGE_MACOS_DIR)/daw-launcher" || (echo "Missing launcher"; exit 1)
	@test -x "$(PACKAGE_MACOS_DIR)/daw-bin" || (echo "Missing daw-bin"; exit 1)
	@test -f "$(PACKAGE_CONTENTS_DIR)/Info.plist" || (echo "Missing Info.plist"; exit 1)
	@test -f "$(PACKAGE_FRAMEWORKS_DIR)/libvulkan.1.dylib" || (echo "Missing bundled libvulkan"; exit 1)
	@test -f "$(PACKAGE_FRAMEWORKS_DIR)/libMoltenVK.dylib" || (echo "Missing bundled libMoltenVK"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/config/engine.cfg" || (echo "Missing config/engine.cfg"; exit 1)
	@if [ -f "$(PACKAGE_APP_ICON_SRC)" ] || [ -d "$(PACKAGE_APP_ICONSET_SRC)" ]; then \
		test -f "$(PACKAGE_BUNDLED_ICON_PATH)" || (echo "Missing bundled AppIcon.icns"; exit 1); \
	fi
	@test -f "$(PACKAGE_RESOURCES_DIR)/assets/audio/README.md" || (echo "Missing bundled audio README"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/include/fonts/Montserrat/Montserrat-Regular.ttf" || (echo "Missing bundled Montserrat"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/vk_renderer/shaders/textured.vert.spv" || (echo "Missing bundled vk shaders"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/shaders/textured.vert.spv" || (echo "Missing bundled runtime shader"; exit 1)
	@actual_archs="$$(/usr/bin/lipo -archs "$(PACKAGE_MACOS_DIR)/daw-bin" 2>/dev/null || true)"; \
	printf '%s\n' "$$actual_archs" | /usr/bin/grep -qw "$(TARGET_ARCH)" || (echo "Unexpected app binary archs: $$actual_archs"; exit 1)
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		dylib_archs="$$(/usr/bin/lipo -archs "$$dylib" 2>/dev/null || true)"; \
		printf '%s\n' "$$dylib_archs" | /usr/bin/grep -qw "$(TARGET_ARCH)" || (echo "Unexpected dylib archs for $$dylib: $$dylib_archs"; exit 1); \
	done
	@echo "package-desktop-smoke passed."

package-desktop-self-test: package-desktop-smoke
	@"$(PACKAGE_MACOS_DIR)/daw-launcher" --self-test || (echo "package-desktop self-test failed."; exit 1)
	@echo "package-desktop-self-test passed."

package-desktop-copy-desktop: package-desktop
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@ditto "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
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
	@ditto "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@echo "Refreshed $(PACKAGE_APP_NAME) at $(DESKTOP_APP_DIR)"

release-contract:
	@echo "HOST_ARCH=$(HOST_ARCH)"
	@echo "TARGET_OS=$(TARGET_OS)"
	@echo "TARGET_ARCH=$(TARGET_ARCH)"
	@echo "TARGET_VARIANT=$(TARGET_VARIANT)"
	@echo "TARGET_TRIPLE=$(TARGET_TRIPLE)"
	@echo "RELEASE_PLATFORM=$(RELEASE_PLATFORM)"
	@echo "RELEASE_ARCH=$(RELEASE_ARCH)"
	@echo "TARGET_HOMEBREW_PREFIX=$(TARGET_HOMEBREW_PREFIX)"
	@echo "TARGET_PKG_CONFIG_LIBDIR=$(TARGET_PKG_CONFIG_LIBDIR)"
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
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-build-internal

release-build-internal:
	@$(MAKE) package-desktop-self-test
	@echo "release-build complete."

release-bundle-audit:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-bundle-audit-internal

release-bundle-audit-internal: release-build-internal
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

release-sign:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-sign-internal

release-sign-internal: release-bundle-audit-internal
	@test -n "$(RELEASE_CODESIGN_IDENTITY)" || (echo "Missing signing identity"; exit 1)
	@echo "Signing with identity: $(RELEASE_CODESIGN_IDENTITY)"
	@if [ "$(RELEASE_CODESIGN_IDENTITY)" = "-" ]; then \
		for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
			[ -f "$$dylib" ] || continue; \
			codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$$dylib"; \
		done; \
		codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-bin"; \
		codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-launcher"; \
		codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$(PACKAGE_APP_DIR)"; \
	else \
		for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
			[ -f "$$dylib" ] || continue; \
			codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$$dylib"; \
		done; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-bin"; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-launcher"; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_APP_DIR)"; \
	fi
	@echo "release-sign complete."

release-verify:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-verify-internal

release-verify-internal: release-sign-internal
	@codesign --verify --deep --strict "$(PACKAGE_APP_DIR)"
	@if [ "$(RELEASE_CODESIGN_IDENTITY)" = "-" ]; then \
		echo "release-verify note: ad-hoc identity in use; skipping spctl Gatekeeper assessment"; \
	else \
		set +e; spctl_out="$$(spctl --assess --type execute --verbose=2 "$(PACKAGE_APP_DIR)" 2>&1)"; spctl_rc=$$?; set -e; \
		echo "$$spctl_out"; \
		if [ $$spctl_rc -eq 0 ]; then \
			:; \
		elif printf '%s' "$$spctl_out" | rg -q 'source=Unnotarized Developer ID'; then \
			echo "release-verify passed (pre-notary signed state)."; \
		else \
			echo "release-verify failed."; \
			exit $$spctl_rc; \
		fi; \
	fi
	@echo "release-verify passed."

release-verify-signed:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-verify-signed-internal

release-verify-signed-internal: release-verify-internal
	@echo "release-verify-signed passed."

release-notarize:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-notarize-internal

release-notarize-internal: release-sign-internal
	@test -n "$(APPLE_NOTARY_PROFILE)" || (echo "Missing APPLE_NOTARY_PROFILE"; exit 1)
	@mkdir -p "$(RELEASE_DIR)"
	@ditto -c -k --keepParent "$(PACKAGE_APP_DIR)" "$(RELEASE_APP_ZIP)"
	@xcrun notarytool submit "$(RELEASE_APP_ZIP)" --keychain-profile "$(APPLE_NOTARY_PROFILE)" --wait --output-format json > "$(RELEASE_DIR)/notary_submit.json"
	@rg -q '"status"[[:space:]]*:[[:space:]]*"Accepted"' "$(RELEASE_DIR)/notary_submit.json" || (cat "$(RELEASE_DIR)/notary_submit.json" && echo "Notary submission was not accepted" && exit 1)
	@echo "release-notarize passed."

release-staple:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-staple-internal

release-staple-internal:
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

release-verify-notarized:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-verify-notarized-internal

release-verify-notarized-internal: release-staple-internal
	@spctl --assess --type execute --verbose=2 "$(PACKAGE_APP_DIR)"
	@xcrun stapler validate "$(PACKAGE_APP_DIR)"
	@echo "release-verify-notarized passed."

release-artifact:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-artifact-internal

release-artifact-internal: release-verify-internal
	@mkdir -p "$(RELEASE_DIR)"
	@ditto -c -k --keepParent "$(PACKAGE_APP_DIR)" "$(RELEASE_APP_ZIP)"
	@shasum -a 256 "$(RELEASE_APP_ZIP)" > "$(RELEASE_APP_ZIP).sha256"
	@{ \
		echo "product=$(RELEASE_PRODUCT_NAME)"; \
		echo "program=$(RELEASE_PROGRAM_KEY)"; \
		echo "host_arch=$(HOST_ARCH)"; \
		echo "target_os=$(TARGET_OS)"; \
		echo "target_arch=$(TARGET_ARCH)"; \
		echo "target_variant=$(TARGET_VARIANT)"; \
		echo "target_triple=$(TARGET_TRIPLE)"; \
		echo "release_platform=$(RELEASE_PLATFORM)"; \
		echo "release_arch=$(RELEASE_ARCH)"; \
		echo "version=$(RELEASE_VERSION)"; \
		echo "channel=$(RELEASE_CHANNEL)"; \
		echo "bundle_id=$(RELEASE_BUNDLE_ID)"; \
		echo "zip=$(RELEASE_APP_ZIP)"; \
		echo "sha256=$$(cut -d' ' -f1 "$(RELEASE_APP_ZIP).sha256")"; \
	} > "$(RELEASE_MANIFEST)"
	@echo "release-artifact complete: $(RELEASE_APP_ZIP)"

release-distribute:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-distribute-internal

release-distribute-internal: release-artifact-internal
	@echo "release-distribute passed."

release-desktop-refresh:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-desktop-refresh-internal

release-desktop-refresh-internal: release-distribute-internal
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@cp -R "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@spctl --assess --type execute --verbose=2 "$(DESKTOP_APP_DIR)"
	@echo "release-desktop-refresh passed."

loop-gates: $(APP_BIN)
	RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

loop-gates-strict: $(APP_BIN)
	PROFILE=strict STRICT=1 RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

test:
	@$(MAKE) BUILD_TOOLCHAIN="$(TEST_TOOLCHAIN)" run-headless-smoke
	@$(MAKE) BUILD_TOOLCHAIN="$(TEST_TOOLCHAIN)" test-stable

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
	$(APP_OBJ_DIR)/src/session/session_document.o \
	$(APP_OBJ_DIR)/src/session/session_validation.o \
	$(APP_OBJ_DIR)/src/session/session_io_write.o \
	$(APP_OBJ_DIR)/src/session/session_io_read.o \
	$(APP_OBJ_DIR)/src/session/session_io_json.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_engine.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_effects_panel.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_master_fx.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_track_clips.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_track_fx.o \
	$(APP_OBJ_DIR)/src/session/session_apply.o \
	$(APP_OBJ_DIR)/src/config/config.o \
	$(CORE_PACK_LIB) \
	$(CORE_IO_LIB) \
	$(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-cache: $(CACHE_TEST_BIN)
	$(CACHE_TEST_BIN)

$(CACHE_TEST_BIN): $(CACHE_TEST_OBJS) $(APP_OBJ_DIR)/src/audio/media_cache.o $(APP_OBJ_DIR)/src/audio/media_clip.o
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-overlap: $(OVERLAP_TEST_BIN)
	$(OVERLAP_TEST_BIN)

$(OVERLAP_TEST_BIN): $(OVERLAP_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(OVERLAP_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-smoke: $(SMOKE_TEST_BIN)
	$(SMOKE_TEST_BIN)

$(SMOKE_TEST_BIN): $(SMOKE_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(SMOKE_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-kitviz-adapter: $(KITVIZ_ADAPTER_TEST_BIN)
	$(KITVIZ_ADAPTER_TEST_BIN)

$(KITVIZ_ADAPTER_TEST_BIN): $(KITVIZ_ADAPTER_TEST_SRCS) src/ui/kit_viz_waveform_adapter.c src/ui/timeline_waveform.c $(KIT_VIZ_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_data/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/kit_viz_waveform_adapter_test.c src/ui/kit_viz_waveform_adapter.c src/ui/timeline_waveform.c \
		$(KIT_VIZ_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB) \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-waveform-pack-warmstart: $(WAVEFORM_PACK_WARMSTART_TEST_BIN)
	$(WAVEFORM_PACK_WARMSTART_TEST_BIN)

$(WAVEFORM_PACK_WARMSTART_TEST_BIN): $(WAVEFORM_PACK_WARMSTART_TEST_SRCS) src/ui/timeline_waveform.c $(KIT_VIZ_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_data/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/waveform_cache_pack_warmstart_test.c src/ui/timeline_waveform.c \
		$(KIT_VIZ_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB) \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-layout-sweep: $(LAYOUT_SWEEP_TEST_BIN)
	$(LAYOUT_SWEEP_TEST_BIN)

$(LAYOUT_SWEEP_TEST_BIN): $(LAYOUT_SWEEP_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(APP_OBJS_NO_MAIN),"$(obj)") $(TIMER_HUD_OBJS_QUOTED) $(LAYOUT_SWEEP_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-data-path-contract: $(DATA_PATH_CONTRACT_TEST_BIN)
	$(DATA_PATH_CONTRACT_TEST_BIN)

test-library-copy-vs-reference-contract: test-data-path-contract
	@echo "test-library-copy-vs-reference-contract: success"

$(DATA_PATH_CONTRACT_TEST_BIN): $(DATA_PATH_CONTRACT_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(APP_OBJS_NO_MAIN),"$(obj)") $(TIMER_HUD_OBJS_QUOTED) $(DATA_PATH_CONTRACT_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-pack-contract: $(PACK_CONTRACT_TEST_BIN)
	$(PACK_CONTRACT_TEST_BIN)

$(PACK_CONTRACT_TEST_BIN): $(PACK_CONTRACT_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(APP_OBJS_NO_MAIN),"$(obj)") $(TIMER_HUD_OBJS_QUOTED) $(PACK_CONTRACT_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-trace-contract: $(TRACE_CONTRACT_TEST_BIN)
	$(TRACE_CONTRACT_TEST_BIN)

$(TRACE_CONTRACT_TEST_BIN): $(TRACE_CONTRACT_TEST_SRCS) src/export/daw_trace_export.c $(CORE_TRACE_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic -Iinclude -I$(SHARED_ROOT)/core/core_trace/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/daw_trace_export_contract_test.c src/export/daw_trace_export.c $(CORE_TRACE_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_BASE_LIB) \
		-lm -o "$@"

test-trace-async-contract: $(TRACE_ASYNC_CONTRACT_TEST_BIN)
	$(TRACE_ASYNC_CONTRACT_TEST_BIN)

$(TRACE_ASYNC_CONTRACT_TEST_BIN): $(TRACE_ASYNC_CONTRACT_TEST_SRCS) src/export/daw_trace_export.c src/export/daw_trace_export_async.c $(CORE_WORKERS_LIB) $(CORE_TRACE_LIB) $(CORE_QUEUE_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic -Iinclude -I$(SHARED_ROOT)/core/core_workers/include -I$(SHARED_ROOT)/core/core_trace/include -I$(SHARED_ROOT)/core/core_queue/include -I$(SHARED_ROOT)/core/core_pack/include -I$(SHARED_ROOT)/core/core_io/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/daw_trace_export_async_contract_test.c src/export/daw_trace_export.c src/export/daw_trace_export_async.c $(CORE_WORKERS_LIB) $(CORE_TRACE_LIB) $(CORE_QUEUE_LIB) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_BASE_LIB) \
		-lm -lpthread -o "$@"

test-kitviz-fx-preview-adapter: $(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN)
	$(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN)

$(KITVIZ_FX_PREVIEW_ADAPTER_TEST_BIN): $(KITVIZ_FX_PREVIEW_ADAPTER_TEST_SRCS) src/ui/kit_viz_fx_preview_adapter.c $(KIT_VIZ_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_data/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/kit_viz_fx_preview_adapter_test.c src/ui/kit_viz_fx_preview_adapter.c $(KIT_VIZ_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB) \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-kitviz-meter-adapter: $(KITVIZ_METER_ADAPTER_TEST_BIN)
	$(KITVIZ_METER_ADAPTER_TEST_BIN)

$(KITVIZ_METER_ADAPTER_TEST_BIN): $(KITVIZ_METER_ADAPTER_TEST_SRCS) src/ui/kit_viz_meter_adapter.c $(KIT_VIZ_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/kit/kit_viz/include -I$(SHARED_ROOT)/core/core_data/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/kit_viz_meter_adapter_test.c src/ui/kit_viz_meter_adapter.c $(KIT_VIZ_LIB) $(CORE_DATA_LIB) $(CORE_BASE_LIB) \
		$(SDL2_LDFLAGS) -lSDL2 -lm -o "$@"

test-shared-theme-font-adapter: $(SHARED_THEME_FONT_ADAPTER_TEST_BIN)
	$(SHARED_THEME_FONT_ADAPTER_TEST_BIN)

$(SHARED_THEME_FONT_ADAPTER_TEST_BIN): $(SHARED_THEME_FONT_ADAPTER_TEST_SRCS) src/ui/shared_theme_font_adapter.c $(CORE_THEME_LIB) $(CORE_FONT_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(SHARED_ROOT)/core/core_theme/include -I$(SHARED_ROOT)/core/core_font/include -I$(SHARED_ROOT)/core/core_base/include \
		tests/shared_theme_font_adapter_test.c src/ui/shared_theme_font_adapter.c $(CORE_THEME_LIB) $(CORE_FONT_LIB) $(CORE_BASE_LIB) \
		$(SDL2_LDFLAGS) -lSDL2 -o "$@"

$(TEST_BUILD_ROOT)/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -c "$<" -o "$@"

-include $(ALL_DEPS)

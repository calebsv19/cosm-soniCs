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

CPPFLAGS := -Iinclude -Iextern -I$(SDLAPP_DIR) -I$(VK_RENDERER_DIR)/include -I$(TIMER_HUD_DIR)/include -I$(TIMER_HUD_DIR)/external -I$(CORE_BASE_DIR)/include -I$(CORE_IO_DIR)/include -I$(CORE_DATA_DIR)/include -I$(CORE_PACK_DIR)/include -I$(CORE_TIME_DIR)/include -I$(CORE_THEME_DIR)/include -I$(CORE_FONT_DIR)/include -I$(CORE_PANE_DIR)/include -I$(CORE_QUEUE_DIR)/include -I$(CORE_SCHED_DIR)/include -I$(CORE_JOBS_DIR)/include -I$(CORE_WORKERS_DIR)/include -I$(CORE_WAKE_DIR)/include -I$(CORE_KERNEL_DIR)/include -I$(CORE_TRACE_DIR)/include -I$(KIT_VIZ_DIR)/include -I$(KIT_RENDER_DIR)/include -I$(KIT_WORKSPACE_AUTHORING_DIR)/include $(SDL2_CFLAGS) $(VULKAN_CFLAGS) -DVK_RENDERER_SHADER_ROOT=\"$(abspath $(VK_RENDERER_DIR))\" -include $(VK_RENDERER_DIR)/include/vk_renderer_sdl.h

LDFLAGS := $(ARCH_FLAGS) $(SDL2_LDFLAGS) $(SDL2_LIBS) $(SDL2_FRAMEWORKS) $(VULKAN_LIBS)
ifeq ($(UNAME_S),Darwin)
	CFLAGS += -DVK_USE_PLATFORM_METAL_EXT
	LDFLAGS += -framework AudioToolbox -framework CoreFoundation -framework Metal -framework QuartzCore -framework Cocoa -framework IOKit -framework CoreVideo
endif

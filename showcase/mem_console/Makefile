CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
PKG_CONFIG ?= pkg-config

CORE_BASE_DIR ?= ../../core/core_base
CORE_THEME_DIR ?= ../../core/core_theme
CORE_FONT_DIR ?= ../../core/core_font
CORE_PACK_DIR ?= ../../core/core_pack
CORE_TIME_DIR ?= ../../core/core_time
CORE_QUEUE_DIR ?= ../../core/core_queue
CORE_SCHED_DIR ?= ../../core/core_sched
CORE_JOBS_DIR ?= ../../core/core_jobs
CORE_WORKERS_DIR ?= ../../core/core_workers
CORE_WAKE_DIR ?= ../../core/core_wake
CORE_KERNEL_DIR ?= ../../core/core_kernel
CORE_MEMDB_DIR ?= ../../core/core_memdb
KIT_RENDER_DIR ?= ../../kit/kit_render
KIT_UI_DIR ?= ../../kit/kit_ui
KIT_GRAPH_STRUCT_DIR ?= ../../kit/kit_graph_struct
VK_RENDERER_DIR ?= ../../vk_renderer

VULKAN_CFLAGS := $(shell $(PKG_CONFIG) --cflags vulkan 2>/dev/null)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_TTF_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2_ttf 2>/dev/null)
ifeq ($(strip $(VULKAN_CFLAGS)),)
  VULKAN_CFLAGS := -I/opt/homebrew/include
endif
ifeq ($(strip $(SDL_CFLAGS)),)
  SDL_CFLAGS := -I/opt/homebrew/include/SDL2
endif
ifeq ($(strip $(SDL_TTF_CFLAGS)),)
  SDL_TTF_CFLAGS := -I/opt/homebrew/include/SDL2
endif
VULKAN_LIBS := $(shell $(PKG_CONFIG) --libs vulkan 2>/dev/null)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
SDL_TTF_LIBS := $(shell $(PKG_CONFIG) --libs sdl2_ttf 2>/dev/null)
ifeq ($(strip $(VULKAN_LIBS)),)
  VULKAN_LIBS := -L/opt/homebrew/lib -lvulkan
endif
ifeq ($(strip $(SDL_LIBS)),)
  SDL_LIBS := -L/opt/homebrew/lib -lSDL2
endif
ifeq ($(strip $(SDL_TTF_LIBS)),)
  SDL_TTF_LIBS := -L/opt/homebrew/lib -lSDL2_ttf
endif
APPLE_FW := -framework Metal -framework QuartzCore -framework Cocoa -framework IOKit -framework CoreVideo

INC = -Isrc -I$(CORE_MEMDB_DIR)/include -I$(CORE_PACK_DIR)/include -I$(CORE_TIME_DIR)/include -I$(CORE_QUEUE_DIR)/include -I$(CORE_SCHED_DIR)/include -I$(CORE_JOBS_DIR)/include -I$(CORE_WORKERS_DIR)/include -I$(CORE_WAKE_DIR)/include -I$(CORE_KERNEL_DIR)/include -I$(KIT_UI_DIR)/include -I$(KIT_GRAPH_STRUCT_DIR)/include -I$(KIT_RENDER_DIR)/include -I$(VK_RENDERER_DIR)/include -I$(CORE_BASE_DIR)/include -I$(CORE_THEME_DIR)/include -I$(CORE_FONT_DIR)/include $(VULKAN_CFLAGS) $(SDL_CFLAGS) $(SDL_TTF_CFLAGS)

OBJ_DIR = build
BIN = $(OBJ_DIR)/mem_console
SRC = src/mem_console.c src/mem_console_state.c src/mem_console_db.c src/mem_console_prefs.c src/mem_console_runtime.c src/mem_console_kernel_bridge.c src/mem_console_ui_common.c src/mem_console_ui_graph.c src/mem_console_ui_left_panel.c src/mem_console_ui_detail_panel.c src/mem_console_ui.c

.PHONY: all clean run run-demo vk-renderer-lib

all: $(BIN)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(KIT_RENDER_DIR)/build/vk/libkit_render.a:
	$(MAKE) -C $(KIT_RENDER_DIR) KIT_RENDER_ENABLE_VK=1

$(KIT_UI_DIR)/build/libkit_ui.a: $(KIT_RENDER_DIR)/build/vk/libkit_render.a
	$(MAKE) -C $(KIT_UI_DIR) KIT_RENDER_ENABLE_VK=1

$(KIT_GRAPH_STRUCT_DIR)/build/libkit_graph_struct.a: $(KIT_UI_DIR)/build/libkit_ui.a
	$(MAKE) -C $(KIT_GRAPH_STRUCT_DIR) KIT_RENDER_ENABLE_VK=1

$(CORE_BASE_DIR)/build/libcore_base.a:
	$(MAKE) -C $(CORE_BASE_DIR)

$(CORE_THEME_DIR)/build/libcore_theme.a: $(CORE_BASE_DIR)/build/libcore_base.a
	$(MAKE) -C $(CORE_THEME_DIR)

$(CORE_FONT_DIR)/build/libcore_font.a: $(CORE_BASE_DIR)/build/libcore_base.a
	$(MAKE) -C $(CORE_FONT_DIR)

$(CORE_QUEUE_DIR)/build/libcore_queue.a:
	$(MAKE) -C $(CORE_QUEUE_DIR)

$(CORE_TIME_DIR)/build/libcore_time.a:
	$(MAKE) -C $(CORE_TIME_DIR)

$(CORE_SCHED_DIR)/build/libcore_sched.a:
	$(MAKE) -C $(CORE_SCHED_DIR)

$(CORE_JOBS_DIR)/build/libcore_jobs.a:
	$(MAKE) -C $(CORE_JOBS_DIR)

$(CORE_WORKERS_DIR)/build/libcore_workers.a: $(CORE_QUEUE_DIR)/build/libcore_queue.a
	$(MAKE) -C $(CORE_WORKERS_DIR)

$(CORE_WAKE_DIR)/build/libcore_wake.a:
	$(MAKE) -C $(CORE_WAKE_DIR)

$(CORE_KERNEL_DIR)/build/libcore_kernel.a: $(CORE_TIME_DIR)/build/libcore_time.a $(CORE_SCHED_DIR)/build/libcore_sched.a $(CORE_JOBS_DIR)/build/libcore_jobs.a $(CORE_WAKE_DIR)/build/libcore_wake.a $(CORE_QUEUE_DIR)/build/libcore_queue.a
	$(MAKE) -C $(CORE_KERNEL_DIR)

$(CORE_MEMDB_DIR)/build/libcore_memdb.a: $(CORE_BASE_DIR)/build/libcore_base.a
	$(MAKE) -C $(CORE_MEMDB_DIR)

$(CORE_PACK_DIR)/build/libcore_pack.a: $(CORE_BASE_DIR)/build/libcore_base.a
	$(MAKE) -C $(CORE_PACK_DIR)

vk-renderer-lib:
	$(MAKE) -C $(VK_RENDERER_DIR)

$(BIN): $(SRC) $(KIT_GRAPH_STRUCT_DIR)/build/libkit_graph_struct.a $(KIT_UI_DIR)/build/libkit_ui.a $(KIT_RENDER_DIR)/build/vk/libkit_render.a $(CORE_MEMDB_DIR)/build/libcore_memdb.a $(CORE_PACK_DIR)/build/libcore_pack.a $(CORE_KERNEL_DIR)/build/libcore_kernel.a $(CORE_WORKERS_DIR)/build/libcore_workers.a $(CORE_QUEUE_DIR)/build/libcore_queue.a $(CORE_SCHED_DIR)/build/libcore_sched.a $(CORE_JOBS_DIR)/build/libcore_jobs.a $(CORE_WAKE_DIR)/build/libcore_wake.a $(CORE_TIME_DIR)/build/libcore_time.a vk-renderer-lib $(CORE_THEME_DIR)/build/libcore_theme.a $(CORE_FONT_DIR)/build/libcore_font.a $(CORE_BASE_DIR)/build/libcore_base.a | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC) $(SRC) $(KIT_GRAPH_STRUCT_DIR)/build/libkit_graph_struct.a $(KIT_UI_DIR)/build/libkit_ui.a $(KIT_RENDER_DIR)/build/vk/libkit_render.a $(VK_RENDERER_DIR)/build/lib/libvkrenderer.a $(CORE_MEMDB_DIR)/build/libcore_memdb.a $(CORE_PACK_DIR)/build/libcore_pack.a $(CORE_KERNEL_DIR)/build/libcore_kernel.a $(CORE_WORKERS_DIR)/build/libcore_workers.a $(CORE_QUEUE_DIR)/build/libcore_queue.a $(CORE_SCHED_DIR)/build/libcore_sched.a $(CORE_JOBS_DIR)/build/libcore_jobs.a $(CORE_WAKE_DIR)/build/libcore_wake.a $(CORE_TIME_DIR)/build/libcore_time.a $(CORE_THEME_DIR)/build/libcore_theme.a $(CORE_FONT_DIR)/build/libcore_font.a $(CORE_BASE_DIR)/build/libcore_base.a $(VULKAN_LIBS) $(SDL_LIBS) $(SDL_TTF_LIBS) $(APPLE_FW) -lm -o $@

RUN_ARGS ?=
REPO_ROOT := ../../..
DEMO_DB ?= $(if $(wildcard $(HOME)/Desktop/CodeWork/data),$(HOME)/Desktop/CodeWork/data/codework_mem_console.sqlite,shared/showcase/mem_console/demo/demo_mem_console.sqlite)

run: $(BIN)
	cd $(REPO_ROOT) && ./shared/showcase/mem_console/$(BIN) --db $(DEMO_DB) $(RUN_ARGS)

run-demo: $(BIN)
	cd $(REPO_ROOT) && ./shared/showcase/mem_console/$(BIN) --db $(DEMO_DB) $(RUN_ARGS)

clean:
	rm -rf $(OBJ_DIR)

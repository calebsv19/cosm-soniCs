CORE_BASE_LIB_SRC := $(CORE_BASE_DIR)/build/libcore_base.a
CORE_IO_LIB_SRC := $(CORE_IO_DIR)/build/libcore_io.a
CORE_DATA_LIB_SRC := $(CORE_DATA_DIR)/build/libcore_data.a
CORE_PACK_LIB_SRC := $(CORE_PACK_DIR)/build/libcore_pack.a
CORE_TIME_LIB_SRC := $(CORE_TIME_DIR)/build/libcore_time.a
CORE_THEME_LIB_SRC := $(CORE_THEME_DIR)/build/libcore_theme.a
CORE_FONT_LIB_SRC := $(CORE_FONT_DIR)/build/libcore_font.a
CORE_PANE_LIB_SRC := $(CORE_PANE_DIR)/build/libcore_pane.a
CORE_QUEUE_LIB_SRC := $(CORE_QUEUE_DIR)/build/libcore_queue.a
CORE_SCHED_LIB_SRC := $(CORE_SCHED_DIR)/build/libcore_sched.a
CORE_JOBS_LIB_SRC := $(CORE_JOBS_DIR)/build/libcore_jobs.a
CORE_WORKERS_LIB_SRC := $(CORE_WORKERS_DIR)/build/libcore_workers.a
CORE_WAKE_LIB_SRC := $(CORE_WAKE_DIR)/build/libcore_wake.a
CORE_KERNEL_LIB_SRC := $(CORE_KERNEL_DIR)/build/libcore_kernel.a
CORE_TRACE_LIB_SRC := $(CORE_TRACE_DIR)/build/libcore_trace.a
KIT_VIZ_LIB_SRC := $(KIT_VIZ_DIR)/build/libkit_viz.a
KIT_RENDER_LIB_SRC := $(KIT_RENDER_DIR)/build/vk/libkit_render.a
KIT_WORKSPACE_AUTHORING_LIB_SRC := $(KIT_WORKSPACE_AUTHORING_DIR)/build/libkit_workspace_authoring.a
VK_RENDERER_LIB_SRC := $(VK_RENDERER_DIR)/build/lib/libvkrenderer.a

CORE_BASE_LIB := $(SHARED_BUILD_DIR)/libcore_base.a
CORE_IO_LIB := $(SHARED_BUILD_DIR)/libcore_io.a
CORE_DATA_LIB := $(SHARED_BUILD_DIR)/libcore_data.a
CORE_PACK_LIB := $(SHARED_BUILD_DIR)/libcore_pack.a
CORE_TIME_LIB := $(SHARED_BUILD_DIR)/libcore_time.a
CORE_THEME_LIB := $(SHARED_BUILD_DIR)/libcore_theme.a
CORE_FONT_LIB := $(SHARED_BUILD_DIR)/libcore_font.a
CORE_PANE_LIB := $(SHARED_BUILD_DIR)/libcore_pane.a
CORE_QUEUE_LIB := $(SHARED_BUILD_DIR)/libcore_queue.a
CORE_SCHED_LIB := $(SHARED_BUILD_DIR)/libcore_sched.a
CORE_JOBS_LIB := $(SHARED_BUILD_DIR)/libcore_jobs.a
CORE_WORKERS_LIB := $(SHARED_BUILD_DIR)/libcore_workers.a
CORE_WAKE_LIB := $(SHARED_BUILD_DIR)/libcore_wake.a
CORE_KERNEL_LIB := $(SHARED_BUILD_DIR)/libcore_kernel.a
CORE_TRACE_LIB := $(SHARED_BUILD_DIR)/libcore_trace.a
KIT_VIZ_LIB := $(SHARED_BUILD_DIR)/libkit_viz.a
KIT_RENDER_LIB := $(SHARED_BUILD_DIR)/libkit_render.a
KIT_WORKSPACE_AUTHORING_LIB := $(SHARED_BUILD_DIR)/libkit_workspace_authoring.a
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
	$(CORE_PANE_LIB) \
	$(CORE_BASE_LIB) \
	$(KIT_VIZ_LIB) \
	$(KIT_RENDER_LIB) \
	$(KIT_WORKSPACE_AUTHORING_LIB) \
	$(VK_RENDERER_LIB)

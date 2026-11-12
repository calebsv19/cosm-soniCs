# Local consumer identity; shared implementation remains unchanged.
.DEFAULT_GOAL := all
L4_STAMP := $(APP_BUILD_ROOT)/l4-identity.stamp
L4_DIRS := $(VK_RENDERER_DIR) $(VK_RUNTIME_DIR) $(TIMER_HUD_DIR) $(CORE_BASE_DIR) $(CORE_IO_DIR) $(CORE_DATA_DIR) $(CORE_PACK_DIR) $(CORE_TIME_DIR) $(CORE_THEME_DIR) $(CORE_FONT_DIR) $(CORE_PANE_DIR) $(CORE_PANE_MODULE_DIR) $(CORE_PANE_SNAPSHOT_DIR) $(CORE_WORKSPACE_AUTHORING_SESSION_DIR) $(CORE_QUEUE_DIR) $(CORE_SCHED_DIR) $(CORE_JOBS_DIR) $(CORE_WORKERS_DIR) $(CORE_WAKE_DIR) $(CORE_KERNEL_DIR) $(CORE_TRACE_DIR) $(KIT_VIZ_DIR) $(KIT_RENDER_DIR) $(KIT_UI_DIR) $(KIT_WORKSPACE_AUTHORING_DIR) $(KIT_PANE_DIR)
L4_RULES = $(filter-out %.d,$(MAKEFILE_LIST))
.PHONY: l4-force shared-input-check
l4-force:
shared-input-check:
	@python3 tools/verify_shared_adoption.py --manifest config/shared_adoption.json --module "vk_renderer=$(VK_RENDERER_DIR)" --module "vk_runtime=$(VK_RUNTIME_DIR)" --module "timer_hud=$(TIMER_HUD_DIR)" --module "core/core_base=$(CORE_BASE_DIR)" --module "core/core_io=$(CORE_IO_DIR)" --module "core/core_data=$(CORE_DATA_DIR)" --module "core/core_pack=$(CORE_PACK_DIR)" --module "core/core_time=$(CORE_TIME_DIR)" --module "core/core_theme=$(CORE_THEME_DIR)" --module "core/core_font=$(CORE_FONT_DIR)" --module "core/core_pane=$(CORE_PANE_DIR)" --module "core/core_pane_module=$(CORE_PANE_MODULE_DIR)" --module "core/core_pane_snapshot=$(CORE_PANE_SNAPSHOT_DIR)" --module "core/core_workspace_authoring_session=$(CORE_WORKSPACE_AUTHORING_SESSION_DIR)" --module "core/core_queue=$(CORE_QUEUE_DIR)" --module "core/core_sched=$(CORE_SCHED_DIR)" --module "core/core_jobs=$(CORE_JOBS_DIR)" --module "core/core_workers=$(CORE_WORKERS_DIR)" --module "core/core_wake=$(CORE_WAKE_DIR)" --module "core/core_kernel=$(CORE_KERNEL_DIR)" --module "core/core_trace=$(CORE_TRACE_DIR)" --module "kit/kit_viz=$(KIT_VIZ_DIR)" --module "kit/kit_render=$(KIT_RENDER_DIR)" --module "kit/kit_ui=$(KIT_UI_DIR)" --module "kit/kit_workspace_authoring=$(KIT_WORKSPACE_AUTHORING_DIR)" --module "kit/kit_pane=$(KIT_PANE_DIR)"
$(L4_STAMP): l4-force | shared-input-check
	@mkdir -p "$(@D)"
	@{ printf '%s\n' '$(APP_CC)' '$(HOST_CC)' '$(CPPFLAGS)' '$(CFLAGS)' '$(LDFLAGS)' '$(ARCH_FLAGS)' '$(L4_DIRS)'; $(HOST_CC) --version; cksum $(L4_RULES) tools/shared_compile.py tools/verify_shared_adoption.py config/shared_adoption.json $(TOOLCHAIN_DEP); } > "$@.tmp"
	@cmp -s "$@.tmp" "$@" 2>/dev/null || mv "$@.tmp" "$@"
	@rm -f "$@.tmp"
$(APP_OBJS) $(TIMER_HUD_OBJS) $(APP_SHARED_LIBS) $(APP_BIN): $(L4_STAMP) | shared-input-check
-include $(foreach dir,$(L4_DIRS),$(shell find $(dir)/build -name '*.l4.d' -type f 2>/dev/null))

# Conservative source closure also covers nested module builds.
L4_SHARED_INPUTS := $(foreach dir,$(L4_DIRS),$(shell find $(dir)/src $(dir)/include -type f 2>/dev/null) $(wildcard $(dir)/Makefile $(dir)/VERSION))
$(APP_SHARED_LIBS): $(L4_SHARED_INPUTS)
$(ENGINE_TEST_SUPPORT_OBJS): $(L4_STAMP) | shared-input-check

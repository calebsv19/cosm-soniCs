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
$(eval $(call build_copy_static_lib,CORE_PANE,))
$(eval $(call build_copy_static_lib,CORE_QUEUE,))
$(eval $(call build_copy_static_lib,CORE_SCHED,))
$(eval $(call build_copy_static_lib,CORE_JOBS,))
$(eval $(call build_copy_static_lib,CORE_WORKERS,))
$(eval $(call build_copy_static_lib,CORE_WAKE,))
$(eval $(call build_copy_static_lib,CORE_KERNEL,))
$(eval $(call build_copy_static_lib,CORE_TRACE,))
$(eval $(call build_copy_static_lib,KIT_VIZ,))
$(eval $(call build_copy_static_lib,KIT_RENDER,KIT_RENDER_ENABLE_VK=1))
$(eval $(call build_copy_static_lib,KIT_WORKSPACE_AUTHORING,))
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

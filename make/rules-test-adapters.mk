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

test-workspace-authoring-host: $(WORKSPACE_AUTHORING_HOST_TEST_BIN)
	$(WORKSPACE_AUTHORING_HOST_TEST_BIN)

test-library-copy-vs-reference-contract: test-data-path-contract
	@echo "test-library-copy-vs-reference-contract: success"

$(DATA_PATH_CONTRACT_TEST_BIN): $(DATA_PATH_CONTRACT_TEST_OBJS) $(APP_OBJS_NO_MAIN)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(APP_OBJS_NO_MAIN),"$(obj)") $(TIMER_HUD_OBJS_QUOTED) $(DATA_PATH_CONTRACT_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

$(WORKSPACE_AUTHORING_HOST_TEST_BIN): $(WORKSPACE_AUTHORING_HOST_TEST_SRCS) src/app/workspace_authoring/daw_workspace_authoring_host.c src/ui/shared_theme_font_adapter.c $(KIT_WORKSPACE_AUTHORING_LIB) $(CORE_THEME_LIB) $(CORE_FONT_LIB) $(CORE_PANE_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic $(SDL2_CFLAGS) -Iinclude -I$(KIT_WORKSPACE_AUTHORING_DIR)/include -I$(KIT_RENDER_DIR)/include -I$(CORE_THEME_DIR)/include -I$(CORE_FONT_DIR)/include -I$(CORE_PANE_DIR)/include -I$(CORE_BASE_DIR)/include \
		tests/daw_workspace_authoring_host_test.c tests/daw_workspace_authoring_kit_render_stub.c src/app/workspace_authoring/daw_workspace_authoring_host.c src/ui/shared_theme_font_adapter.c \
		$(KIT_WORKSPACE_AUTHORING_LIB) $(CORE_THEME_LIB) $(CORE_FONT_LIB) $(CORE_PANE_LIB) $(CORE_BASE_LIB) \
		$(SDL2_LDFLAGS) -lSDL2 -o "$@"

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

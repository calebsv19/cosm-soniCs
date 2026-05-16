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

TIMELINE_CONTRACT_TEST_SRCS := \
	tests/timeline_contract_test.c

TIMELINE_CONTRACT_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(TIMELINE_CONTRACT_TEST_SRCS))
TIMELINE_CONTRACT_TEST_BIN := $(TEST_BUILD_ROOT)/timeline_contract_test

MIDI_MODEL_TEST_SRCS := \
	tests/midi_model_test.c

MIDI_MODEL_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(MIDI_MODEL_TEST_SRCS))
MIDI_MODEL_TEST_BIN := $(TEST_BUILD_ROOT)/midi_model_test

MIDI_INSTRUMENT_RENDER_TEST_SRCS := \
	tests/midi_instrument_render_test.c

MIDI_INSTRUMENT_RENDER_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(MIDI_INSTRUMENT_RENDER_TEST_SRCS))
MIDI_INSTRUMENT_RENDER_TEST_BIN := $(TEST_BUILD_ROOT)/midi_instrument_render_test

TIMELINE_MIDI_REGION_TEST_SRCS := \
	tests/timeline_midi_region_test.c

TIMELINE_MIDI_REGION_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(TIMELINE_MIDI_REGION_TEST_SRCS))
TIMELINE_MIDI_REGION_TEST_BIN := $(TEST_BUILD_ROOT)/timeline_midi_region_test

MIDI_EDITOR_SHELL_TEST_SRCS := \
	tests/midi_editor_shell_test.c

MIDI_EDITOR_SHELL_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(MIDI_EDITOR_SHELL_TEST_SRCS))
MIDI_EDITOR_SHELL_TEST_BIN := $(TEST_BUILD_ROOT)/midi_editor_shell_test

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

WORKSPACE_AUTHORING_HOST_TEST_SRCS := \
	tests/daw_workspace_authoring_host_test.c \
	tests/daw_workspace_authoring_kit_render_stub.c

WORKSPACE_AUTHORING_HOST_TEST_BIN := $(TEST_BUILD_ROOT)/daw_workspace_authoring_host_test

AUDIO_CAPTURE_DEVICE_TEST_SRCS := \
	tests/audio_capture_device_test.c

AUDIO_CAPTURE_DEVICE_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(AUDIO_CAPTURE_DEVICE_TEST_SRCS))
AUDIO_CAPTURE_DEVICE_TEST_BIN := $(TEST_BUILD_ROOT)/audio_capture_device_test

AUDIO_RECORDING_TEST_SRCS := \
	tests/audio_recording_test.c

AUDIO_RECORDING_TEST_OBJS := $(patsubst tests/%.c,$(TEST_BUILD_ROOT)/%.o,$(AUDIO_RECORDING_TEST_SRCS))
AUDIO_RECORDING_TEST_BIN := $(TEST_BUILD_ROOT)/audio_recording_test

# Keep legacy app tests wired to the full non-main app object set so link coverage
# stays in lock-step with engine/runtime refactors.
APP_OBJS_NO_MAIN := $(filter-out $(APP_OBJ_DIR)/src/app/main.o,$(APP_OBJS))
ENGINE_TEST_SUPPORT_OBJS = $(APP_OBJS_NO_MAIN) $(TIMER_HUD_OBJS)

APP_DEPS := $(APP_OBJS:.o=.d)
TIMER_HUD_DEPS := $(TIMER_HUD_OBJS:.o=.d)
TEST_DEPS := $(TEST_OBJS:.o=.d)
CACHE_TEST_DEPS := $(CACHE_TEST_OBJS:.o=.d)
OVERLAP_TEST_DEPS := $(OVERLAP_TEST_OBJS:.o=.d)
TIMELINE_CONTRACT_TEST_DEPS := $(TIMELINE_CONTRACT_TEST_OBJS:.o=.d)
MIDI_MODEL_TEST_DEPS := $(MIDI_MODEL_TEST_OBJS:.o=.d)
MIDI_INSTRUMENT_RENDER_TEST_DEPS := $(MIDI_INSTRUMENT_RENDER_TEST_OBJS:.o=.d)
TIMELINE_MIDI_REGION_TEST_DEPS := $(TIMELINE_MIDI_REGION_TEST_OBJS:.o=.d)
MIDI_EDITOR_SHELL_TEST_DEPS := $(MIDI_EDITOR_SHELL_TEST_OBJS:.o=.d)
SMOKE_TEST_DEPS := $(SMOKE_TEST_OBJS:.o=.d)
PACK_CONTRACT_TEST_DEPS := $(PACK_CONTRACT_TEST_OBJS:.o=.d)
LAYOUT_SWEEP_TEST_DEPS := $(LAYOUT_SWEEP_TEST_OBJS:.o=.d)
DATA_PATH_CONTRACT_TEST_DEPS := $(DATA_PATH_CONTRACT_TEST_OBJS:.o=.d)
ENGINE_TEST_SUPPORT_DEPS := $(ENGINE_TEST_SUPPORT_OBJS:.o=.d)
AUDIO_CAPTURE_DEVICE_TEST_DEPS := $(AUDIO_CAPTURE_DEVICE_TEST_OBJS:.o=.d)
AUDIO_RECORDING_TEST_DEPS := $(AUDIO_RECORDING_TEST_OBJS:.o=.d)
ALL_DEPS := $(APP_DEPS) $(TIMER_HUD_DEPS) $(TEST_DEPS) $(CACHE_TEST_DEPS) $(OVERLAP_TEST_DEPS) $(TIMELINE_CONTRACT_TEST_DEPS) $(MIDI_MODEL_TEST_DEPS) $(MIDI_INSTRUMENT_RENDER_TEST_DEPS) $(TIMELINE_MIDI_REGION_TEST_DEPS) $(MIDI_EDITOR_SHELL_TEST_DEPS) $(SMOKE_TEST_DEPS) $(PACK_CONTRACT_TEST_DEPS) $(LAYOUT_SWEEP_TEST_DEPS) $(DATA_PATH_CONTRACT_TEST_DEPS) $(AUDIO_CAPTURE_DEVICE_TEST_DEPS) $(AUDIO_RECORDING_TEST_DEPS) $(ENGINE_TEST_SUPPORT_DEPS)

STABLE_TEST_TARGETS := \
	test-pack-contract \
	test-trace-contract \
	test-trace-async-contract \
	test-kitviz-adapter \
	test-kitviz-fx-preview-adapter \
	test-kitviz-meter-adapter \
	test-waveform-pack-warmstart \
	test-layout-sweep \
	test-timeline-contract \
	test-midi-model \
	test-midi-instrument-render \
	test-timeline-midi-region \
	test-midi-editor-shell \
	test-audio-capture-device \
	test-audio-recording \
	test-data-path-contract \
	test-workspace-authoring-host \
	test-library-copy-vs-reference-contract

LEGACY_TEST_TARGETS := \
	test-session \
	test-cache \
	test-overlap \
	test-smoke \
	test-shared-theme-font-adapter

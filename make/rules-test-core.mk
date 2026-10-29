test-mcp-bridge-timeout:
	$(MCP_PYTHON) tests/mcp_bridge_timeout_test.py

.PHONY: test-song-export test-mcp-song-export
test-song-export: $(TEST_BUILD_ROOT)/song_export_test
	$(TEST_BUILD_ROOT)/song_export_test
$(TEST_BUILD_ROOT)/song_export_test: tests/song_export_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)
test-mcp-song-export: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_song_export_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

test-mcp-dynamics-feedback: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_dynamics_feedback_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

test-dynamics-feedback: $(TEST_BUILD_ROOT)/dynamics_feedback_test
	$(TEST_BUILD_ROOT)/dynamics_feedback_test
$(TEST_BUILD_ROOT)/dynamics_feedback_test: tests/dynamics_feedback_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-live-stereo test-mcp-live-stereo
test-live-stereo: $(TEST_BUILD_ROOT)/live_stereo_test
	$(TEST_BUILD_ROOT)/live_stereo_test
$(TEST_BUILD_ROOT)/live_stereo_test: tests/live_stereo_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)
test-mcp-live-stereo: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_live_stereo_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

test:
	@$(MAKE) BUILD_TOOLCHAIN="$(TEST_TOOLCHAIN)" run-headless-smoke
	@$(MAKE) BUILD_TOOLCHAIN="$(TEST_TOOLCHAIN)" test-stable

test-stable:
	@$(MAKE) $(STABLE_TEST_TARGETS)
	@echo "daw stable test lane passed"

.PHONY: test-application-instrument-action
test-application-instrument-action: $(TEST_BUILD_ROOT)/application_instrument_action_test
	$(TEST_BUILD_ROOT)/application_instrument_action_test

$(TEST_BUILD_ROOT)/application_instrument_action_test: tests/application_instrument_action_test.c src/app/app_actions.c src/engine/engine_clips_midi.c src/undo/undo_manager_stack.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/app/app_actions.o $(APP_OBJ_DIR)/src/engine/engine_clips_midi.o $(APP_OBJ_DIR)/src/undo/undo_manager_stack.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-audio-output-device
test-audio-output-device: $(TEST_BUILD_ROOT)/audio_output_device_test
	$(TEST_BUILD_ROOT)/audio_output_device_test

$(TEST_BUILD_ROOT)/audio_output_device_test: tests/audio_output_device_test.c src/audio/device_monitor.c src/audio/device_sdl.c src/audio/device_preview.c src/audio/device_comparison.c include/audio/audio_device.h
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< src/audio/device_comparison.c src/audio/device_monitor.c -o "$@" $(LDFLAGS)

.PHONY: test-engine-command-delivery
.PHONY: test-effects-revision
test-effects-revision: $(TEST_BUILD_ROOT)/effects_revision_test
	$(TEST_BUILD_ROOT)/effects_revision_test

$(TEST_BUILD_ROOT)/effects_revision_test: tests/effects_revision_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-engine-command-delivery: $(TEST_BUILD_ROOT)/engine_command_delivery_test
	$(TEST_BUILD_ROOT)/engine_command_delivery_test

$(TEST_BUILD_ROOT)/engine_command_delivery_test: tests/engine_command_delivery_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-engine-source-lifetime
.PHONY: test-engine-analysis-lifecycle
test-engine-analysis-lifecycle: $(TEST_BUILD_ROOT)/engine_analysis_lifecycle_test
	$(TEST_BUILD_ROOT)/engine_analysis_lifecycle_test

$(TEST_BUILD_ROOT)/engine_analysis_lifecycle_test: tests/engine_analysis_lifecycle_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/engine_core.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-engine-mix-ownership
test-engine-mix-ownership: $(TEST_BUILD_ROOT)/engine_mix_ownership_test
	@mkdir -p tmp
	$(TEST_BUILD_ROOT)/engine_mix_ownership_test

$(TEST_BUILD_ROOT)/engine_mix_ownership_test: tests/engine_mix_ownership_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-engine-source-lifetime: $(TEST_BUILD_ROOT)/engine_source_lifetime_test
	@mkdir -p tmp
	$(TEST_BUILD_ROOT)/engine_source_lifetime_test

$(TEST_BUILD_ROOT)/engine_source_lifetime_test: tests/engine_source_lifetime_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

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
	$(APP_OBJ_DIR)/src/effects/parameter_automation.o $(APP_OBJ_DIR)/src/engine/mixer_automation.o $(APP_OBJ_DIR)/src/session/session_mixer_automation.o \
	$(APP_OBJ_DIR)/src/engine/automation.o \
	$(APP_OBJ_DIR)/src/engine/automation_semantics.o \
	$(APP_OBJ_DIR)/src/session/session_automation.o \
	$(APP_OBJ_DIR)/src/engine/instrument_percussion.o \
	$(APP_OBJ_DIR)/src/engine/instrument_catalog.o \
	$(APP_OBJ_DIR)/src/engine/instrument_controls.o \
	$(APP_OBJ_DIR)/src/engine/instrument_filter.o \
	$(APP_OBJ_DIR)/src/session/save_file.o \
	$(APP_OBJ_DIR)/src/session/session_document.o \
	$(APP_OBJ_DIR)/src/session/session_validation.o \
	$(APP_OBJ_DIR)/src/session/session_io_write.o \
	$(APP_OBJ_DIR)/src/session/session_io_read.o \
	$(APP_OBJ_DIR)/src/session/session_io_json.o \
	$(APP_OBJ_DIR)/src/session/session_json_string.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_document.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_engine.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_effects_panel.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_master_fx.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_track_clips.o \
	$(APP_OBJ_DIR)/src/session/session_io_read_parse_track_fx.o \
	$(APP_OBJ_DIR)/src/engine/midi.o \
	$(APP_OBJ_DIR)/src/config/config.o \
	$(CORE_PACK_LIB) \
	$(CORE_IO_LIB) \
	$(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-cache: $(CACHE_TEST_BIN)
	$(CACHE_TEST_BIN)

$(CACHE_TEST_BIN): $(CACHE_TEST_OBJS) $(APP_OBJ_DIR)/src/audio/media_cache.o $(APP_OBJ_DIR)/src/audio/media_clip.o $(APP_OBJ_DIR)/src/audio/resample.o
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-overlap: $(OVERLAP_TEST_BIN)
	$(OVERLAP_TEST_BIN)

$(OVERLAP_TEST_BIN): $(OVERLAP_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(OVERLAP_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-timeline-contract: $(TIMELINE_CONTRACT_TEST_BIN)
	$(TIMELINE_CONTRACT_TEST_BIN)

$(TIMELINE_CONTRACT_TEST_BIN): $(TIMELINE_CONTRACT_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(TIMELINE_CONTRACT_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-midi-model: $(MIDI_MODEL_TEST_BIN)
	$(MIDI_MODEL_TEST_BIN)

$(MIDI_MODEL_TEST_BIN): $(MIDI_MODEL_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(MIDI_MODEL_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-midi-instrument-render: $(MIDI_INSTRUMENT_RENDER_TEST_BIN)
	$(MIDI_INSTRUMENT_RENDER_TEST_BIN)

$(MIDI_INSTRUMENT_RENDER_TEST_BIN): $(MIDI_INSTRUMENT_RENDER_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(MIDI_INSTRUMENT_RENDER_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-timeline-midi-region: $(TIMELINE_MIDI_REGION_TEST_BIN)
	$(TIMELINE_MIDI_REGION_TEST_BIN)

$(TIMELINE_MIDI_REGION_TEST_BIN): $(TIMELINE_MIDI_REGION_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(TIMELINE_MIDI_REGION_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-midi-editor-shell: $(MIDI_EDITOR_SHELL_TEST_BIN)
	$(MIDI_EDITOR_SHELL_TEST_BIN)

$(MIDI_EDITOR_SHELL_TEST_BIN): $(MIDI_EDITOR_SHELL_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(MIDI_EDITOR_SHELL_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-audio-capture-device: $(AUDIO_CAPTURE_DEVICE_TEST_BIN)
	$(AUDIO_CAPTURE_DEVICE_TEST_BIN)

$(AUDIO_CAPTURE_DEVICE_TEST_BIN): $(AUDIO_CAPTURE_DEVICE_TEST_OBJS) $(APP_OBJ_DIR)/src/audio/audio_capture_device_sdl.o
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

test-audio-recording: $(AUDIO_RECORDING_TEST_BIN)
	$(AUDIO_RECORDING_TEST_BIN)

$(AUDIO_RECORDING_TEST_BIN): $(AUDIO_RECORDING_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/app/audio_recording.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(AUDIO_RECORDING_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-track-role: $(TRACK_ROLE_TEST_BIN)
	$(TRACK_ROLE_TEST_BIN)

$(TRACK_ROLE_TEST_BIN): $(TRACK_ROLE_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(TRACK_ROLE_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-smoke: $(SMOKE_TEST_BIN)
	$(SMOKE_TEST_BIN)

$(SMOKE_TEST_BIN): $(SMOKE_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(SMOKE_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

$(TEST_BUILD_ROOT)/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -c "$<" -o "$@"

.PHONY: test-engine-parameter-transaction
test-engine-parameter-transaction: $(TEST_BUILD_ROOT)/engine_parameter_transaction_test
	$(TEST_BUILD_ROOT)/engine_parameter_transaction_test

$(TEST_BUILD_ROOT)/engine_parameter_transaction_test: tests/engine_parameter_transaction_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/undo/undo_manager_stack.o $(APP_OBJ_DIR)/src/engine/engine_tracks.o $(APP_OBJ_DIR)/src/engine/engine_fx.o $(APP_OBJ_DIR)/src/engine/engine_clips.o $(APP_OBJ_DIR)/src/engine/engine_clip_history.o $(APP_OBJ_DIR)/src/engine/engine_scope_host.o $(APP_OBJ_DIR)/src/engine/automation.o $(APP_OBJ_DIR)/src/engine/automation_semantics.o $(APP_OBJ_DIR)/src/engine/sampler.o $(APP_OBJ_DIR)/src/engine/engine_clips_automation.o $(APP_OBJ_DIR)/src/engine/engine_clips_midi.o $(APP_OBJ_DIR)/src/engine/midi.o $(APP_OBJ_DIR)/src/engine/engine_clips_no_overlap.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-session-atomic-save
test-session-atomic-save: $(TEST_BUILD_ROOT)/session_atomic_save_test
	$(TEST_BUILD_ROOT)/session_atomic_save_test

$(TEST_BUILD_ROOT)/session_atomic_save_test: tests/session_atomic_save_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/session/save_file.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-session-transaction
test-session-transaction: $(TEST_BUILD_ROOT)/session_transaction_test
	$(TEST_BUILD_ROOT)/session_transaction_test

$(TEST_BUILD_ROOT)/session_transaction_test: tests/session_transaction_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/session/session_document.o $(APP_OBJ_DIR)/src/session/session_apply.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-engine-transport-clock
test-engine-transport-clock: $(TEST_BUILD_ROOT)/engine_transport_clock_test
	$(TEST_BUILD_ROOT)/engine_transport_clock_test

$(TEST_BUILD_ROOT)/engine_transport_clock_test: tests/engine_transport_clock_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-media-durability
test-media-durability: $(TEST_BUILD_ROOT)/media_durability_test
	$(TEST_BUILD_ROOT)/media_durability_test

$(TEST_BUILD_ROOT)/media_durability_test: tests/media_durability_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/session/save_file.o $(APP_OBJ_DIR)/src/audio/wav_writer.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-fade-processing
test-fade-processing: $(TEST_BUILD_ROOT)/fade_processing_test
	$(TEST_BUILD_ROOT)/fade_processing_test

$(TEST_BUILD_ROOT)/fade_processing_test: tests/fade_processing_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-analysis-calibration
test-analysis-calibration: $(TEST_BUILD_ROOT)/analysis_calibration_test
	$(TEST_BUILD_ROOT)/analysis_calibration_test

$(TEST_BUILD_ROOT)/analysis_calibration_test: tests/analysis_calibration_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-dynamics-processing
test-dynamics-processing: $(TEST_BUILD_ROOT)/dynamics_processing_test
	$(TEST_BUILD_ROOT)/dynamics_processing_test

DYNAMICS_INSTRUMENTED_SOURCES := src/effects/dynamics/fx_sidechain_compressor.c src/effects/dynamics/fx_limiter.c src/effects/dynamics/fx_compressor.c src/effects/effects_manager.c
DYNAMICS_INSTRUMENTED_OBJS := $(patsubst src/%.c,$(TEST_BUILD_ROOT)/instrumented/%.o,$(DYNAMICS_INSTRUMENTED_SOURCES))
$(TEST_BUILD_ROOT)/instrumented/%.o: src/%.c include/effects/effects_api.h include/effects/effects_manager.h include/effects/dynamics_math.h
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -Dmalloc=daw_test_malloc -Dcalloc=daw_test_calloc -Drealloc=daw_test_realloc -Dfree=daw_test_free -c $< -o "$@"

$(TEST_BUILD_ROOT)/dynamics_processing_test: tests/dynamics_processing_test.c $(DYNAMICS_INSTRUMENTED_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(DYNAMICS_INSTRUMENTED_OBJS) $(foreach obj,$(filter-out $(patsubst %.c,$(APP_OBJ_DIR)/%.o,$(DYNAMICS_INSTRUMENTED_SOURCES)),$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-control-transitions
test-control-transitions: $(TEST_BUILD_ROOT)/control_transitions_test
	$(TEST_BUILD_ROOT)/control_transitions_test

$(TEST_BUILD_ROOT)/control_transitions_test: tests/control_transitions_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-media-conversion
test-media-conversion: $(TEST_BUILD_ROOT)/media_conversion_test
	$(TEST_BUILD_ROOT)/media_conversion_test

MEDIA_CONVERSION_INSTRUMENTED := $(TEST_BUILD_ROOT)/instrumented/audio/media_clip.o $(TEST_BUILD_ROOT)/instrumented/audio/resample.o
$(TEST_BUILD_ROOT)/media_conversion_test: tests/media_conversion_test.c $(MEDIA_CONVERSION_INSTRUMENTED)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(MEDIA_CONVERSION_INSTRUMENTED) -o "$@" $(LDFLAGS)

.PHONY: test-instrument-lifecycle
test-instrument-lifecycle: $(TEST_BUILD_ROOT)/instrument_lifecycle_test
	$(TEST_BUILD_ROOT)/instrument_lifecycle_test

INSTRUMENT_LIFECYCLE_SOURCES := src/engine/instrument_osc.c src/engine/engine_midi_audition.c src/engine/midi.c
INSTRUMENT_LIFECYCLE_OBJS := $(patsubst src/%.c,$(TEST_BUILD_ROOT)/instrumented/%.o,$(INSTRUMENT_LIFECYCLE_SOURCES))
$(TEST_BUILD_ROOT)/instrument_lifecycle_test: tests/instrument_lifecycle_test.c $(INSTRUMENT_LIFECYCLE_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(INSTRUMENT_LIFECYCLE_OBJS) $(foreach obj,$(filter-out $(patsubst %.c,$(APP_OBJ_DIR)/%.o,$(INSTRUMENT_LIFECYCLE_SOURCES)),$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-export-render
test-export-render: $(TEST_BUILD_ROOT)/export_render_test
	$(TEST_BUILD_ROOT)/export_render_test

EXPORT_RENDER_SOURCES := src/engine/export_stream.c src/engine/engine_io.c src/engine/engine_source_plan.c src/engine/instrument_osc.c src/engine/sampler.c src/engine/automation.c src/engine/engine_eq.c src/engine/graph.c src/engine/buffer_pool.c src/effects/effects_manager.c src/effects/basics/fx_gain.c src/effects/dynamics/fx_limiter.c src/effects/delay/fx_delay_simple.c
# Inject spool failures only into the export test's private engine I/O object.
$(TEST_BUILD_ROOT)/instrumented/engine/engine_io.o: CPPFLAGS += -Dtmpfile=daw_test_tmpfile -Dfwrite=daw_test_fwrite -Dfflush=daw_test_fflush -Dfseek=daw_test_fseek -Dfread=daw_test_fread -Dfclose=daw_test_fclose
$(TEST_BUILD_ROOT)/instrumented/engine/export_stream.o: CPPFLAGS += -Dtmpfile=daw_test_tmpfile -Dfwrite=daw_test_fwrite -Dfflush=daw_test_fflush -Dfseek=daw_test_fseek -Dfread=daw_test_fread -Dfclose=daw_test_fclose

EXPORT_RENDER_OBJS := $(patsubst src/%.c,$(TEST_BUILD_ROOT)/instrumented/%.o,$(EXPORT_RENDER_SOURCES))
$(TEST_BUILD_ROOT)/export_render_test: tests/export_render_test.c $(EXPORT_RENDER_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(EXPORT_RENDER_OBJS) $(foreach obj,$(filter-out $(patsubst %.c,$(APP_OBJ_DIR)/%.o,$(EXPORT_RENDER_SOURCES)),$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-region-scheduling
test-region-scheduling: $(TEST_BUILD_ROOT)/region_scheduling_test
	$(TEST_BUILD_ROOT)/region_scheduling_test

$(TEST_BUILD_ROOT)/region_scheduling_test: tests/region_scheduling_test.c $(TEST_BUILD_ROOT)/instrumented/engine/graph.o $(APP_OBJ_DIR)/src/engine/buffer_pool.o
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $^ -o "$@" $(LDFLAGS)

.PHONY: test-midi-scheduling
test-midi-scheduling: $(TEST_BUILD_ROOT)/midi_scheduling_test
	$(TEST_BUILD_ROOT)/midi_scheduling_test

$(TEST_BUILD_ROOT)/midi_scheduling_test: tests/midi_scheduling_test.c tests/fixtures/instrument_unscheduled_reference.inc src/engine/instrument_osc.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/instrument_osc.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mixer-publication
test-mixer-publication: $(TEST_BUILD_ROOT)/mixer_publication_test
	@mkdir -p tmp
	$(TEST_BUILD_ROOT)/mixer_publication_test

$(TEST_BUILD_ROOT)/mixer_publication_test: tests/mixer_publication_test.c src/engine/engine_source_plan.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/engine_source_plan.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

# Instrumented objects must follow the same transitive header dependencies as normal objects.
INSTRUMENTED_TEST_OBJS := $(sort $(DYNAMICS_INSTRUMENTED_OBJS) $(MEDIA_CONVERSION_INSTRUMENTED) $(INSTRUMENT_LIFECYCLE_OBJS) $(EXPORT_RENDER_OBJS))
-include $(INSTRUMENTED_TEST_OBJS:.o=.d)

.PHONY: test-media-preparation
test-media-preparation: $(TEST_BUILD_ROOT)/media_preparation_test
	$(TEST_BUILD_ROOT)/media_preparation_test

$(TEST_BUILD_ROOT)/media_preparation_test: tests/media_preparation_test.c src/audio/media_cache.c $(APP_OBJ_DIR)/src/audio/media_clip.o $(APP_OBJ_DIR)/src/audio/resample.o
	@mkdir -p "$(@D)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(APP_OBJ_DIR)/src/audio/media_clip.o $(APP_OBJ_DIR)/src/audio/resample.o -o "$@" $(LDFLAGS)

.PHONY: test-media-jobs
test-media-jobs: $(TEST_BUILD_ROOT)/media_jobs_test
	$(TEST_BUILD_ROOT)/media_jobs_test

$(TEST_BUILD_ROOT)/media_jobs_test: tests/media_jobs_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(@D)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-media-import
test-media-import: $(TEST_BUILD_ROOT)/media_import_test
	$(TEST_BUILD_ROOT)/media_import_test

$(TEST_BUILD_ROOT)/media_import_test: tests/media_import_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(@D)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

$(TEST_BUILD_ROOT)/media_ffmpeg_test: tests/media_ffmpeg_test.c src/audio/media_clip.c $(APP_OBJ_DIR)/src/audio/resample.o
	@mkdir -p "$(@D)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(APP_OBJ_DIR)/src/audio/resample.o -o "$@" $(LDFLAGS)

# Keep directly linked media harnesses current after public-header-only changes.
-include $(TEST_BUILD_ROOT)/media_preparation_test.d
-include $(TEST_BUILD_ROOT)/media_jobs_test.d
-include $(TEST_BUILD_ROOT)/media_import_test.d
-include $(TEST_BUILD_ROOT)/media_ffmpeg_test.d

.PHONY: test-input-delivery
test-input-delivery: $(TEST_BUILD_ROOT)/input_delivery_test
	$(TEST_BUILD_ROOT)/input_delivery_test

$(TEST_BUILD_ROOT)/input_delivery_test: tests/input_delivery_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-shared-text-focus
test-shared-text-focus: $(TEST_BUILD_ROOT)/shared_text_focus_test
	$(TEST_BUILD_ROOT)/shared_text_focus_test

$(TEST_BUILD_ROOT)/shared_text_focus_test: tests/shared_text_focus_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: build-native-text-ui-proof
build-native-text-ui-proof: $(TEST_BUILD_ROOT)/native_text_ui_test

$(TEST_BUILD_ROOT)/native_text_ui_test: tests/native_text_ui_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-shared-editor-controls
test-shared-editor-controls: $(TEST_BUILD_ROOT)/shared_editor_controls_test
	$(TEST_BUILD_ROOT)/shared_editor_controls_test

$(TEST_BUILD_ROOT)/shared_editor_controls_test: tests/shared_editor_controls_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: build-native-editor-controls-proof
build-native-editor-controls-proof: $(TEST_BUILD_ROOT)/native_editor_controls_test

$(TEST_BUILD_ROOT)/native_editor_controls_test: tests/native_editor_controls_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-shared-editor-exceptions
test-shared-editor-exceptions: $(TEST_BUILD_ROOT)/shared_editor_exceptions_test
	$(TEST_BUILD_ROOT)/shared_editor_exceptions_test

$(TEST_BUILD_ROOT)/shared_editor_exceptions_test: tests/shared_editor_exceptions_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-shared-pane-composition
test-shared-pane-composition: $(TEST_BUILD_ROOT)/shared_pane_composition_test
	$(TEST_BUILD_ROOT)/shared_pane_composition_test

$(TEST_BUILD_ROOT)/shared_pane_composition_test: tests/shared_pane_composition_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-shared-window-lifecycle
test-shared-window-lifecycle: $(TEST_BUILD_ROOT)/shared_window_lifecycle_test
	$(TEST_BUILD_ROOT)/shared_window_lifecycle_test

$(TEST_BUILD_ROOT)/shared_window_lifecycle_test: tests/shared_window_lifecycle_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-shared-control-chrome
test-shared-control-chrome: $(TEST_BUILD_ROOT)/shared_control_chrome_test
	$(TEST_BUILD_ROOT)/shared_control_chrome_test

$(TEST_BUILD_ROOT)/shared_control_chrome_test: tests/shared_control_chrome_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: build-mcp-control-fixture test-mcp-control
build-mcp-control-fixture: $(TEST_BUILD_ROOT)/mcp_control_fixture

$(TEST_BUILD_ROOT)/mcp_control_fixture: tests/mcp_control_fixture.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

MCP_PYTHON ?= build/mcp-venv/bin/python
test-mcp-control: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_control_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture

.PHONY: test-instrument-controls
test-sampled-violin: $(TEST_BUILD_ROOT)/sampled_violin_test
	$(TEST_BUILD_ROOT)/sampled_violin_test

$(TEST_BUILD_ROOT)/sampled_violin_test: tests/sampled_violin_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-instrument-controls: $(TEST_BUILD_ROOT)/instrument_controls_test
	$(TEST_BUILD_ROOT)/instrument_controls_test

$(TEST_BUILD_ROOT)/instrument_controls_test: tests/instrument_controls_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-instrument-loop
test-mcp-instrument-loop: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_instrument_loop_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-preset-library test-mcp-presets
test-preset-library: $(TEST_BUILD_ROOT)/preset_library_test
	$(TEST_BUILD_ROOT)/preset_library_test

$(TEST_BUILD_ROOT)/preset_library_test: tests/preset_library_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-mcp-presets: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_preset_library_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-sound-inspection test-mcp-sound-analysis
test-sound-inspection: $(TEST_BUILD_ROOT)/sound_inspection_test
	$(TEST_BUILD_ROOT)/sound_inspection_test

$(TEST_BUILD_ROOT)/sound_inspection_test: tests/sound_inspection_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-mcp-sound-analysis: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sound_analysis_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-midi-authoring
test-midi-authoring: $(TEST_BUILD_ROOT)/midi_authoring_test
	$(TEST_BUILD_ROOT)/midi_authoring_test

$(TEST_BUILD_ROOT)/midi_authoring_test: tests/midi_authoring_test.c src/app/midi_actions.c src/engine/engine_clips_midi.c src/undo/undo_manager_stack.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/app/midi_actions.o $(APP_OBJ_DIR)/src/engine/engine_clips_midi.o $(APP_OBJ_DIR)/src/undo/undo_manager_stack.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-midi
test-mcp-midi: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_midi_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-midi-regions test-mcp-midi-regions
test-midi-regions: $(TEST_BUILD_ROOT)/midi_regions_test
	$(TEST_BUILD_ROOT)/midi_regions_test
$(TEST_BUILD_ROOT)/midi_regions_test: tests/midi_regions_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out %/src/engine/engine_clips.o %/src/engine/engine_tracks.o %/src/undo/undo_manager_stack.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)
test-mcp-midi-regions: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_midi_regions_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-fx-actions
test-fx-actions: $(TEST_BUILD_ROOT)/fx_actions_test
	$(TEST_BUILD_ROOT)/fx_actions_test
$(TEST_BUILD_ROOT)/fx_actions_test: tests/fx_actions_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out %/src/engine/engine_fx_chain.o %/src/undo/undo_manager_stack.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-fx
test-mcp-fx: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_fx_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-composition
test-composition: $(TEST_BUILD_ROOT)/composition_test
	$(TEST_BUILD_ROOT)/composition_test
$(TEST_BUILD_ROOT)/composition_test: tests/composition_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-composition
test-mcp-composition: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_composition_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-fx-library
test-fx-library: $(TEST_BUILD_ROOT)/fx_library_test
	$(TEST_BUILD_ROOT)/fx_library_test
$(TEST_BUILD_ROOT)/fx_library_test: tests/fx_library_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-fx-library
test-mcp-fx-library: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_fx_library_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-fx-library-ui
test-fx-library-ui: $(TEST_BUILD_ROOT)/fx_library_ui_test
	$(TEST_BUILD_ROOT)/fx_library_ui_test
$(TEST_BUILD_ROOT)/fx_library_ui_test: tests/fx_library_ui_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-midi-expression
test-midi-expression: $(TEST_BUILD_ROOT)/midi_expression_test
	$(TEST_BUILD_ROOT)/midi_expression_test
$(TEST_BUILD_ROOT)/midi_expression_test: tests/midi_expression_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-arrangement
test-arrangement: $(TEST_BUILD_ROOT)/arrangement_test
	$(TEST_BUILD_ROOT)/arrangement_test
$(TEST_BUILD_ROOT)/arrangement_test: tests/arrangement_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/engine_clip_history.o $(APP_OBJ_DIR)/src/undo/undo_manager_stack.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-arrangement
test-mcp-arrangement: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_arrangement_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-fx-return-alignment
test-fx-return-alignment: $(TEST_BUILD_ROOT)/fx_return_alignment_test
	$(TEST_BUILD_ROOT)/fx_return_alignment_test
$(TEST_BUILD_ROOT)/fx_return_alignment_test: tests/fx_return_alignment_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/effects/effects_manager.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-engine-routing
test-engine-routing: $(TEST_BUILD_ROOT)/engine_routing_test
	$(TEST_BUILD_ROOT)/engine_routing_test
$(TEST_BUILD_ROOT)/engine_routing_test: tests/engine_routing_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/engine_routing.o $(APP_OBJ_DIR)/src/engine/engine_source_plan.o $(APP_OBJ_DIR)/src/engine/engine_audio.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-routing-actions
test-routing-actions: $(TEST_BUILD_ROOT)/routing_actions_test
	$(TEST_BUILD_ROOT)/routing_actions_test
$(TEST_BUILD_ROOT)/routing_actions_test: tests/routing_actions_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/engine_routing.o $(APP_OBJ_DIR)/src/engine/engine_tracks.o $(APP_OBJ_DIR)/src/undo/undo_manager_stack.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-routing
test-mcp-routing: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_routing_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-routing-ui
test-routing-ui: $(TEST_BUILD_ROOT)/routing_ui_test
	$(TEST_BUILD_ROOT)/routing_ui_test
$(TEST_BUILD_ROOT)/routing_ui_test: tests/routing_ui_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

# Verifies callback-safe aligned listening separately from native production capture/actions.
# Uses the ordinary official SDK against a private paused dummy-audio endpoint.
.PHONY: test-mcp-ab-comparison
test-mcp-ab-comparison: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_ab_comparison_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-comparison-player test-ab-comparison
test-comparison-player: $(TEST_BUILD_ROOT)/comparison_player_test
	$(TEST_BUILD_ROOT)/comparison_player_test
$(TEST_BUILD_ROOT)/comparison_player_test: tests/comparison_player_test.c src/audio/device_comparison.c src/audio/device_preview.c include/audio/audio_device.h
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< -o "$@" $(LDFLAGS)
test-ab-comparison: $(TEST_BUILD_ROOT)/ab_comparison_test
	$(TEST_BUILD_ROOT)/ab_comparison_test
$(TEST_BUILD_ROOT)/ab_comparison_test: tests/ab_comparison_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-audio-preview
test-audio-preview: $(TEST_BUILD_ROOT)/audio_preview_test
	$(TEST_BUILD_ROOT)/audio_preview_test
$(TEST_BUILD_ROOT)/audio_preview_test: tests/audio_preview_test.c src/audio/device_preview.c src/audio/device_comparison.c include/audio/audio_device.h
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< src/audio/device_comparison.c -o "$@" $(LDFLAGS)

.PHONY: test-percussion-render
test-percussion-render: $(TEST_BUILD_ROOT)/percussion_render_test
	$(TEST_BUILD_ROOT)/percussion_render_test

$(TEST_BUILD_ROOT)/percussion_render_test: tests/percussion_render_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-percussion
test-mcp-percussion: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_percussion_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts/percussion

.PHONY: test-instrument-articulation
test-instrument-articulation: $(TEST_BUILD_ROOT)/instrument_articulation_test
	$(TEST_BUILD_ROOT)/instrument_articulation_test
$(TEST_BUILD_ROOT)/instrument_articulation_test: tests/instrument_articulation_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-articulation
test-mcp-articulation: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_articulation_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts/articulation

.PHONY: test-track-hat-choke test-mcp-track-hat-choke
test-track-hat-choke: $(TEST_BUILD_ROOT)/track_hat_choke_test
	$(TEST_BUILD_ROOT)/track_hat_choke_test
$(TEST_BUILD_ROOT)/track_hat_choke_test: tests/track_hat_choke_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)
test-mcp-track-hat-choke: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_track_hat_choke_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts/track-hat-choke

.PHONY: test-midi-note-editing
test-midi-note-editing: $(MIDI_NOTE_EDITING_TEST_BIN)
	$(MIDI_NOTE_EDITING_TEST_BIN)

$(MIDI_NOTE_EDITING_TEST_BIN): $(MIDI_NOTE_EDITING_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(MIDI_NOTE_EDITING_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-automation-model
test-automation-model: $(TEST_BUILD_ROOT)/automation_model_test
	$(TEST_BUILD_ROOT)/automation_model_test

$(TEST_BUILD_ROOT)/automation_model_test: $(TEST_BUILD_ROOT)/automation_model_test.o $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	$(HOST_CC) $(foreach obj,$^,"$(obj)") -o "$@" $(LDFLAGS)

.PHONY: test-mcp-automation
test-mcp-automation: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_automation_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts/automation

.PHONY: test-mcp-sampled-violin test-sampled-violin
test-mcp-sampled-violin: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sampled_violin_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/receipts/sampled-violin

.PHONY: test-sampled-winds test-mcp-sampled-winds
test-sampled-winds: $(TEST_BUILD_ROOT)/sampled_winds_test
	$(TEST_BUILD_ROOT)/sampled_winds_test
$(TEST_BUILD_ROOT)/sampled_winds_test: tests/sampled_winds_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)
test-mcp-sampled-winds: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sampled_winds_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/receipts/sampled-winds

.PHONY: test-sampled-saxophone test-mcp-sampled-saxophone
test-sampled-saxophone: test-sampled-winds
test-mcp-sampled-saxophone: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sampled_winds_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/receipts/sampled-saxophone --saxophone-only

.PHONY: test-sampled-flute-cello test-mcp-sampled-flute-cello
test-sampled-flute-cello: test-sampled-winds
test-mcp-sampled-flute-cello: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sampled_winds_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/receipts/sampled-flute-cello --flute-cello-only

.PHONY: test-sampled-oboe-horn test-mcp-sampled-oboe-horn
test-sampled-oboe-horn: test-sampled-winds
test-mcp-sampled-oboe-horn: $(TEST_BUILD_ROOT)/mcp_control_fixture
	@$(MCP_PYTHON) tests/mcp_sampled_winds_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/mcp-oboe-horn --oboe-horn-only

.PHONY: test-sampled-bassoon-trombone test-mcp-sampled-bassoon-trombone
test-sampled-bassoon-trombone: test-sampled-winds
test-mcp-sampled-bassoon-trombone: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sampled_winds_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/receipts/sampled-bassoon-trombone --bassoon-trombone-only

.PHONY: test-sampled-piano-strings
test-sampled-piano-strings: test-sampled-winds $(TEST_BUILD_ROOT)/sampled_piano_test
	$(TEST_BUILD_ROOT)/sampled_piano_test
$(TEST_BUILD_ROOT)/sampled_piano_test: tests/sampled_piano_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-sampled-piano-strings
test-mcp-sampled-piano-strings: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_sampled_winds_test.py $(TEST_BUILD_ROOT)/mcp_control_fixture $(BUILD_DIR)/receipts/sampled-piano-strings --piano-strings-only

.PHONY: test-piano-expression
test-piano-expression: $(TEST_BUILD_ROOT)/piano_expression_test
	$(TEST_BUILD_ROOT)/piano_expression_test
$(TEST_BUILD_ROOT)/piano_expression_test: tests/piano_expression_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-timeline-navigation
test-timeline-navigation: $(TEST_BUILD_ROOT)/timeline_navigation_test
	$(TEST_BUILD_ROOT)/timeline_navigation_test

$(TEST_BUILD_ROOT)/timeline_navigation_test: tests/timeline_navigation_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-track-groups
test-track-groups: $(TEST_BUILD_ROOT)/track_groups_test
	$(TEST_BUILD_ROOT)/track_groups_test $(TEST_BUILD_ROOT)/groups-roundtrip.json

$(TEST_BUILD_ROOT)/track_groups_test: tests/track_groups_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/engine/group_mix.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: build-track-groups-native-proof
build-track-groups-native-proof: $(TEST_BUILD_ROOT)/track_groups_native_test

$(TEST_BUILD_ROOT)/track_groups_native_test: tests/track_groups_native_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-project-collections build-project-collections-native
test-project-collections: $(TEST_BUILD_ROOT)/project_collections_test
	$(TEST_BUILD_ROOT)/project_collections_test

$(TEST_BUILD_ROOT)/project_collections_test: tests/project_collections_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

build-project-collections-native: $(TEST_BUILD_ROOT)/project_collections_native_test
$(TEST_BUILD_ROOT)/project_collections_native_test: tests/project_collections_native_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-track-ui build-track-ui-native
test-track-ui: $(TEST_BUILD_ROOT)/track_ui_test
	$(TEST_BUILD_ROOT)/track_ui_test
build-track-ui-native: $(TEST_BUILD_ROOT)/track_ui_test
$(TEST_BUILD_ROOT)/track_ui_test: tests/track_ui_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-header-mixer build-header-mixer-native
test-header-mixer: $(TEST_BUILD_ROOT)/header_mixer_test
	$(TEST_BUILD_ROOT)/header_mixer_test
build-header-mixer-native: $(TEST_BUILD_ROOT)/header_mixer_test
$(TEST_BUILD_ROOT)/header_mixer_test: tests/header_mixer_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-track-controls
test-mcp-track-controls: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_track_controls_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-eq-meter test-mcp-eq
test-eq-meter: $(TEST_BUILD_ROOT)/eq_meter_test
	$(TEST_BUILD_ROOT)/eq_meter_test
$(TEST_BUILD_ROOT)/eq_meter_test: tests/eq_meter_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)
test-mcp-eq: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_eq_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

.PHONY: test-text-cache-lifetime build-text-cache-lifetime
test-text-cache-lifetime: $(TEST_BUILD_ROOT)/text_cache_lifetime_test
	$(TEST_BUILD_ROOT)/text_cache_lifetime_test
build-text-cache-lifetime: $(TEST_BUILD_ROOT)/text_cache_lifetime_test
$(TEST_BUILD_ROOT)/text_cache_lifetime_test: tests/text_cache_lifetime_test.c src/ui/text_draw.c
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $^ -o "$@" $(LDFLAGS)

.PHONY: build-dense-text-native
build-dense-text-native: $(TEST_BUILD_ROOT)/dense_text_native_test
$(TEST_BUILD_ROOT)/dense_text_native_test: tests/dense_text_native_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mix-capture
test-mix-capture: $(TEST_BUILD_ROOT)/mix_capture_test
	$(TEST_BUILD_ROOT)/mix_capture_test
$(TEST_BUILD_ROOT)/mix_capture_test: tests/mix_capture_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mcp-mix-capture
test-mcp-mix-capture: $(TEST_BUILD_ROOT)/mcp_control_fixture
	$(MCP_PYTHON) tests/mcp_mix_capture_test.py --fixture $(TEST_BUILD_ROOT)/mcp_control_fixture --output-root $(BUILD_DIR)/receipts

# Builds the independently driven standards/reference qualification harness.
.PHONY: test-loudness-analysis
test-loudness-analysis: $(TEST_BUILD_ROOT)/loudness_analysis_test
	$(MCP_PYTHON) tests/loudness_analysis_test.py --binary $(TEST_BUILD_ROOT)/loudness_analysis_test --output-root $(BUILD_DIR)/receipts/loudness
$(TEST_BUILD_ROOT)/loudness_analysis_test: tests/loudness_analysis_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

# Qualifies exact insert curves through native editing and the production render graph.
.PHONY: test-effect-automation
test-effect-automation: $(TEST_BUILD_ROOT)/effect_parameter_automation_test
	$(TEST_BUILD_ROOT)/effect_parameter_automation_test
$(TEST_BUILD_ROOT)/effect_parameter_automation_test: tests/effect_parameter_automation_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: test-mixer-automation
test-mixer-automation: $(TEST_BUILD_ROOT)/mixer_automation_test
	$(TEST_BUILD_ROOT)/mixer_automation_test
$(TEST_BUILD_ROOT)/mixer_automation_test: tests/mixer_automation_test.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

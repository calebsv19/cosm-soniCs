test:
	@$(MAKE) BUILD_TOOLCHAIN="$(TEST_TOOLCHAIN)" run-headless-smoke
	@$(MAKE) BUILD_TOOLCHAIN="$(TEST_TOOLCHAIN)" test-stable

test-stable:
	@$(MAKE) $(STABLE_TEST_TARGETS)
	@echo "daw stable test lane passed"

.PHONY: test-audio-output-device
test-audio-output-device: $(TEST_BUILD_ROOT)/audio_output_device_test
	$(TEST_BUILD_ROOT)/audio_output_device_test

$(TEST_BUILD_ROOT)/audio_output_device_test: tests/audio_output_device_test.c src/audio/device_sdl.c include/audio/audio_device.h
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< -o "$@" $(LDFLAGS)

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
	$(APP_OBJ_DIR)/src/session/save_file.o \
	$(APP_OBJ_DIR)/src/session/session_document.o \
	$(APP_OBJ_DIR)/src/session/session_validation.o \
	$(APP_OBJ_DIR)/src/session/session_io_write.o \
	$(APP_OBJ_DIR)/src/session/session_io_read.o \
	$(APP_OBJ_DIR)/src/session/session_io_json.o \
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
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(filter-out $(APP_OBJ_DIR)/src/undo/undo_manager_stack.o $(APP_OBJ_DIR)/src/engine/engine_tracks.o $(APP_OBJ_DIR)/src/engine/engine_fx.o $(APP_OBJ_DIR)/src/engine/engine_clips.o $(APP_OBJ_DIR)/src/engine/engine_clip_history.o $(APP_OBJ_DIR)/src/engine/engine_scope_host.o $(APP_OBJ_DIR)/src/engine/automation.o $(APP_OBJ_DIR)/src/engine/sampler.o $(APP_OBJ_DIR)/src/engine/engine_clips_automation.o $(APP_OBJ_DIR)/src/engine/engine_clips_midi.o $(APP_OBJ_DIR)/src/engine/midi.o $(APP_OBJ_DIR)/src/engine/engine_clips_no_overlap.o,$(ENGINE_TEST_SUPPORT_OBJS)),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

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

EXPORT_RENDER_SOURCES := src/engine/engine_io.c src/engine/engine_source_plan.c src/engine/instrument_osc.c src/engine/sampler.c src/engine/automation.c src/engine/engine_eq.c src/engine/graph.c src/engine/buffer_pool.c src/effects/effects_manager.c src/effects/basics/fx_gain.c src/effects/dynamics/fx_limiter.c src/effects/delay/fx_delay_simple.c
# Inject spool failures only into the export test's private engine I/O object.
$(TEST_BUILD_ROOT)/instrumented/engine/engine_io.o: CPPFLAGS += -Dtmpfile=daw_test_tmpfile -Dfwrite=daw_test_fwrite -Dfflush=daw_test_fflush -Dfseek=daw_test_fseek -Dfread=daw_test_fread -Dfclose=daw_test_fclose

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
-include $(addprefix $(TEST_BUILD_ROOT)/,media_preparation_test.d media_jobs_test.d media_import_test.d media_ffmpeg_test.d)

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

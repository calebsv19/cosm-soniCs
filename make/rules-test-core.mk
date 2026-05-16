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
	$(APP_OBJ_DIR)/src/engine/midi.o \
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
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(AUDIO_RECORDING_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

test-smoke: $(SMOKE_TEST_BIN)
	$(SMOKE_TEST_BIN)

$(SMOKE_TEST_BIN): $(SMOKE_TEST_OBJS) $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(SMOKE_TEST_OBJS) $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

$(TEST_BUILD_ROOT)/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -c "$<" -o "$@"

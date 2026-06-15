# =========================
#  fisiCs memory-check audit
# =========================

MEMORY_CHECK_FISICS_OVERLAY := physics-units,memory-check
MEMORY_CHECK_REPORT_DIR := build/memory_check
MEMORY_CHECK_STDOUT := $(MEMORY_CHECK_REPORT_DIR)/daw.stdout
MEMORY_CHECK_STDERR := $(MEMORY_CHECK_REPORT_DIR)/daw.stderr
MEMORY_CHECK_OBJ_DIR := $(TARGET_BUILD_ROOT)/toolchains/fisics/memory_check_obj
MEMORY_CHECK_BIN := $(TARGET_BUILD_ROOT)/toolchains/fisics/bin/daw_memory_check_session_test
MEMORY_CHECK_SRCS := \
	tests/session_serialization_test.c \
	src/session/session_document.c \
	src/session/session_validation.c \
	src/session/session_io_write.c \
	src/session/session_io_read.c \
	src/session/session_io_json.c \
	src/session/session_io_read_parse.c \
	src/session/session_io_read_parse_document.c \
	src/session/session_io_read_parse_engine.c \
	src/session/session_io_read_parse_effects_panel.c \
	src/session/session_io_read_parse_master_fx.c \
	src/session/session_io_read_parse_track_clips.c \
	src/session/session_io_read_parse_track_fx.c \
	src/engine/midi.c \
	src/config/config.c
MEMORY_CHECK_OBJS := $(patsubst %.c,$(MEMORY_CHECK_OBJ_DIR)/%.o,$(MEMORY_CHECK_SRCS))
MEMORY_CHECK_REPORT_POLICY ?= always
FISICS_MEMCHECK_RUNTIME ?= /Users/calebsv/Desktop/CodeWork/fisiCs/build/unsanitized/libfisics_memcheck_runtime.a

$(MEMORY_CHECK_OBJ_DIR)/%.o: %.c $(COMPILER_STAMP)
	@mkdir -p "$(dir $@)"
	$(APP_CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(MEMORY_CHECK_BIN): $(MEMORY_CHECK_OBJS) $(CORE_PACK_LIB) $(CORE_IO_LIB) $(CORE_BASE_LIB)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(foreach obj,$(MEMORY_CHECK_OBJS),"$(obj)") "$(CORE_PACK_LIB)" "$(CORE_IO_LIB)" "$(CORE_BASE_LIB)" "$(FISICS_MEMCHECK_RUNTIME)" -o "$@" $(LDFLAGS)

memory-check-build:
	@$(MAKE) BUILD_TOOLCHAIN=fisics APP_CC="$(FISICS_CC) --overlay=$(MEMORY_CHECK_FISICS_OVERLAY)" ARCH_FLAGS= -B "$(MEMORY_CHECK_BIN)"

memory-check-run: memory-check-build
	@mkdir -p "$(MEMORY_CHECK_REPORT_DIR)"
	FISICS_MEMCHECK_REPORT="$(MEMORY_CHECK_REPORT_POLICY)" "$(MEMORY_CHECK_BIN)" > "$(MEMORY_CHECK_STDOUT)" 2> "$(MEMORY_CHECK_STDERR)"
	@echo "memory-check stdout: $(MEMORY_CHECK_STDOUT)"
	@echo "memory-check stderr: $(MEMORY_CHECK_STDERR)"

memory-check-audit: memory-check-run
	@echo "memory-check summary:"
	@grep -E "\\[fisics:memory-check\\] (summary|leak|double free|unknown pointer free)" "$(MEMORY_CHECK_STDERR)" || true

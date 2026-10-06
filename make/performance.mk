# Opt-in measurement build; separate object roots prevent optimized/default object mixing.
PERF_OPT_LEVEL ?= 2
override BUILD_DIR := build/performance/O$(PERF_OPT_LEVEL)
include Makefile
CFLAGS += -O$(PERF_OPT_LEVEL) -g

.PHONY: performance-build
performance-build: $(TEST_BUILD_ROOT)/runtime_workload_bench

$(TEST_BUILD_ROOT)/runtime_workload_bench: tests/performance/runtime_workload_bench.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: performance-media-import
performance-media-import: $(TEST_BUILD_ROOT)/media_import_bench

$(TEST_BUILD_ROOT)/media_import_bench: tests/performance/media_import_bench.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: performance-analysis
performance-analysis: $(TEST_BUILD_ROOT)/analysis_compute_bench

$(TEST_BUILD_ROOT)/analysis_compute_bench: tests/performance/analysis_compute_bench.c include/engine/analysis_math.h
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< -o "$@" -lm

.PHONY: performance-sustained
performance-sustained: $(TEST_BUILD_ROOT)/sustained_acceptance_bench

$(TEST_BUILD_ROOT)/sustained_acceptance_bench: tests/performance/sustained_acceptance_bench.c tests/performance/runtime_workload_bench.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: performance-lifecycle
performance-lifecycle: $(TEST_BUILD_ROOT)/lifecycle_acceptance_bench

$(TEST_BUILD_ROOT)/lifecycle_acceptance_bench: tests/performance/lifecycle_acceptance_bench.c tests/performance/sustained_acceptance_bench.c tests/performance/runtime_workload_bench.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

.PHONY: performance-device
performance-device: $(TEST_BUILD_ROOT)/device_acceptance_bench

$(TEST_BUILD_ROOT)/device_acceptance_bench: tests/performance/device_acceptance_bench.c $(ENGINE_TEST_SUPPORT_OBJS) $(APP_SHARED_LIBS)
	@mkdir -p "$(dir $@)"
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) $< $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o "$@" $(LDFLAGS)

audit-probe:
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) docs/audits/2026-09-19/runtime_probe.c $(foreach obj,$(ENGINE_TEST_SUPPORT_OBJS),"$(obj)") $(APP_SHARED_LIBS) -o tmp/runtime_audit_20260919/probe $(LDFLAGS)
	tmp/runtime_audit_20260919/probe

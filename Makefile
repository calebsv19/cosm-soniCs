include make/config.mk
include make/target.mk
include make/shared.mk
include make/paths.mk
include make/sources-app.mk
include make/objects.mk
include make/flags.mk
include make/shared-libs.mk

APP_BIN := $(APP_BIN_DIR)/$(APP_NAME)
include make/sources-tests.mk
include make/phony.mk

include make/rules-build.mk
include make/rules-runtime.mk
include make/package-macos.mk
include make/release.mk
include make/rules-test-core.mk
include make/rules-test-adapters.mk
include make/rules-loop.mk

-include $(ALL_DEPS)

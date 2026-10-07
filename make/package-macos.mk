PACKAGE_BIN = $(TARGET_BUILD_ROOT)/toolchains/$(PACKAGE_TOOLCHAIN)/bin/$(APP_NAME)
DIST_DIR ?= $(TARGET_BUILD_ROOT)/dist
PACKAGE_APP_NAME ?= soniCs.app
PACKAGE_DISPLAY_NAME ?= soniCs
PACKAGE_BUNDLE_ID ?= com.cosm.sonics
PACKAGE_PROFILE ?= standard
PACKAGE_RUNTIME_NAMESPACE ?= DAW
PACKAGE_LOG_NAMESPACE ?= DAW
PACKAGE_PROGRAM_VERSION ?= $(shell tr -d '[:space:]' < VERSION)
PACKAGE_BUILD_LABEL ?= soniCs-$(PACKAGE_PROGRAM_VERSION)
PACKAGE_EMBED_BUILD_IDENTITY ?= 0
PACKAGE_APP_DIR = $(DIST_DIR)/$(PACKAGE_APP_NAME)
PACKAGE_CONTENTS_DIR = $(PACKAGE_APP_DIR)/Contents
PACKAGE_MACOS_DIR = $(PACKAGE_CONTENTS_DIR)/MacOS
PACKAGE_RESOURCES_DIR = $(PACKAGE_CONTENTS_DIR)/Resources
PACKAGE_FRAMEWORKS_DIR = $(PACKAGE_CONTENTS_DIR)/Frameworks
PACKAGE_INFO_PLIST_SRC := tools/packaging/macos/Info.plist
PACKAGE_LAUNCHER_SRC := tools/packaging/macos/daw-launcher
PACKAGE_DYLIB_BUNDLER := tools/packaging/macos/bundle-dylibs.sh
PACKAGE_APP_ICON_NAME := AppIcon
PACKAGE_APP_ICON_FILE := $(PACKAGE_APP_ICON_NAME).icns
PACKAGE_LOCAL_ICON_DIR := tools/packaging/macos/local_app_icon
PACKAGE_APP_ICON_SRC ?= $(PACKAGE_LOCAL_ICON_DIR)/$(PACKAGE_APP_ICON_FILE)
PACKAGE_APP_ICONSET_SRC ?= $(PACKAGE_LOCAL_ICON_DIR)/$(PACKAGE_APP_ICON_NAME).iconset
PACKAGE_BUNDLED_ICON_PATH := $(PACKAGE_RESOURCES_DIR)/$(PACKAGE_APP_ICON_FILE)
DESKTOP_APP_DIR ?= $(HOME)/Desktop/$(PACKAGE_APP_NAME)
PACKAGE_ADHOC_SIGN_IDENTITY ?= -
PACKAGE_DESTRUCTIVE_DESTINATION_GUARD := tools/packaging/macos/guard-destructive-destination.sh
CODEWORK_WORKSPACE_ROOT := $(abspath $(shell git rev-parse --path-format=absolute --git-common-dir)/../..)
MEW1_TOOL ?= $(CODEWORK_WORKSPACE_ROOT)/shared/scripts/mew1/mew1.py
MAIN_EDIT_DIST_DIR := $(TARGET_BUILD_ROOT)/dist/dev/main-edit
MAIN_EDIT_APP_NAME := soniCs Main Edit.app
MAIN_EDIT_DISPLAY_NAME := soniCs Main Edit
MAIN_EDIT_BUNDLE_ID := com.cosm.sonics.main-edit
MAIN_EDIT_RUNTIME_NAMESPACE := DAW-Main-Edit
MAIN_EDIT_LOG_NAMESPACE := DAW-Main-Edit
MAIN_EDIT_PROFILE := main-edit
MAIN_EDIT_BUILD_LABEL := soniCs-main-edit-$(PACKAGE_PROGRAM_VERSION)
MAIN_EDIT_APP_DIR := $(MAIN_EDIT_DIST_DIR)/$(MAIN_EDIT_APP_NAME)
MAIN_EDIT_DESKTOP_APP_DIR ?= $(HOME)/Desktop/$(MAIN_EDIT_APP_NAME)
MAIN_EDIT_PROCESS_RECEIPT := $(TARGET_BUILD_ROOT)/receipts/mew1/process-audit.json

package-build-lane:
	@$(MAKE) BUILD_TOOLCHAIN="$(PACKAGE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" "$(PACKAGE_BIN)"

package-desktop: package-build-lane
	@echo "Preparing desktop package..."
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" package_app "$(PACKAGE_APP_DIR)" "$(PACKAGE_APP_NAME)"
	@rm -rf "$(PACKAGE_APP_DIR)"
	@mkdir -p "$(PACKAGE_MACOS_DIR)" "$(PACKAGE_RESOURCES_DIR)" "$(PACKAGE_FRAMEWORKS_DIR)"
	@cp "$(PACKAGE_INFO_PLIST_SRC)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $(PACKAGE_BUNDLE_ID)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Set :CFBundleName $(PACKAGE_DISPLAY_NAME)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string $(PACKAGE_DISPLAY_NAME)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(PACKAGE_PROGRAM_VERSION)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(PACKAGE_PROGRAM_VERSION)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Add :SoniCsPackageProfile string $(PACKAGE_PROFILE)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Add :SoniCsRuntimeNamespace string $(PACKAGE_RUNTIME_NAMESPACE)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Add :SoniCsLogNamespace string $(PACKAGE_LOG_NAMESPACE)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@/usr/libexec/PlistBuddy -c "Add :SoniCsBuildLabel string $(PACKAGE_BUILD_LABEL)" "$(PACKAGE_CONTENTS_DIR)/Info.plist"
	@cp "$(PACKAGE_BIN)" "$(PACKAGE_MACOS_DIR)/daw-bin"
	@cp "$(PACKAGE_LAUNCHER_SRC)" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@chmod +x "$(PACKAGE_MACOS_DIR)/daw-bin" "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@if [ -f "$(PACKAGE_APP_ICON_SRC)" ]; then \
		cp "$(PACKAGE_APP_ICON_SRC)" "$(PACKAGE_BUNDLED_ICON_PATH)"; \
		echo "Bundled app icon from $(PACKAGE_APP_ICON_SRC)"; \
	elif [ -d "$(PACKAGE_APP_ICONSET_SRC)" ]; then \
		/usr/bin/iconutil -c icns -o "$(PACKAGE_BUNDLED_ICON_PATH)" "$(PACKAGE_APP_ICONSET_SRC)" || exit 1; \
		echo "Bundled app icon from $(PACKAGE_APP_ICONSET_SRC)"; \
	else \
		echo "warning: no app icon source found at $(PACKAGE_APP_ICON_SRC) or $(PACKAGE_APP_ICONSET_SRC)"; \
	fi
	@PACKAGE_DEP_SEARCH_ROOTS="$(TARGET_DEP_SEARCH_ROOTS)" "$(PACKAGE_DYLIB_BUNDLER)" "$(PACKAGE_MACOS_DIR)/daw-bin" "$(PACKAGE_FRAMEWORKS_DIR)"
	@mkdir -p "$(PACKAGE_RESOURCES_DIR)/config/templates" "$(PACKAGE_RESOURCES_DIR)/assets/audio" "$(PACKAGE_RESOURCES_DIR)/include" "$(PACKAGE_RESOURCES_DIR)/shared/assets" "$(PACKAGE_RESOURCES_DIR)/vk_renderer" "$(PACKAGE_RESOURCES_DIR)/shaders"
	@cp config/engine.cfg "$(PACKAGE_RESOURCES_DIR)/config/engine.cfg"
	@cp config/timer_hud_settings.json "$(PACKAGE_RESOURCES_DIR)/config/timer_hud_settings.json"
	@cp config/theme_preset.txt "$(PACKAGE_RESOURCES_DIR)/config/theme_preset.txt"
	@cp config/README.md "$(PACKAGE_RESOURCES_DIR)/config/README.md"
	@cp config/templates/public_default_project.json "$(PACKAGE_RESOURCES_DIR)/config/templates/public_default_project.json"
	@cp assets/audio/README.md "$(PACKAGE_RESOURCES_DIR)/assets/audio/README.md"
	@cp -R include/fonts "$(PACKAGE_RESOURCES_DIR)/include/"
	@cp -R "$(SHARED_ROOT)/assets/fonts" "$(PACKAGE_RESOURCES_DIR)/shared/assets/"
	@cp -R "$(VK_RENDERER_DIR)/shaders" "$(PACKAGE_RESOURCES_DIR)/vk_renderer/"
	@cp -R "$(VK_RENDERER_DIR)/shaders/." "$(PACKAGE_RESOURCES_DIR)/shaders/"
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$$dylib"; \
	done
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-bin"
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-launcher"
	@if [ "$(PACKAGE_EMBED_BUILD_IDENTITY)" = "1" ]; then \
		python3 "$(MEW1_TOOL)" write-identity \
			--output "$(PACKAGE_RESOURCES_DIR)/build_identity.json" \
			--source-root "$(CURDIR)" \
			--binary "$(PACKAGE_MACOS_DIR)/daw-bin" \
			--profile "$(PACKAGE_PROFILE)" \
			--program daw \
			--product soniCs \
			--version "$(PACKAGE_PROGRAM_VERSION)" \
			--architecture "$(TARGET_ARCH)" \
			--toolchain "$(PACKAGE_TOOLCHAIN)" \
			--build-label "$(PACKAGE_BUILD_LABEL)"; \
	fi
	@codesign --force --sign "$(PACKAGE_ADHOC_SIGN_IDENTITY)" --timestamp=none "$(PACKAGE_APP_DIR)"
	@codesign --verify --deep --strict "$(PACKAGE_APP_DIR)"
	@echo "Desktop package ready: $(PACKAGE_APP_DIR)"

package-desktop-smoke: package-desktop
	@test -x "$(PACKAGE_MACOS_DIR)/daw-launcher" || (echo "Missing launcher"; exit 1)
	@test -x "$(PACKAGE_MACOS_DIR)/daw-bin" || (echo "Missing daw-bin"; exit 1)
	@test -f "$(PACKAGE_CONTENTS_DIR)/Info.plist" || (echo "Missing Info.plist"; exit 1)
	@test -f "$(PACKAGE_FRAMEWORKS_DIR)/libvulkan.1.dylib" || (echo "Missing bundled libvulkan"; exit 1)
	@test -f "$(PACKAGE_FRAMEWORKS_DIR)/libMoltenVK.dylib" || (echo "Missing bundled libMoltenVK"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/config/engine.cfg" || (echo "Missing config/engine.cfg"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/config/templates/public_default_project.json" || (echo "Missing public default project template"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/runtime" || (echo "Bundled generated config/runtime"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/last_session.json" || (echo "Bundled local last_session.json"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/projects" || (echo "Bundled local project state"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/library_index.json" || (echo "Bundled local library index"; exit 1)
	@if [ -f "$(PACKAGE_APP_ICON_SRC)" ] || [ -d "$(PACKAGE_APP_ICONSET_SRC)" ]; then \
		test -f "$(PACKAGE_BUNDLED_ICON_PATH)" || (echo "Missing bundled AppIcon.icns"; exit 1); \
	fi
	@test -f "$(PACKAGE_RESOURCES_DIR)/assets/audio/README.md" || (echo "Missing bundled audio README"; exit 1)
	@extra_audio="$$(find "$(PACKAGE_RESOURCES_DIR)/assets/audio" -type f ! -name README.md -print -quit)"; \
	test -z "$$extra_audio" || (echo "Bundled local user audio: $$extra_audio"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/include/fonts/Montserrat/Montserrat-Regular.ttf" || (echo "Missing bundled Montserrat"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/vk_renderer/shaders/textured.vert.spv" || (echo "Missing bundled vk shaders"; exit 1)
	@test -f "$(PACKAGE_RESOURCES_DIR)/shaders/textured.vert.spv" || (echo "Missing bundled runtime shader"; exit 1)
	@actual_archs="$$(/usr/bin/lipo -archs "$(PACKAGE_MACOS_DIR)/daw-bin" 2>/dev/null || true)"; \
	printf '%s\n' "$$actual_archs" | /usr/bin/grep -qw "$(TARGET_ARCH)" || (echo "Unexpected app binary archs: $$actual_archs"; exit 1)
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		dylib_archs="$$(/usr/bin/lipo -archs "$$dylib" 2>/dev/null || true)"; \
		printf '%s\n' "$$dylib_archs" | /usr/bin/grep -qw "$(TARGET_ARCH)" || (echo "Unexpected dylib archs for $$dylib: $$dylib_archs"; exit 1); \
	done
	@echo "package-desktop-smoke passed."

package-desktop-self-test: package-desktop-smoke
	@"$(PACKAGE_MACOS_DIR)/daw-launcher" --self-test || (echo "package-desktop self-test failed."; exit 1)
	@echo "package-desktop-self-test passed."

package-desktop-copy-desktop: package-desktop
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" desktop_app "$(DESKTOP_APP_DIR)" "$(PACKAGE_APP_NAME)"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@ditto "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@echo "Copied $(PACKAGE_APP_NAME) to $(DESKTOP_APP_DIR)"

package-desktop-sync: package-desktop-copy-desktop
	@echo "Desktop app sync complete."

package-desktop-open: package-desktop
	@open "$(PACKAGE_APP_DIR)"

package-desktop-remove:
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" desktop_app "$(DESKTOP_APP_DIR)" "$(PACKAGE_APP_NAME)"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@echo "Removed desktop copy at $(DESKTOP_APP_DIR)"

package-desktop-refresh: package-desktop
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" desktop_app "$(DESKTOP_APP_DIR)" "$(PACKAGE_APP_NAME)"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@ditto "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@echo "Refreshed $(PACKAGE_APP_NAME) at $(DESKTOP_APP_DIR)"

package-desktop-main-edit:
	@test -s "$(PACKAGE_APP_ICON_SRC)" || test -d "$(PACKAGE_APP_ICONSET_SRC)" || (echo "Missing Main Edit icon input: $(PACKAGE_APP_ICON_SRC)"; exit 1)
	@test -f "$(MEW1_TOOL)" || (echo "Missing shared MEW1 helper: $(MEW1_TOOL)"; exit 1)
	@before="$$(python3 "$(MEW1_TOOL)" fingerprint --repo "$(CURDIR)")"; \
	$(MAKE) package-desktop-smoke \
		DIST_DIR="$(MAIN_EDIT_DIST_DIR)" \
		PACKAGE_APP_NAME="$(MAIN_EDIT_APP_NAME)" \
		PACKAGE_DISPLAY_NAME="$(MAIN_EDIT_DISPLAY_NAME)" \
		PACKAGE_BUNDLE_ID="$(MAIN_EDIT_BUNDLE_ID)" \
		PACKAGE_PROFILE="$(MAIN_EDIT_PROFILE)" \
		PACKAGE_RUNTIME_NAMESPACE="$(MAIN_EDIT_RUNTIME_NAMESPACE)" \
		PACKAGE_LOG_NAMESPACE="$(MAIN_EDIT_LOG_NAMESPACE)" \
		PACKAGE_BUILD_LABEL="$(MAIN_EDIT_BUILD_LABEL)" \
		PACKAGE_EMBED_BUILD_IDENTITY=1 || exit 1; \
	after="$$(python3 "$(MEW1_TOOL)" fingerprint --repo "$(CURDIR)")"; \
	if [ "$$before" != "$$after" ]; then \
		"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" package_app "$(MAIN_EDIT_APP_DIR)" "$(MAIN_EDIT_APP_NAME)"; \
		rm -rf "$(MAIN_EDIT_APP_DIR)"; \
		echo "Source changed during Main Edit packaging; discarded generated package."; \
		exit 1; \
	fi
	@echo "Main Edit desktop package ready: $(MAIN_EDIT_APP_DIR)"

package-desktop-main-edit-self-test: package-desktop-main-edit
	@test -s "$(MAIN_EDIT_APP_DIR)/Contents/Resources/$(PACKAGE_APP_ICON_FILE)" || (echo "Missing bundled Main Edit icon"; exit 1)
	@test "$$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$(MAIN_EDIT_APP_DIR)/Contents/Info.plist")" = "$(PACKAGE_APP_ICON_NAME)" || (echo "Main Edit icon metadata mismatch"; exit 1)
	@if [ -f "$(PACKAGE_APP_ICON_SRC)" ]; then cmp "$(PACKAGE_APP_ICON_SRC)" "$(MAIN_EDIT_APP_DIR)/Contents/Resources/$(PACKAGE_APP_ICON_FILE)" || exit 1; fi
	@test "$$("/usr/libexec/PlistBuddy" -c 'Print :CFBundleIdentifier' "$(MAIN_EDIT_APP_DIR)/Contents/Info.plist")" = "$(MAIN_EDIT_BUNDLE_ID)"
	@test "$$("/usr/libexec/PlistBuddy" -c 'Print :CFBundleDisplayName' "$(MAIN_EDIT_APP_DIR)/Contents/Info.plist")" = "$(MAIN_EDIT_DISPLAY_NAME)"
	@test "$$("/usr/libexec/PlistBuddy" -c 'Print :SoniCsPackageProfile' "$(MAIN_EDIT_APP_DIR)/Contents/Info.plist")" = "$(MAIN_EDIT_PROFILE)"
	@test "$$("/usr/libexec/PlistBuddy" -c 'Print :SoniCsRuntimeNamespace' "$(MAIN_EDIT_APP_DIR)/Contents/Info.plist")" = "$(MAIN_EDIT_RUNTIME_NAMESPACE)"
	@test "$$("/usr/libexec/PlistBuddy" -c 'Print :SoniCsLogNamespace' "$(MAIN_EDIT_APP_DIR)/Contents/Info.plist")" = "$(MAIN_EDIT_LOG_NAMESPACE)"
	@test -f "$(MAIN_EDIT_APP_DIR)/Contents/Resources/build_identity.json"
	@python3 "$(MEW1_TOOL)" verify-identity \
		--identity "$(MAIN_EDIT_APP_DIR)/Contents/Resources/build_identity.json" \
		--source-root "$(CURDIR)" \
		--binary "$(MAIN_EDIT_APP_DIR)/Contents/MacOS/daw-bin" \
		--profile "$(MAIN_EDIT_PROFILE)" \
		--program daw \
		--product soniCs \
		--version "$(PACKAGE_PROGRAM_VERSION)"
	@fake_home="$(TARGET_BUILD_ROOT)/package-main-edit-self-test/home"; \
	rm -rf "$$fake_home"; mkdir -p "$$fake_home"; \
	HOME="$$fake_home" "$(MAIN_EDIT_APP_DIR)/Contents/MacOS/daw-launcher" --self-test; \
	config="$$(HOME="$$fake_home" "$(MAIN_EDIT_APP_DIR)/Contents/MacOS/daw-launcher" --print-config)"; \
	printf '%s\n' "$$config"; \
	printf '%s\n' "$$config" | grep -Fqx "DAW_PACKAGE_PROFILE=$(MAIN_EDIT_PROFILE)"; \
	printf '%s\n' "$$config" | grep -Fqx "DAW_RUNTIME_NAMESPACE=$(MAIN_EDIT_RUNTIME_NAMESPACE)"; \
	printf '%s\n' "$$config" | grep -Fqx "DAW_LOG_NAMESPACE=$(MAIN_EDIT_LOG_NAMESPACE)"
	@codesign --verify --deep --strict "$(MAIN_EDIT_APP_DIR)"
	@echo "package-desktop-main-edit-self-test passed."

package-desktop-main-edit-refresh: package-desktop-main-edit-self-test
	@mkdir -p "$(dir $(MAIN_EDIT_PROCESS_RECEIPT))"
	@python3 "$(MEW1_TOOL)" process-audit --match "$(MAIN_EDIT_DISPLAY_NAME)" --path "$(MAIN_EDIT_DESKTOP_APP_DIR)" > "$(MAIN_EDIT_PROCESS_RECEIPT)"
	@if grep -Fq '"running": true' "$(MAIN_EDIT_PROCESS_RECEIPT)"; then \
		echo "Refusing to replace a running $(MAIN_EDIT_APP_NAME); process receipt: $(MAIN_EDIT_PROCESS_RECEIPT)"; \
		exit 1; \
	fi
	@mkdir -p "$$(dirname "$(MAIN_EDIT_DESKTOP_APP_DIR)")"
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" desktop_app "$(MAIN_EDIT_DESKTOP_APP_DIR)" "$(MAIN_EDIT_APP_NAME)"
	@rm -rf "$(MAIN_EDIT_DESKTOP_APP_DIR)"
	@ditto "$(MAIN_EDIT_APP_DIR)" "$(MAIN_EDIT_DESKTOP_APP_DIR)"
	@echo "Refreshed $(MAIN_EDIT_APP_NAME) at $(MAIN_EDIT_DESKTOP_APP_DIR)"

package-desktop-main-edit-open: package-desktop-main-edit-refresh
	@open "$(MAIN_EDIT_DESKTOP_APP_DIR)"

RELEASE_VERSION_FILE ?= VERSION
RELEASE_VERSION ?= $(strip $(shell cat "$(RELEASE_VERSION_FILE)" 2>/dev/null))
ifeq ($(RELEASE_VERSION),)
RELEASE_VERSION := 0.1.0
endif
RELEASE_CHANNEL ?= stable
RELEASE_PRODUCT_NAME := soniCs
RELEASE_PROGRAM_KEY := daw
RELEASE_BUNDLE_ID := com.cosm.sonics
RELEASE_ARTIFACT_BASENAME := $(RELEASE_PRODUCT_NAME)-$(RELEASE_VERSION)-$(RELEASE_PLATFORM)-$(RELEASE_ARCH)-$(RELEASE_CHANNEL)
RELEASE_ROOT ?= build/release
RELEASE_DIR = $(RELEASE_ROOT)
# Explicit authentication roots isolate both the app bundle and final artifacts.
ifneq ($(filter command line environment,$(origin RELEASE_ROOT)),)
DIST_DIR := $(RELEASE_ROOT)/package
endif
RELEASE_APP_ZIP := $(RELEASE_DIR)/$(RELEASE_ARTIFACT_BASENAME).zip
RELEASE_MANIFEST := $(RELEASE_DIR)/$(RELEASE_ARTIFACT_BASENAME).manifest.txt
RELEASE_CODESIGN_IDENTITY ?= $(if $(strip $(APPLE_SIGN_IDENTITY)),$(APPLE_SIGN_IDENTITY),$(PACKAGE_ADHOC_SIGN_IDENTITY))
APPLE_SIGN_IDENTITY ?=
APPLE_NOTARY_PROFILE ?=
APPLE_TEAM_ID ?=
STAPLE_MAX_ATTEMPTS ?= 6
STAPLE_RETRY_DELAY_SEC ?= 15

release-contract:
	@echo "HOST_ARCH=$(HOST_ARCH)"
	@echo "TARGET_OS=$(TARGET_OS)"
	@echo "TARGET_ARCH=$(TARGET_ARCH)"
	@echo "TARGET_VARIANT=$(TARGET_VARIANT)"
	@echo "TARGET_TRIPLE=$(TARGET_TRIPLE)"
	@echo "RELEASE_PLATFORM=$(RELEASE_PLATFORM)"
	@echo "RELEASE_ARCH=$(RELEASE_ARCH)"
	@echo "TARGET_HOMEBREW_PREFIX=$(TARGET_HOMEBREW_PREFIX)"
	@echo "TARGET_PKG_CONFIG_LIBDIR=$(TARGET_PKG_CONFIG_LIBDIR)"
	@echo "RELEASE_PROGRAM_KEY=$(RELEASE_PROGRAM_KEY)"
	@echo "RELEASE_PRODUCT_NAME=$(RELEASE_PRODUCT_NAME)"
	@echo "RELEASE_BUNDLE_ID=$(RELEASE_BUNDLE_ID)"
	@echo "RELEASE_VERSION=$(RELEASE_VERSION)"
	@echo "RELEASE_CHANNEL=$(RELEASE_CHANNEL)"
	@test "$(RELEASE_PRODUCT_NAME)" = "soniCs" || (echo "Unexpected release product"; exit 1)
	@test "$(RELEASE_PROGRAM_KEY)" = "daw" || (echo "Unexpected release program key"; exit 1)
	@test "$(RELEASE_BUNDLE_ID)" = "com.cosm.sonics" || (echo "Unexpected release bundle id"; exit 1)
	@test -f "$(RELEASE_VERSION_FILE)" || (echo "Missing VERSION file"; exit 1)
	@echo "release-contract passed."

release-clean:
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" release_dir "$(RELEASE_DIR)" release
	@rm -rf "$(RELEASE_DIR)"
	@echo "release-clean complete."

release-build:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-build-internal

release-build-internal:
	@$(MAKE) release-package-self-test
	@echo "release-build complete."

release-bundle-audit:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-bundle-audit-internal

release-bundle-audit-internal: release-build-internal
	@mkdir -p "$(RELEASE_DIR)"
	@/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$(PACKAGE_CONTENTS_DIR)/Info.plist" > "$(RELEASE_DIR)/bundle_id.txt"
	@test "$$(cat "$(RELEASE_DIR)/bundle_id.txt")" = "$(RELEASE_BUNDLE_ID)" || (echo "Bundle identifier mismatch"; exit 1)
	@otool -L "$(PACKAGE_MACOS_DIR)/daw-bin" > "$(RELEASE_DIR)/otool_daw_bin.txt"
	@for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		out="$(RELEASE_DIR)/otool_$$(basename "$$dylib").txt"; \
		otool -L "$$dylib" > "$$out"; \
	done
	@! rg -q '^[[:space:]]+/(opt/homebrew|usr/local|Users)/' "$(RELEASE_DIR)"/otool_*.txt || (echo "Found non-portable dylib linkage"; exit 1)
	@! rg -q '^[[:space:]]+@rpath/' "$(RELEASE_DIR)"/otool_*.txt || (echo "Found unresolved @rpath dylib linkage"; exit 1)
	@set -eu; runtime="$$(mktemp -d "$(RELEASE_DIR)/audit-runtime.XXXXXX")"; \
	trap 'rm -rf "$$runtime"' EXIT HUP INT TERM; \
	DAW_RUNTIME_DIR="$$runtime" "$(PACKAGE_MACOS_DIR)/daw-launcher" --print-config > "$(RELEASE_DIR)/print_config.txt"
	@rg -q '^DAW_RUNTIME_DIR=' "$(RELEASE_DIR)/print_config.txt" || (echo "Missing DAW_RUNTIME_DIR in launcher config"; exit 1)
	@rg -q '^VK_ICD_FILENAMES=' "$(RELEASE_DIR)/print_config.txt" || (echo "Missing VK_ICD_FILENAMES in launcher config"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/runtime" || (echo "Release bundle includes generated config/runtime"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/last_session.json" || (echo "Release bundle includes local last_session.json"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/projects" || (echo "Release bundle includes local project state"; exit 1)
	@test ! -e "$(PACKAGE_RESOURCES_DIR)/config/library_index.json" || (echo "Release bundle includes local library index"; exit 1)
	@extra_audio="$$(find "$(PACKAGE_RESOURCES_DIR)/assets/audio" -type f ! -name README.md -print -quit)"; \
	test -z "$$extra_audio" || (echo "Release bundle includes local user audio: $$extra_audio"; exit 1)
	@! find "$(PACKAGE_APP_DIR)" -path '*/docs/private_program_docs/*' -print -quit | rg -q . || (echo "Release bundle includes private docs"; exit 1)
	@echo "release-bundle-audit passed."

release-sign:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-sign-internal

release-sign-internal: release-bundle-audit-internal
	@test -n "$(RELEASE_CODESIGN_IDENTITY)" || (echo "Missing signing identity"; exit 1)
	@echo "Signing with identity: $(RELEASE_CODESIGN_IDENTITY)"
	@if [ "$(RELEASE_CODESIGN_IDENTITY)" = "-" ]; then \
		for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
			[ -f "$$dylib" ] || continue; \
			codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$$dylib"; \
		done; \
		codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-bin"; \
		codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$(PACKAGE_MACOS_DIR)/daw-launcher"; \
		codesign --force --sign "$(RELEASE_CODESIGN_IDENTITY)" --timestamp=none "$(PACKAGE_APP_DIR)"; \
	else \
		for dylib in "$(PACKAGE_FRAMEWORKS_DIR)"/*.dylib; do \
			[ -f "$$dylib" ] || continue; \
			codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$$dylib"; \
		done; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-bin"; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_MACOS_DIR)/daw-launcher"; \
		codesign --force --timestamp --options runtime --sign "$(RELEASE_CODESIGN_IDENTITY)" "$(PACKAGE_APP_DIR)"; \
	fi
	@echo "release-sign complete."

release-verify:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-verify-internal

release-verify-internal: release-sign-internal
	@codesign --verify --deep --strict "$(PACKAGE_APP_DIR)"
	@if [ "$(RELEASE_CODESIGN_IDENTITY)" = "-" ]; then \
		echo "release-verify note: ad-hoc identity in use; skipping spctl Gatekeeper assessment"; \
	else \
		set +e; spctl_out="$$(spctl --assess --type execute --verbose=2 "$(PACKAGE_APP_DIR)" 2>&1)"; spctl_rc=$$?; set -e; \
		echo "$$spctl_out"; \
		if [ $$spctl_rc -eq 0 ]; then \
			:; \
		elif printf '%s' "$$spctl_out" | rg -q 'source=Unnotarized Developer ID'; then \
			echo "release-verify passed (pre-notary signed state)."; \
		else \
			echo "release-verify failed."; \
			exit $$spctl_rc; \
		fi; \
	fi
	@echo "release-verify passed."

release-verify-signed:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-verify-signed-internal

release-verify-signed-internal: release-verify-internal
	@echo "release-verify-signed passed."

release-notarize:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-notarize-internal

release-notarize-internal: release-verify-signed-internal
	@test -n "$(APPLE_NOTARY_PROFILE)" || (echo "Missing APPLE_NOTARY_PROFILE"; exit 1)
	@mkdir -p "$(RELEASE_DIR)"
	@ditto -c -k --keepParent "$(PACKAGE_APP_DIR)" "$(RELEASE_APP_ZIP)"
	@xcrun notarytool submit "$(RELEASE_APP_ZIP)" --keychain-profile "$(APPLE_NOTARY_PROFILE)" --wait --output-format json > "$(RELEASE_DIR)/notary_submit.json"
	@rg -q '"status"[[:space:]]*:[[:space:]]*"Accepted"' "$(RELEASE_DIR)/notary_submit.json" || (cat "$(RELEASE_DIR)/notary_submit.json" && echo "Notary submission was not accepted" && exit 1)
	@echo "release-notarize passed."

release-staple:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-staple-internal

release-staple-internal: release-notarize-internal
	@attempt=1; \
	while [ $$attempt -le $(STAPLE_MAX_ATTEMPTS) ]; do \
		if xcrun stapler staple "$(PACKAGE_APP_DIR)" && xcrun stapler validate "$(PACKAGE_APP_DIR)"; then \
			echo "release-staple passed."; \
			exit 0; \
		fi; \
		echo "staple attempt $$attempt/$(STAPLE_MAX_ATTEMPTS) failed; retrying in $(STAPLE_RETRY_DELAY_SEC)s"; \
		sleep $(STAPLE_RETRY_DELAY_SEC); \
		attempt=$$((attempt+1)); \
	done; \
	echo "release-staple failed."; \
	exit 1

release-verify-notarized:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-verify-notarized-internal

release-verify-notarized-internal: release-staple-internal
	@spctl --assess --type execute --verbose=2 "$(PACKAGE_APP_DIR)"
	@xcrun stapler validate "$(PACKAGE_APP_DIR)"
	@echo "release-verify-notarized passed."

release-artifact:
	@if [ "$(RELEASE_ROOT)" != "build/release" ]; then python3 tools/packaging/prepare_release_root.py --output "$(RELEASE_ROOT)" >/dev/null; fi
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-artifact-internal

release-artifact-internal: release-verify-notarized-internal
	@mkdir -p "$(RELEASE_DIR)"
	@ditto -c -k --keepParent "$(PACKAGE_APP_DIR)" "$(RELEASE_APP_ZIP)"
	@shasum -a 256 "$(RELEASE_APP_ZIP)" > "$(RELEASE_APP_ZIP).sha256"
	@{ \
		echo "product=$(RELEASE_PRODUCT_NAME)"; \
		echo "program=$(RELEASE_PROGRAM_KEY)"; \
		echo "host_arch=$(HOST_ARCH)"; \
		echo "target_os=$(TARGET_OS)"; \
		echo "target_arch=$(TARGET_ARCH)"; \
		echo "target_variant=$(TARGET_VARIANT)"; \
		echo "target_triple=$(TARGET_TRIPLE)"; \
		echo "release_platform=$(RELEASE_PLATFORM)"; \
		echo "release_arch=$(RELEASE_ARCH)"; \
		echo "version=$(RELEASE_VERSION)"; \
		echo "channel=$(RELEASE_CHANNEL)"; \
		echo "bundle_id=$(RELEASE_BUNDLE_ID)"; \
		echo "signed=1"; \
		echo "notarized=1"; \
		echo "zip=$(RELEASE_APP_ZIP)"; \
		echo "sha256=$$(cut -d' ' -f1 "$(RELEASE_APP_ZIP).sha256")"; \
		echo "notary_json=$(RELEASE_DIR)/notary_submit.json"; \
	} > "$(RELEASE_MANIFEST)"
	@echo "release-artifact complete: $(RELEASE_APP_ZIP)"

release-distribute:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-distribute-internal

release-distribute-internal: release-artifact-internal
	@echo "release-distribute passed."

release-desktop-refresh:
	@$(MAKE) BUILD_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" PACKAGE_TOOLCHAIN="$(RELEASE_TOOLCHAIN)" TARGET_OS="$(TARGET_OS)" TARGET_ARCH="$(TARGET_ARCH)" TARGET_VARIANT="$(TARGET_VARIANT)" release-desktop-refresh-internal

release-desktop-refresh-internal: release-distribute-internal
	@mkdir -p "$$(dirname "$(DESKTOP_APP_DIR)")"
	@"$(PACKAGE_DESTRUCTIVE_DESTINATION_GUARD)" desktop_app "$(DESKTOP_APP_DIR)" "$(PACKAGE_APP_NAME)"
	@rm -rf "$(DESKTOP_APP_DIR)"
	@cp -R "$(PACKAGE_APP_DIR)" "$(DESKTOP_APP_DIR)"
	@spctl --assess --type execute --verbose=2 "$(DESKTOP_APP_DIR)"
	@echo "release-desktop-refresh passed."

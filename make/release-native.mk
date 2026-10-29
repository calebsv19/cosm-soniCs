# Fixed authentication phases consumed by the Registry's native desktop owner.
RELEASE_VERSION ?= $(strip $(shell cat VERSION))
SONICS_NATIVE_APP := $(RELEASE_ROOT)/soniCs.app
SONICS_NATIVE_ZIP := $(RELEASE_ROOT)/soniCs-$(RELEASE_VERSION)-macOS-arm64-stable.zip
.PHONY: release-build release-bundle-audit release-sign release-verify release-verify-signed release-notarize release-staple release-verify-notarized release-artifact
release-build:
	@python3 tools/packaging/contained_producer.py --root "$(RELEASE_PREPARATION_ROOT)" --target release-bundle-audit --make "$(MAKE)" --compiler "$(HOST_CC)" --release-root "$(RELEASE_ROOT)"
release-bundle-audit: release-build
release-sign: release-bundle-audit
	@test -n "$(APPLE_SIGN_IDENTITY)" && test "$(APPLE_SIGN_IDENTITY)" != "-"
	@for dylib in "$(SONICS_NATIVE_APP)/Contents/Frameworks/"*.dylib; do \
		[ -f "$$dylib" ] || continue; \
		codesign --force --timestamp --options runtime --sign "$(APPLE_SIGN_IDENTITY)" "$$dylib" || exit $$?; \
	done
	@codesign --force --timestamp --options runtime --sign "$(APPLE_SIGN_IDENTITY)" "$(SONICS_NATIVE_APP)/Contents/MacOS/daw-bin"
	@codesign --force --timestamp --options runtime --sign "$(APPLE_SIGN_IDENTITY)" "$(SONICS_NATIVE_APP)/Contents/MacOS/daw-launcher"
	@codesign --force --timestamp --options runtime --sign "$(APPLE_SIGN_IDENTITY)" "$(SONICS_NATIVE_APP)"
release-verify: release-sign
	@codesign --verify --deep --strict "$(SONICS_NATIVE_APP)"
	@set +e; output="$$(spctl --assess --type execute --verbose=2 "$(SONICS_NATIVE_APP)" 2>&1)"; status=$$?; set -e; \
	printf '%s\n' "$$output"; \
	if [ $$status -ne 0 ]; then printf '%s\n' "$$output" | /usr/bin/grep -q '^source=Unnotarized Developer ID$$' || exit $$status; fi
release-verify-signed: release-verify
	@echo "release-verify-signed passed."
release-notarize: release-verify-signed
	@test -n "$(APPLE_NOTARY_PROFILE)"
	@/usr/bin/ditto -c -k --keepParent "$(SONICS_NATIVE_APP)" "$(SONICS_NATIVE_ZIP)"
	@xcrun notarytool submit "$(SONICS_NATIVE_ZIP)" --keychain-profile "$(APPLE_NOTARY_PROFILE)" --wait --output-format json > "$(RELEASE_ROOT)/notary_submit.json"
	@python3 tools/packaging/native_auth.py accepted --root "$(RELEASE_ROOT)" --version "$(RELEASE_VERSION)"
release-staple: release-notarize
	@xcrun stapler staple "$(SONICS_NATIVE_APP)"
	@xcrun stapler validate "$(SONICS_NATIVE_APP)"
release-verify-notarized: release-staple
	@codesign --verify --deep --strict "$(SONICS_NATIVE_APP)"
	@xcrun stapler validate "$(SONICS_NATIVE_APP)"
	@spctl --assess --type execute --verbose=2 "$(SONICS_NATIVE_APP)"
release-artifact: release-verify-notarized
	@/usr/bin/ditto -c -k --keepParent "$(SONICS_NATIVE_APP)" "$(SONICS_NATIVE_ZIP)"
	@python3 tools/packaging/native_auth.py manifest --root "$(RELEASE_ROOT)" --version "$(RELEASE_VERSION)"

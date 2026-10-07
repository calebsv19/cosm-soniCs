# Create-only local input for the existing release authentication stage.
RELEASE_ROOT ?=
RELEASE_DISPOSABLE_BASENAME := $(RELEASE_PRODUCT_NAME)-$(RELEASE_VERSION)-$(RELEASE_PLATFORM)-$(RELEASE_ARCH)-$(RELEASE_CHANNEL)-disposable
RELEASE_ARTIFACT_ZIP = $(RELEASE_ROOT)/$(RELEASE_DISPOSABLE_BASENAME).zip
RELEASE_ARTIFACT_MANIFEST = $(RELEASE_ROOT)/$(RELEASE_DISPOSABLE_BASENAME).manifest.txt

.PHONY: release-artifact-disposable
release-artifact-disposable:
	@set -eu; \
	root="$$(python3 tools/packaging/prepare_release_root.py --output "$(RELEASE_ROOT)")"; \
	$(MAKE) release-package-self-test \
		DIST_DIR="$$root"; \
	archive="$(RELEASE_ARTIFACT_ZIP)"; \
	manifest="$(RELEASE_ARTIFACT_MANIFEST)"; \
	/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$$root/$(PACKAGE_APP_NAME)" "$$archive"; \
	shasum -a 256 "$$archive" > "$$archive.sha256"; \
	{ \
		echo "product=$(RELEASE_PRODUCT_NAME)"; \
		echo "program=$(RELEASE_PROGRAM_KEY)"; \
		echo "version=$(RELEASE_VERSION)"; \
		echo "platform=$(RELEASE_PLATFORM)"; \
		echo "arch=$(RELEASE_ARCH)"; \
		echo "format=zip"; \
		echo "channel=$(RELEASE_CHANNEL)"; \
		echo "bundle_id=$(RELEASE_BUNDLE_ID)"; \
		echo "source_commit=$$(git rev-parse HEAD)"; \
		echo "source_tree=$$(git rev-parse HEAD^{tree})"; \
		echo "disposable=1"; \
		echo "signed=0"; \
		echo "release_signed=0"; \
		echo "notarized=0"; \
		echo "codesign_identity=ad-hoc"; \
		echo "installed_app_replacement=0"; \
		echo "app=$(PACKAGE_APP_NAME)"; \
		echo "artifact=$$(basename "$$archive")"; \
		echo "zip=$$(basename "$$archive")"; \
		echo "sha256=$$(cut -d' ' -f1 "$$archive.sha256")"; \
	} > "$$manifest"; \
	echo "Disposable release artifact complete: $$root"

# Exercise package runtime seeding without touching the installed app runtime.
.PHONY: release-package-self-test
release-package-self-test: package-desktop-smoke
	@set -eu; \
	runtime="$$(mktemp -d "$(CURDIR)/build/sonics-package-self-test.XXXXXX")"; \
	trap 'rm -rf "$$runtime"' EXIT HUP INT TERM; \
	DAW_RUNTIME_DIR="$$runtime" "$(PACKAGE_MACOS_DIR)/daw-launcher" --self-test

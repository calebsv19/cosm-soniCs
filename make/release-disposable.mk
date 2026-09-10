# Create-only local input for the existing release authentication stage.
RELEASE_ROOT ?=
RELEASE_DISPOSABLE_BASENAME := $(RELEASE_PRODUCT_NAME)-$(RELEASE_VERSION)-$(RELEASE_PLATFORM)-$(RELEASE_ARCH)-$(RELEASE_CHANNEL)-disposable
RELEASE_ARTIFACT_ZIP = $(RELEASE_ROOT)/$(RELEASE_DISPOSABLE_BASENAME).zip
RELEASE_ARTIFACT_MANIFEST = $(RELEASE_ROOT)/$(RELEASE_DISPOSABLE_BASENAME).manifest.txt

.PHONY: release-artifact-disposable
release-artifact-disposable:
	@set -eu; \
	root="$(RELEASE_ROOT)"; \
	[ -n "$$root" ] || { echo "RELEASE_ROOT is required"; exit 1; }; \
	case "$$root" in /*|.|./*|*/.|*//*|*'..'*) echo "RELEASE_ROOT must be a contained job-scoped relative path: $$root"; exit 1;; esac; \
	case "/$$root/" in */../*) echo "RELEASE_ROOT must be a contained job-scoped relative path: $$root"; exit 1;; esac; \
	case "$$root" in build/release-authenticated/*) job_id="$${root#build/release-authenticated/}";; *) echo "RELEASE_ROOT must use build/release-authenticated/<job-id>: $$root"; exit 1;; esac; \
	case "$$job_id" in ''|*/*|-*|*[^a-z0-9-]*) echo "RELEASE_ROOT job id is invalid: $$job_id"; exit 1;; esac; \
	job_length=$${#job_id}; \
	[ "$$job_length" -ge 3 ] && [ "$$job_length" -le 81 ] || { echo "RELEASE_ROOT job id is invalid: $$job_id"; exit 1; }; \
	source_root="$$(cd "$(CURDIR)" && pwd -P)"; \
	[ -d "$$source_root" ] || { echo "RELEASE_ROOT source root is invalid: $$source_root"; exit 1; }; \
	root="$$source_root/$$root"; \
	[ ! -L "$$root" ] || { echo "RELEASE_ROOT must not be a symlink: $$root"; exit 1; }; \
	[ ! -e "$$root" ] || { echo "RELEASE_ROOT must not already exist: $$root"; exit 1; }; \
	parent="$$source_root"; \
	for component in build release-authenticated; do \
		candidate="$$parent/$$component"; \
		[ ! -L "$$candidate" ] || { echo "RELEASE_ROOT ancestor must not be a symlink: $$candidate"; exit 1; }; \
		if [ -e "$$candidate" ]; then [ -d "$$candidate" ] || { echo "RELEASE_ROOT ancestor must be a directory: $$candidate"; exit 1; }; else mkdir "$$candidate"; fi; \
		resolved="$$(cd "$$candidate" && pwd -P)"; \
		[ "$$resolved" = "$$candidate" ] || { echo "RELEASE_ROOT ancestor escaped source tree: $$candidate"; exit 1; }; \
		parent="$$candidate"; \
	done; \
	[ "$$parent" = "$$(dirname "$$root")" ] || { echo "RELEASE_ROOT selected parent drifted: $$root"; exit 1; }; \
	mkdir "$$root"; \
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

# Route fixed release gates into an attempt-owned working copy of frozen inputs.
.PHONY: run-headless-smoke release-package-self-test release-artifact-disposable
run-headless-smoke release-package-self-test release-artifact-disposable:
	@python3 tools/packaging/contained_producer.py --root "$(RELEASE_PREPARATION_ROOT)" --target "$@" --make "$(MAKE)" --compiler "$(HOST_CC)" --release-root "$(RELEASE_ROOT)"

include make/release-native.mk

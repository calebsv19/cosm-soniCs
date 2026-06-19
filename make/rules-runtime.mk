run: $(APP_BIN)
	$(APP_BIN)

run-ide-theme: $(APP_BIN)
	DAW_USE_SHARED_THEME_FONT=1 DAW_USE_SHARED_THEME=1 DAW_USE_SHARED_FONT=1 DAW_THEME_PRESET=ide_gray DAW_FONT_PRESET=ide $(APP_BIN)

run-headless-smoke: all test-stable
	@echo "daw headless smoke passed (non-interactive)"
	@echo "demo-proof: build=all tests=test-stable ui=not-launched package=separate manual=separate"
	@echo "demo-proof: package lane -> make -C daw package-desktop-self-test"
	@echo "demo-proof: manual microphone proof remains outside automated gates"

visual-harness: $(APP_BIN)
	@echo "visual harness binary ready: $(APP_BIN)"

VISUAL_ARTIFACT_DIR ?= visual_artifacts
VISUAL_ARTIFACT_PATH ?= $(VISUAL_ARTIFACT_DIR)/daw_first_frame.bmp

visual-artifact: $(APP_BIN)
	@mkdir -p "$(VISUAL_ARTIFACT_DIR)"
	@rm -f "$(VISUAL_ARTIFACT_PATH)"
	@DAW_VISUAL_ARTIFACT_ONCE=1 DAW_VISUAL_ARTIFACT_PATH="$(VISUAL_ARTIFACT_PATH)" $(APP_BIN)
	@test -s "$(VISUAL_ARTIFACT_PATH)"
	@echo "visual-artifact ready: $(VISUAL_ARTIFACT_PATH)"

visual-proof: visual-artifact

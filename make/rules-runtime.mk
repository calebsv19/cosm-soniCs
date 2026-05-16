run: $(APP_BIN)
	$(APP_BIN)

run-ide-theme: $(APP_BIN)
	DAW_USE_SHARED_THEME_FONT=1 DAW_USE_SHARED_THEME=1 DAW_USE_SHARED_FONT=1 DAW_THEME_PRESET=ide_gray DAW_FONT_PRESET=ide $(APP_BIN)

run-headless-smoke: all test-stable
	@echo "daw headless smoke passed (non-interactive)"

visual-harness: $(APP_BIN)
	@echo "visual harness binary ready: $(APP_BIN)"

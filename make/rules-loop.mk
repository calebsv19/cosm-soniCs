loop-gates: $(APP_BIN)
	APP_BIN="$(APP_BIN)" HEADLESS=$${HEADLESS:-1} SCENARIOS="$${SCENARIOS:-idle interaction}" RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

loop-gates-strict: $(APP_BIN)
	APP_BIN="$(APP_BIN)" HEADLESS=$${HEADLESS:-1} SCENARIOS="$${SCENARIOS:-idle interaction}" PROFILE=strict STRICT=1 RUN_SECONDS=$${RUN_SECONDS:-8} ./tools/run_loop_gates.sh

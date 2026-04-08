#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_BIN="${APP_BIN:-$ROOT_DIR/build/daw_app}"
RUN_SECONDS="${RUN_SECONDS:-8}"
SCENARIOS=(${SCENARIOS:-idle playback interaction})
LOG_DIR="${LOG_DIR:-/tmp/daw_loop_gates_$(date +%Y%m%d_%H%M%S)}"
SUMMARY_FILE="${SUMMARY_FILE:-$LOG_DIR/summary.txt}"
PROFILE="${PROFILE:-default}"
STRICT="${STRICT:-0}"
HEADLESS="${HEADLESS:-0}"

mkdir -p "$LOG_DIR"

if [[ ! -x "$APP_BIN" ]]; then
  echo "[GateHarness] app binary not found/executable: $APP_BIN"
  echo "[GateHarness] build first: make -C $ROOT_DIR"
  exit 1
fi

pass_count=0
fail_count=0
inconclusive_count=0

if [[ "$PROFILE" == "strict" ]]; then
  export DAW_GATE_MAX_ACTIVE_PCT_IDLE="${DAW_GATE_MAX_ACTIVE_PCT_IDLE:-10}"
  export DAW_GATE_MIN_BLOCKED_PCT_IDLE="${DAW_GATE_MIN_BLOCKED_PCT_IDLE:-90}"
  export DAW_GATE_MIN_WAITS_PLAYBACK="${DAW_GATE_MIN_WAITS_PLAYBACK:-2}"
  export DAW_GATE_MAX_ACTIVE_PCT_INTERACTION="${DAW_GATE_MAX_ACTIVE_PCT_INTERACTION:-85}"
fi

echo "[GateHarness] app=$APP_BIN"
echo "[GateHarness] run_seconds=$RUN_SECONDS"
echo "[GateHarness] log_dir=$LOG_DIR"
echo "[GateHarness] scenarios=${SCENARIOS[*]}"
echo "[GateHarness] profile=$PROFILE strict=$STRICT"
echo "[GateHarness] headless=$HEADLESS"

for scenario in "${SCENARIOS[@]}"; do
  log_file="$LOG_DIR/${scenario}.log"
  echo "[GateHarness] scenario=$scenario start"

  DAW_LOOP_DIAG_LOG=1 \
  DAW_LOOP_GATE_EVAL=1 \
  DAW_HEADLESS="$HEADLESS" \
  DAW_SCENARIO="$scenario" \
  "$APP_BIN" >"$log_file" 2>&1 &
  pid=$!

  sleep "$RUN_SECONDS"
  kill "$pid" >/dev/null 2>&1 || true
  sleep 0.2
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill -9 "$pid" >/dev/null 2>&1 || true
  fi
  wait "$pid" >/dev/null 2>&1 || true

  if grep -q "\\[LoopGate\\]" "$log_file"; then
    if grep -q "\\[LoopGate\\].*pass=no" "$log_file"; then
      if [[ "$scenario" == "playback" ]] && \
         grep -q "\\[LoopGate\\].*scenario=playback.*pass=no.*playback_active=no" "$log_file" && \
         grep -q "SDL_OpenAudioDevice failed" "$log_file"; then
        echo "[GateHarness] scenario=$scenario result=inconclusive reason=no_audio_backend"
        inconclusive_count=$((inconclusive_count + 1))
      else
        echo "[GateHarness] scenario=$scenario result=fail"
        fail_count=$((fail_count + 1))
      fi
    elif grep -q "\\[LoopGate\\].*pass=yes" "$log_file"; then
      echo "[GateHarness] scenario=$scenario result=pass"
      pass_count=$((pass_count + 1))
    else
      echo "[GateHarness] scenario=$scenario result=inconclusive reason=unparsed_loopgate"
      inconclusive_count=$((inconclusive_count + 1))
    fi
  elif grep -q "SDL_Init failed: The video driver did not add any displays" "$log_file"; then
    echo "[GateHarness] scenario=$scenario result=inconclusive reason=no_display_backend"
    inconclusive_count=$((inconclusive_count + 1))
  elif grep -q "SDL_OpenAudioDevice failed" "$log_file"; then
    echo "[GateHarness] scenario=$scenario result=inconclusive reason=no_audio_backend"
    inconclusive_count=$((inconclusive_count + 1))
  else
    echo "[GateHarness] scenario=$scenario result=inconclusive reason=no_loopgate_output"
    inconclusive_count=$((inconclusive_count + 1))
  fi
done

echo "[GateHarness] summary pass=$pass_count fail=$fail_count inconclusive=$inconclusive_count"
echo "[GateHarness] logs=$LOG_DIR"
{
  echo "app=$APP_BIN"
  echo "run_seconds=$RUN_SECONDS"
  echo "profile=$PROFILE"
  echo "strict=$STRICT"
  echo "headless=$HEADLESS"
  echo "scenarios=${SCENARIOS[*]}"
  echo "pass=$pass_count"
  echo "fail=$fail_count"
  echo "inconclusive=$inconclusive_count"
  echo "logs=$LOG_DIR"
} > "$SUMMARY_FILE"
echo "[GateHarness] summary_file=$SUMMARY_FILE"

if [[ $fail_count -gt 0 ]]; then
  exit 1
fi
if [[ $inconclusive_count -gt 0 ]]; then
  if [[ "$STRICT" == "1" ]]; then
    exit 1
  fi
  exit 2
fi
exit 0

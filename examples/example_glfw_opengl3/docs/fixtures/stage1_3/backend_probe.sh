#!/usr/bin/env bash
# Smoke tests for Stage 1.3 Drogon endpoints using seeded fixtures.
set -euo pipefail

BACKEND_URL="${BACKEND_URL:-http://39.114.73.97:33931/api}"

check() {
  local name="$1"; shift
  echo "[probe] ${name}" >&2
  if ! output=$(curl -sfSL "$@" ); then
    echo "[fail] ${name}" >&2
    return 1
  fi
  echo "$output"
}

# Dataset catalog should include btc25_1 and eth25_1
check "datasets" "${BACKEND_URL}/indicators/datasets" \
  | jq 'map(select(.questdb_tag == "btc25_1" or .questdb_tag == "eth25_1")) | length == 2' \
  | grep -q true

echo "[pass] dataset catalog" >&2

# Walkforward run list should include prediction measurements we expect
check "walkforward runs" "${BACKEND_URL}/walkforward/runs" \
  | jq 'map(select(.predictionMeasurement == "btc25_run1" or .predictionMeasurement == "eth25_run1")) | length == 2' \
  | grep -q true

echo "[pass] walkforward list" >&2

# Trades endpoint returns at least one record for btc25 run (UUID looked up via prediction measurement)
RUN_ID=$(curl -sfSL "${BACKEND_URL}/walkforward/runs" \
  | jq -r 'first(.[] | select(.predictionMeasurement == "btc25_run1") | .runId)')
if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
  echo "[fail] unable to resolve runId for btc25_run1" >&2
  exit 1
fi

SIM_ID=$(curl -sfSL "${BACKEND_URL}/tradesim/runs" \
  | jq -r --arg RUN "$RUN_ID" 'first(.[] | select(.runId == $RUN) | .simulationId)')
if [[ -z "$SIM_ID" || "$SIM_ID" == "null" ]]; then
  echo "[fail] unable to resolve simulationId for run ${RUN_ID}" >&2
  exit 1
fi

check "trades" "${BACKEND_URL}/tradesim/runs/${SIM_ID}/trades" \
  | jq -e 'length > 0' >/dev/null

echo "[pass] trades endpoint" >&2

echo "All Stage 1.3 probes succeeded." >&2

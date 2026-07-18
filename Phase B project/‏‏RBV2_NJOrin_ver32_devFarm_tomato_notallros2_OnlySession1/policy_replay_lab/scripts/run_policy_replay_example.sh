#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"

SESSION_PATH="${1:-Debugging/toClient/sessions/session_20260630_103628}"
MODE="${2:-policy_only}"
MAX_IMAGES="${3:-0}"

SESSION_ID="$(basename "$SESSION_PATH")"
CPP_OUT="policy_replay_lab/outputs/${SESSION_ID}/cpp_replay"
ANALYSIS_OUT="policy_replay_lab/outputs/${SESSION_ID}/analysis"

echo "[run] project: $PROJECT_ROOT"
echo "[run] session: $SESSION_PATH"
echo "[run] mode: $MODE"

if [[ ! -x policy_replay_lab/bin/policy_replay_cli ]]; then
  echo "[run] CLI not found; building first..."
  bash policy_replay_lab/scripts/build_policy_replay_cli.sh
fi

./policy_replay_lab/bin/policy_replay_cli \
  --project-root . \
  --session "$SESSION_PATH" \
  --mode "$MODE" \
  --output "$CPP_OUT" \
  --engine models/best8s_seg_v43_fp16.engine \
  --groups images_ok_raw,images_weak_noise_raw \
  --max-images "$MAX_IMAGES"

python3 policy_replay_lab/python/policy_analysis.py \
  --cpp-jsonl "$CPP_OUT/cpp_replay_detections.jsonl" \
  --config policy_replay_lab/configs/policy_v32_experiment.json \
  --output "$ANALYSIS_OUT"

echo "[run] done"
echo "[run] CSV: $ANALYSIS_OUT/policy_compare.csv"
echo "[run] old debug: $ANALYSIS_OUT/old_policy_debug"
echo "[run] new debug: $ANALYSIS_OUT/new_policy_debug"

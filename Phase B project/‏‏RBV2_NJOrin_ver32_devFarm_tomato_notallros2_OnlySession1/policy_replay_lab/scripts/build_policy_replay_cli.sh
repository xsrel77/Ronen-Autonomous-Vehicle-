#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"

mkdir -p policy_replay_lab/bin

echo "[build] project root: $PROJECT_ROOT"
echo "[build] compiling policy_replay_cli..."

g++ -std=c++17 -O2 -Wall -Wextra \
  policy_replay_lab/cpp/policy_replay_cli.cpp \
  perception/ObjectDetector.cpp \
  -I. \
  -I/usr/include/opencv4 \
  $(pkg-config --cflags --libs opencv4) \
  -I/usr/local/cuda/include \
  -L/usr/local/cuda/lib64 \
  -lnvinfer -lnvonnxparser -lcudart \
  -pthread \
  -o policy_replay_lab/bin/policy_replay_cli

echo "[build] OK: policy_replay_lab/bin/policy_replay_cli"

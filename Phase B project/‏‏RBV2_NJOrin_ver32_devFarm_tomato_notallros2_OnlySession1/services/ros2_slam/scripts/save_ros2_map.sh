#!/usr/bin/env bash
set -eo pipefail

SESSION_DIR="${1:-}"
if [ -z "${SESSION_DIR}" ]; then
  echo "Usage: $0 <session_dir>"
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROS2_SLAM_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WS_DIR="${ROS2_SLAM_DIR}/ros2_ws"
SESSION_ABS="$(mkdir -p "${SESSION_DIR}" && cd "${SESSION_DIR}" && pwd)"
LOG_FILE="${SESSION_ABS}/ros2_mapping.log"
MAP_PREFIX="${SESSION_ABS}/map"

if [ ! -f /opt/ros/humble/setup.bash ]; then
  echo "[ros2_slam][save] ROS2 Humble not found" | tee -a "${LOG_FILE}"
  exit 2
fi

source /opt/ros/humble/setup.bash
if [ -f "${WS_DIR}/install/setup.bash" ]; then
  source "${WS_DIR}/install/setup.bash"
fi

echo "[ros2_slam][save] waiting briefly for /map before saving" | tee -a "${LOG_FILE}"
if timeout 8s ros2 topic echo /map --once >/dev/null 2>&1; then
  echo "[ros2_slam][save] /map is available" | tee -a "${LOG_FILE}"
else
  echo "[ros2_slam][save] WARN: /map not observed within 8s, trying map_saver anyway" | tee -a "${LOG_FILE}"
fi

echo "[ros2_slam][save] saving map to ${MAP_PREFIX}.yaml/.pgm" | tee -a "${LOG_FILE}"
if ! ros2 run nav2_map_server map_saver_cli -f "${MAP_PREFIX}" --ros-args -p save_map_timeout:=10.0 2>&1 | tee -a "${LOG_FILE}"; then
  echo "[ros2_slam][save] WARN: first save attempt failed, retrying once with default map_saver_cli" | tee -a "${LOG_FILE}"
  sleep 1
  ros2 run nav2_map_server map_saver_cli -f "${MAP_PREFIX}" 2>&1 | tee -a "${LOG_FILE}"
fi

if [ ! -f "${MAP_PREFIX}.yaml" ] || [ ! -f "${MAP_PREFIX}.pgm" ]; then
  echo "[ros2_slam][save] map files missing after save" | tee -a "${LOG_FILE}"
  exit 4
fi

echo "[ros2_slam][save] OK" | tee -a "${LOG_FILE}"

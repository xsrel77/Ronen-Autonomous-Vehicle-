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
PROJECT_DIR="$(cd "${ROS2_SLAM_DIR}/../.." && pwd)"
SESSION_ABS="$(mkdir -p "${SESSION_DIR}" && cd "${SESSION_DIR}" && pwd)"
LOG_FILE="${SESSION_ABS}/ros2_mapping.log"
MANIFEST_FILE="${SESSION_ABS}/session_manifest.json"

if [ ! -f /opt/ros/humble/setup.bash ]; then
  echo "[ros2_slam][start] ROS2 Humble not found" | tee -a "${LOG_FILE}"
  exit 2
fi

if [ ! -f "${WS_DIR}/install/setup.bash" ]; then
  echo "[ros2_slam][start] ROS2 workspace is not built yet: ${WS_DIR}" | tee -a "${LOG_FILE}"
  echo "[ros2_slam][start] Run: ${SCRIPT_DIR}/build_ros2_mapping.sh" | tee -a "${LOG_FILE}"
  exit 3
fi

cat > "${MANIFEST_FILE}" <<JSON
{
  "source": "slam_toolbox",
  "navigation_enabled": false,
  "odom_source": "estimated_cmd_imu_no_encoders",
  "session_dir": "${SESSION_ABS}",
  "map_yaml": "${SESSION_ABS}/map.yaml",
  "map_pgm": "${SESSION_ABS}/map.pgm"
}
JSON

source /opt/ros/humble/setup.bash
source "${WS_DIR}/install/setup.bash"
cd "${PROJECT_DIR}"

echo "[ros2_slam][start] session=${SESSION_ABS}" | tee -a "${LOG_FILE}"
echo "[ros2_slam][start] launching rbv2_mapping + slam_toolbox" | tee -a "${LOG_FILE}"

exec ros2 launch rbv2_mapping rbv2_slam_launch.py 2>&1 | tee -a "${LOG_FILE}"

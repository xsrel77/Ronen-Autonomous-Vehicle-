#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROS2_SLAM_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WS_DIR="${ROS2_SLAM_DIR}/ros2_ws"

if [ ! -f /opt/ros/humble/setup.bash ]; then
  echo "[ros2_slam][build] ROS2 Humble was not found at /opt/ros/humble/setup.bash"
  echo "[ros2_slam][build] Install ROS2 Humble first, then run this script again."
  exit 2
fi

source /opt/ros/humble/setup.bash
cd "${WS_DIR}"
colcon build --symlink-install

echo "[ros2_slam][build] OK"
echo "[ros2_slam][build] Next: source ${WS_DIR}/install/setup.bash"

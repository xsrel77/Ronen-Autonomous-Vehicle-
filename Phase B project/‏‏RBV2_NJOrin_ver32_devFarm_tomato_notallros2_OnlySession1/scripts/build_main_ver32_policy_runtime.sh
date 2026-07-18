#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "[build-main-v32] project: $PROJECT_ROOT"

g++ -std=c++17 -O2 \
app/amain.cpp \
hardware/RaspbotBoard.cpp \
hardware/ServoCamera.cpp \
hardware/MotorDCfb.cpp \
hardware/MotorDCLR.cpp \
control/DriveController.cpp \
control/TestDriveOp_g.cpp \
orchestration/JoystickController.cpp \
behavior/BehaviorManager.cpp \
navigation/NavRuntime.cpp \
navigation/NavManager.cpp \
navigation/OdomRuntime.cpp \
navigation/LidarPoseRuntime.cpp \
navigation/LidarCorrectionHintsRuntime.cpp \
Debugging/test_yaw/YawDebugLogger.cpp \
Debugging/test_nav_odom/NavOdomDebugLogger.cpp \
Debugging/test_nav_lidar/NavLidarDebugLogger.cpp \
Debugging/toClient/ToClientJsonLogger.cpp \
Debugging/toClient/ToClientVideoRecorder.cpp \
Debugging/toClient/ToClientLidarPreviewWriter.cpp \
dev_farm/DevFarmVideoRecorder.cpp \
dev_farm/DevFarmLidarMapper.cpp \
dev_farm/DevFarmOccupancyGridMapper.cpp \
dev_farm/DevFarmPoseProvider.cpp \
dev_farm/DevFarmScanMatcher.cpp \
services/ros2_slam/Ros2SlamManager.cpp \
services/ros2_slam/Ros2OdomUdpPublisher.cpp \
perception/ObjectDetector.cpp \
perception/TargetTracker.cpp \
lidar/MiniLidarSDL.cpp \
gui/UnifiedGui.cpp \
gui/panels/NavStatusPanel.cpp \
gui/panels/M5SensorsPanel.cpp \
gui/panels/DetectionsPanel.cpp \
gui/panels/DriveStatusPanel.cpp \
gui/panels/LidarProximityPanel.cpp \
gui/panels/DiagnosticsPanel.cpp \
gui/panels/CameraViewPanel.cpp \
gui/panels/LidarViewPanel.cpp \
m5stick_comm/M5StickSerial.cpp \
-I. \
-I"$PROJECT_ROOT" \
-I"$PROJECT_ROOT/external/json" \
-I/usr/include/opencv4 \
$(pkg-config --cflags --libs opencv4) \
-I/usr/include/SDL2 -D_REENTRANT \
-I/usr/local/cuda/include \
-L/usr/local/cuda/lib64 \
-lSDL2 -pthread \
-lnvinfer -lnvonnxparser -lcudart \
-o amain

echo "[build-main-v32] OK: $PROJECT_ROOT/amain"

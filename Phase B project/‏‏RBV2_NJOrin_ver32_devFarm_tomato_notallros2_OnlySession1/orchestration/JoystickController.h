#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/SystemState.h"
#include "lidar/MiniLidarSDL.h"
#include "gui/UnifiedGui.h"
#include "behavior/BehaviorManager.h"
#include "m5stick_comm/M5StickSerial.h"
#include "navigation/NavRuntime.h"
#include "navigation/NavManager.h"
#include "navigation/OdomRuntime.h"
#include "navigation/LidarPoseRuntime.h"
#include "navigation/LidarCorrectionHintsRuntime.h"
#include "Debugging/DebugFlags.h"
#include "Debugging/test_yaw/YawDebugLogger.h"
#include "Debugging/test_nav_odom/NavOdomDebugLogger.h"
#include "Debugging/test_nav_lidar/NavLidarDebugLogger.h"
#include "Debugging/toClient/ToClientJsonLogger.h"
#include "dev_farm/DevFarmVideoRecorder.h"
#include "dev_farm/DevFarmLidarMapper.h"
#include "dev_farm/DevFarmPoseProvider.h"
#include "services/ros2_slam/Ros2SlamManager.h"
#include "services/ros2_slam/Ros2OdomUdpPublisher.h"

class DriveController;
class TargetTracker;
class ObjectDetector;
class RaspbotBoard;

class JoystickController {
public:
    explicit JoystickController(TargetTracker* tracker = nullptr,
                                ObjectDetector* detector = nullptr,
                                RaspbotBoard* board = nullptr);
    ~JoystickController();

    bool init();
    void close();

    void setTrackingPump(std::function<void()> pump);
    void setDebugFlags(const DebugFlags& flags);

    bool run(DriveController& drive);

    RobotState getRobotState() const;
    BehaviorDecision getBehaviorDecision() const;

private:
    void updateRobotState(DriveController& drive, uint64_t nowMs);
    void updateM5StickState(uint64_t nowMs);
    void updateNavState(uint64_t nowMs);
    void updateOdomState(uint64_t nowMs);
    void updateNavGuardState(uint64_t nowMs);
    void resetNavGuardMonitor();

    void logNavOdomIfNeeded();
    void logNavLidarIfNeeded();
    void logToClientIfNeeded();

    bool startToClientDebugSession(uint64_t nowMs);
    void stopToClientDebugSession(const std::string& reason, uint64_t nowMs);
    void updateCameraServoState(uint64_t nowMs);

    void toggleDevFarmVideo(uint64_t nowMs);
    bool startDevFarmVideo(uint64_t nowMs);
    void stopDevFarmVideo(const std::string& reason, uint64_t nowMs);
    void updateDevFarmVideo(uint64_t nowMs);

    void toggleDevFarmLidarMap(uint64_t nowMs);
    bool startDevFarmLidarMap(uint64_t nowMs);
    void stopDevFarmLidarMap(const std::string& reason, uint64_t nowMs);
    bool loadDevFarmLidarMap(uint64_t nowMs);
    void updateDevFarmLidarMap(uint64_t nowMs);
    void syncDevFarmLidarMapState();

    bool canStartInternalLidar() const;
    bool canStartRos2Mapping() const;
    void resetRos2OdomSession(uint64_t nowMs);
    void syncRos2SlamState();
    void toggleRos2Mapping(uint64_t nowMs);
    bool startRos2Mapping(uint64_t nowMs);
    void stopRos2Mapping(const std::string& reason, uint64_t nowMs);
    bool loadLatestRos2Map(uint64_t nowMs);
    bool loadRos2MapForGuiOverlay(uint64_t nowMs);
    void resetMapOverlayPose(uint64_t nowMs);
    void setRos2MapManualStartPose(double mapX, double mapY, uint64_t nowMs);
    void adjustRos2MapManualStartYaw(double deltaDeg, uint64_t nowMs);
    void updateRos2MapOverlayPose(uint64_t nowMs);
    void updateRos2MappingState(uint64_t nowMs);

    void beepBuzzer(unsigned durationMs = 120);
    void beepPattern(unsigned count, unsigned durationMs = 90, unsigned gapMs = 80);
    static std::string makeDevFarmVideoPath();

    bool canToggleTracking(uint64_t nowMs) const;
    bool canToggleLidar(uint64_t nowMs) const;
    bool canToggleImu(uint64_t nowMs) const;
    bool canToggleEnv(uint64_t nowMs) const;

    void startTrackingSafely();
    void stopTrackingSafely();

    static double computeSectorMinDistance(const std::vector<LidarPoint>& points,
                                           double minAngleDeg,
                                           double maxAngleDeg);
    static bool loadPgmImage(const std::string& path, int& width, int& height, std::vector<std::uint8_t>& pixels);
    static bool loadRosMapYaml(const std::string& yamlPath, double& resolution, double& originX, double& originY, double& originYawRad, std::string& imagePath);
    static double normalizeAngleDeg(double angleDeg);
    static bool angleInRange(double angleDeg, double minDeg, double maxDeg);

private:
    SDL_GameController* gc_ = nullptr;
    SDL_Haptic*         haptic_ = nullptr;
    bool                sdl_ok_ = false;

    TargetTracker* tracker_ = nullptr;
    ObjectDetector* detector_ = nullptr;
    RaspbotBoard* board_ = nullptr;
    std::function<void()> trackingPump_;

    MiniLidarSDL miniLidar_{"/dev/ttyUSB0"};
    UnifiedGui gui_;
    m5stick::M5StickSerial m5stick_{"/dev/ttyACM0", 115200};

    RobotState robotState_{};
    BehaviorManager behaviorManager_{};
    NavRuntime navRuntime_{};
    NavManager navManager_{};
    OdomRuntime odomRuntime_{};
    LidarPoseRuntime lidarPoseRuntime_{};
    LidarCorrectionHintsRuntime lidarHintsRuntime_{};

    DebugFlags debugFlags_{};
    YawDebugLogger yawDebugLogger_{};
    NavOdomDebugLogger navOdomDebugLogger_{};
    NavLidarDebugLogger navLidarDebugLogger_{};
    ToClientJsonLogger toClientJsonLogger_{};
    DevFarmVideoRecorder devFarmVideoRecorder_{};
    DevFarmLidarMapper devFarmLidarMapper_{};
    DevFarmPoseProvider devFarmPoseProvider_{};
    Ros2SlamManager ros2SlamManager_{};
    Ros2OdomUdpPublisher ros2OdomUdpPublisher_{};

    LidarOwner lidarOwner_ = LidarOwner::None;

    uint64_t lastTrackingToggleMs_ = 0;
    uint64_t lastLidarToggleMs_ = 0;
    uint64_t lastImuToggleMs_ = 0;
    uint64_t lastEnvToggleMs_ = 0;
    uint64_t lastDevFarmVideoToggleMs_ = 0;
    uint64_t lastDevFarmLidarMapToggleMs_ = 0;
    uint64_t lastDevFarmLidarMapLoadMs_ = 0;
    uint64_t lastRos2MappingToggleMs_ = 0;
    uint64_t lastRos2MapLoadMs_ = 0;
    uint64_t lastToClientDebugToggleMs_ = 0;
    bool devFarmVideoWasRecording_ = false;

    bool prevR2Pressed_ = false;
    bool prevGoalReached_ = false;
    double lastRos2MapTrailX_ = 0.0;
    double lastRos2MapTrailY_ = 0.0;
    double lastRos2MapTrailYawDeg_ = 0.0;

    bool autoStopHoldActive_ = false;

    bool navGuardSafeStopLatched_ = false;
    bool navGuardHasPrevSample_ = false;

    uint64_t navGuardLastSampleMs_ = 0;
    uint64_t navFrozenAccumMs_ = 0;
    uint64_t navDegradedAccumMs_ = 0;

    NavPoseState navGuardPrevNav_{};
    OdomState navGuardPrevOdom_{};
    LidarPoseState navGuardPrevLidar_{};

    static constexpr uint64_t kTrackingToggleDebounceMs = 500;
    static constexpr uint64_t kLidarToggleDebounceMs = 300;
    static constexpr uint64_t kM5ToggleDebounceMs = 300;
    static constexpr uint64_t kM5FreshTimeoutMs = 1200;
    static constexpr uint64_t kM5ConnectedTimeoutMs = 5000;
    static constexpr uint64_t kDevFarmVideoToggleDebounceMs = 500;
    static constexpr uint64_t kDevFarmVideoMaxBytes = 1024ULL * 1024ULL * 1024ULL;
    static constexpr uint64_t kDevFarmLidarMapToggleDebounceMs = 500;
    static constexpr uint64_t kDevFarmLidarMapLoadDebounceMs = 500;
    static constexpr uint64_t kRos2MappingToggleDebounceMs = 800;
    static constexpr uint64_t kRos2MapLoadDebounceMs = 500;
    static constexpr uint64_t kToClientDebugToggleDebounceMs = 500;

    static int axisToSignedCmd(Sint16 v, int deadZone = 8000);
    static uint64_t nowMs();
};

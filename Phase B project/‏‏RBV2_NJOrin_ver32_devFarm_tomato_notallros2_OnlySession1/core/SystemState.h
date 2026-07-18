#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/RobotTypes.h"
#include "core/PerceptionTypes.h"
#include "core/LidarTypes.h"
#include "core/BehaviorTypes.h"

struct M5ImuState
{
    bool enabled = false;
    bool hwOk = false;
    bool readOk = false;

    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;

    double gx = 0.0;
    double gy = 0.0;
    double gz = 0.0;

    bool valid = false;
    bool isFresh = false;
    bool isStale = false;

    std::uint64_t timestampMs = 0;
};

struct M5EnvState
{
    bool enabled = false;
    bool hwOk = false;
    bool readOk = false;

    double tempC = 0.0;
    double humidityPct = 0.0;
    double pressureHpa = 0.0;
    double gasKohm = 0.0;

    bool valid = false;
    bool isFresh = false;
    bool isStale = false;

    std::uint64_t timestampMs = 0;
};

struct M5StickState
{
    bool connected = false;
    bool portOpen = false;

    bool imuEnabled = false;
    bool envEnabled = false;

    bool imuHwOk = false;
    bool envHwOk = false;

    bool statusValid = false;
    bool telemetryValid = false;

    bool statusFresh = false;
    bool telemetryFresh = false;

    bool statusStale = false;
    bool telemetryStale = false;

    std::uint64_t lastBootTimestampMs = 0;
    std::uint64_t lastStatusTimestampMs = 0;
    std::uint64_t lastTelemetryTimestampMs = 0;

    M5ImuState imu{};
    M5EnvState env{};
};

enum class NavPoseSource
{
    None,
    YawOnly,
    YawCmd,
    YawOdom,
    Slam
};

enum class NavMotionState
{
    Stop,
    Forward,
    Reverse
};

enum class NavTurnState
{
    Straight,
    Left,
    Right
};

struct NavPoseState
{
    double xMeters = 0.0;
    double yMeters = 0.0;
    double yawDeg = 0.0;

    double yawZeroDeg = 0.0;
    double yawRelativeDeg = 0.0;

    double linearVelocityMps = 0.0;
    double angularVelocityDegPs = 0.0;

    double forwardCommand = 0.0;
    double steeringCommand = 0.0;

    double goalXMeters = 0.0;
    double goalYMeters = 0.0;
    double goalDistanceMeters = 0.0;
    double goalBearingDeg = 0.0;
    double headingErrorDeg = 0.0;

    bool localAutoEnabled = false;
    bool localAutoActive = false;
    bool localAutoGoalReached = false;
    bool localAutoBlockedByEStop = false;

    double localAutoForwardCmd = 0.0;
    double localAutoSteeringCmd = 0.0;

    NavPoseSource poseSource = NavPoseSource::None;
    NavMotionState motionState = NavMotionState::Stop;
    NavTurnState turnState = NavTurnState::Straight;

    bool valid = false;
    bool yawValid = false;
    bool referenceInitialized = false;
    bool estimatedPose = false;
    bool goalActive = false;
    bool localized = false;
    bool mapReady = false;
    bool slamActive = false;
    bool trackingLost = false;

    bool isFresh = false;
    bool isStale = false;

    std::uint64_t timestampMs = 0;
};

enum class OdomPoseSource
{
    None,
    CmdYawRate,
    ImuYawRate,
    ImuYawRateCmdLinear,
    Encoder,
    Fused
};

struct OdomState
{
    double xMeters = 0.0;
    double yMeters = 0.0;
    double yawDeg = 0.0;

    double linearVelocityMps = 0.0;
    double angularVelocityDegPs = 0.0;

    double forwardCommand = 0.0;
    double steeringCommand = 0.0;

    double rawDtSec = 0.0;
    double dtSec = 0.0;

    OdomPoseSource poseSource = OdomPoseSource::None;

    bool valid = false;
    bool yawValid = false;
    bool referenceInitialized = false;
    bool integrationActive = false;
    bool estimatedPose = true;

    bool isFresh = false;
    bool isStale = false;

    std::uint64_t timestampMs = 0;
};

struct NavGuardState
{
    bool imuAvailableForNav = false;
    bool navPoseValid = false;
    bool navPoseFresh = false;

    bool movingCommand = false;
    bool odomMotionEvidence = false;
    bool lidarMotionEvidence = false;

    bool navFrozen = false;
    bool navDegraded = false;

    bool safeStopRequested = false;
    bool safeStopTriggered = false;

    std::uint64_t frozenAccumMs = 0;
    std::uint64_t degradedAccumMs = 0;
    std::uint64_t timestampMs = 0;
};

struct LidarCorrectionHintsState
{
    bool valid = false;
    bool isFresh = false;
    bool isStale = false;

    bool forwardClearanceOk = false;
    bool reverseClearanceOk = false;
    bool corridorCentered = false;

    bool steerCorrectionSuggested = false;
    bool reverseCorrectionSuggested = false;

    int suggestedSteerSign = 0;
    double suggestedSteerStrength = 0.0;

    int preferredReverseSide = 0;
    double reversePreferenceStrength = 0.0;

    double centerErrorM = 0.0;
    double headingHintDeg = 0.0;

    double frontClearanceM = -1.0;
    double rearClearanceM = -1.0;

    std::uint64_t timestampMs = 0;
};

struct DevFarmMapPoint
{
    double xM = 0.0;
    double yM = 0.0;

    double localXM = 0.0;
    double localYM = 0.0;

    double distanceM = 0.0;
    double angleDeg = 0.0;

    double robotXM = 0.0;
    double robotYM = 0.0;
    double robotYawDeg = 0.0;

    std::uint64_t timestampMs = 0;
    std::uint32_t hits = 0;
};

struct DevFarmMapState
{
    bool recording = false;
    bool loaded = false;
    bool valid = false;
    bool isFresh = false;

    bool usesOdomTransform = false;
    bool poseValid = false;
    bool poseFresh = false;
    bool poseRejected = false;
    bool mappingGateOpen = false;
    bool maxPointsReached = false;
    bool lastSaveOk = false;

    std::string poseSource;
    std::string mappingGateStatus;
    std::string mappingSkipReason;
    double lastPoseXM = 0.0;
    double lastPoseYM = 0.0;
    double lastPoseYawDeg = 0.0;
    double lastPoseDeltaM = 0.0;
    double lastPoseDeltaYawDeg = 0.0;

    bool slamLiteEnabled = false;
    bool slamMatchAttempted = false;
    bool slamMatchAccepted = false;
    bool slamMatchWeak = false;

    double slamMatchScore = 0.0;
    double slamBaseScore = 0.0;
    double slamMatchImprovement = 0.0;
    double slamDxM = 0.0;
    double slamDyM = 0.0;
    double slamDYawDeg = 0.0;

    std::uint64_t slamAttemptsCount = 0;
    std::uint64_t slamAcceptedCount = 0;
    std::size_t slamMapCells = 0;

    std::string outputPath;
    std::string latestPathFile;
    std::string stopReason;

    std::uint64_t startedAtMs = 0;
    std::uint64_t stoppedAtMs = 0;
    std::uint64_t timestampMs = 0;

    std::string mapMode;

    double gridResolutionM = 0.0;
    double occupancyResolutionM = 0.0;
    bool occupancyGridEnabled = false;
    bool rayClearingEnabled = false;

    std::size_t occupancyTotalCells = 0;
    std::size_t occupancyOccupiedCells = 0;
    std::size_t occupancyFreeCells = 0;

    std::uint64_t raysIntegratedCount = 0;
    std::uint64_t occupiedUpdatesCount = 0;
    std::uint64_t freeUpdatesCount = 0;
    std::uint64_t scansIntegratedCount = 0;
    std::uint64_t scansSkippedNoPoseCount = 0;
    std::uint64_t scansSkippedBadPoseCount = 0;
    std::uint64_t wallProtectionStopCount = 0;
    std::uint64_t endpointSnapCount = 0;

    std::size_t pointSampleStride = 0;
    std::size_t pointsCount = 0;
    std::size_t previewPointsCount = 0;
    std::uint64_t sourceSamplesCount = 0;
    std::uint64_t acceptedSamplesCount = 0;

    std::vector<DevFarmMapPoint> points;
    std::vector<DevFarmMapPoint> previewPoints;
};



enum class LidarOwner
{
    None,
    InternalMiniLidar,
    Ros2Mapping
};

struct Ros2SlamState
{
    bool mappingActive = false;
    bool latestMapLoaded = false;
    bool latestMapValid = false;
    bool lidarLockedByRos2 = false;
    bool odomBridgeActive = false;
    bool slamToolboxActive = false;
    bool saveRequested = false;
    bool lastSaveOk = false;

    std::string statusText;
    std::string activeSessionDir;
    std::string latestMapYaml;
    std::string latestMapPgm;
    std::string lastError;

    // Fix3: diagnostics for the C++ main-process odom UDP publisher.
    // These fields prove whether amain is actually sending estimated odom
    // to the ROS2 rbv2_odom_udp_bridge_node while R1 mapping is active.
    bool odomUdpRunning = false;
    bool odomUdpLastPublishOk = false;
    bool odomUdpInputValid = false;
    bool odomUdpInputFresh = false;
    std::uint64_t odomUdpPublishCalls = 0;
    std::uint64_t odomUdpPacketsSent = 0;
    std::uint64_t odomUdpKeepAlivePacketsSent = 0;
    std::uint64_t odomUdpSkippedThrottle = 0;
    std::uint64_t odomUdpSendErrors = 0;
    std::uint64_t odomUdpLastSentMs = 0;
    double odomUdpLastX = 0.0;
    double odomUdpLastY = 0.0;
    double odomUdpLastYawDeg = 0.0;
    double odomUdpLastLinearMps = 0.0;
    double odomUdpLastAngularDegPs = 0.0;

    std::uint64_t startedAtMs = 0;
    std::uint64_t stoppedAtMs = 0;
    std::uint64_t timestampMs = 0;
};


struct MapTrailPoint
{
    double xM = 0.0;
    double yM = 0.0;
    double yawDeg = 0.0;
    double distanceM = 0.0;
    std::uint64_t timestampMs = 0;
};

struct Ros2LoadedMapState
{
    bool loaded = false;
    bool valid = false;
    bool poseValid = false;

    std::string statusText;
    std::string lastError;
    std::string sessionDir;
    std::string mapYaml;
    std::string mapPgm;

    int width = 0;
    int height = 0;
    double resolutionM = 0.05;
    double originX = 0.0;
    double originY = 0.0;
    double originYawRad = 0.0;

    // Raw PGM occupancy image as saved by nav2_map_server / map_saver.
    // PGM convention normally used by ROS maps:
    //   low values  -> occupied
    //   high values -> free
    //   mid values  -> unknown
    std::vector<std::uint8_t> pixels;

    // Ver30 overlay pose: no localization / no navigation.
    // R2 loads the saved ROS map image as-is. The operator can click the
    // displayed map to set the robot start point and use keyboard 1/2 to
    // rotate the initial robot heading. Coordinates below are display-map
    // meters: X is image-right, Y is image-down.
    bool manualStartSet = false;
    double manualStartX = 0.0;
    double manualStartY = 0.0;
    double manualStartYawDeg = 0.0; // 0=up on screen, +right/clockwise

    double robotX = 0.0;
    double robotY = 0.0;
    double robotYawDeg = 0.0;
    double robotDistanceM = 0.0;

    std::size_t trailCount = 0;
    std::vector<MapTrailPoint> trail;

    std::uint64_t loadedAtMs = 0;
    std::uint64_t timestampMs = 0;
};

struct SystemHealth
{
    bool cameraAvailable = false;
    bool lidarAvailable = false;
    bool joystickConnected = false;
    bool m5stickAvailable = false;

    bool detectorRunning = false;
    bool trackerRunning = false;
    bool guiRunning = false;

    bool driveFresh = false;
    bool detectionsFresh = false;
    bool trackingFresh = false;
    bool lidarFresh = false;
    bool m5stickFresh = false;

    bool detectorStale = false;
    bool trackerStale = false;
    bool lidarStale = false;
    bool driveStale = false;
    bool m5stickStale = false;

    bool valid = false;
    std::uint64_t timestampMs = 0;
};

struct CameraServoState
{
    bool valid = false;

    // Physical servo angles. In this project the pan servo is the lower servo,
    // and the tilt servo is the upper servo.
    int panDeg = 90;
    int tiltDeg = 90;

    int centerPanDeg = 90;
    int centerTiltDeg = 90;

    double panRelativeDeg = 0.0;
    double tiltRelativeDeg = 0.0;

    // Direction vector relative to the robot body:
    // x_forward = forward, y_left = left, z_up = up.
    double dirForward = 1.0;
    double dirLeft = 0.0;
    double dirUp = 0.0;

    // Digital zoom applied by ObjectDetector before inference/display.
    // This is useful for Next.js reconstruction and greenhouse session review.
    double digitalZoom = 1.0;

    std::uint64_t timestampMs = 0;
};

struct RobotState
{
    DriveState drive{};
    TrackingState tracking{};
    LidarSnapshot lidar{};
    LidarSummary lidarSummary{};
    LidarPoseState lidarPose{};
    LidarCorrectionHintsState lidarHints{};
    DetectionSnapshot detections{};
    M5StickState m5stick{};
    NavPoseState nav{};
    OdomState odom{};
    NavGuardState navGuard{};
    CameraServoState cameraServo{};
    DevFarmMapState devFarmMap{};
    Ros2SlamState ros2Slam{};
    Ros2LoadedMapState ros2Map{};
    SystemHealth health{};
    BehaviorDecision behavior{};

    bool unifiedGuiOpen = false;
    bool emergencyStop = false;

    std::uint64_t timestampMs = 0;
};

#include "orchestration/JoystickController.h"
#include "control/DriveController.h"
#include "perception/TargetTracker.h"
#include "perception/ObjectDetector.h"
#include "hardware/RaspbotBoard.h"

#include <cmath>
#include <ctime>
#include <functional>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <utility>

namespace {
constexpr double PI_D = 3.14159265358979323846;
double absAngleDeltaDeg(double a, double b)
{
    double d = a - b;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return std::fabs(d);
}

bool validDist(double v)
{
    return v > 0.05;
}

std::string trimCopy(const std::string& in)
{
    std::size_t a = 0;
    while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
    std::size_t b = in.size();
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;
    return in.substr(a, b - a);
}

std::string unquoteCopy(std::string v)
{
    v = trimCopy(v);
    if (v.size() >= 2) {
        const char first = v.front();
        const char last = v.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return v.substr(1, v.size() - 2);
        }
    }
    return v;
}

bool parseYamlScalar(const std::string& line, const std::string& key, std::string& out)
{
    const std::string prefix = key + ":";
    const std::string t = trimCopy(line);
    if (t.rfind(prefix, 0) != 0) return false;
    out = unquoteCopy(t.substr(prefix.size()));
    return true;
}

}

JoystickController::JoystickController(TargetTracker* tracker,
                                       ObjectDetector* detector,
                                       RaspbotBoard* board)
    : tracker_(tracker), detector_(detector), board_(board) {
    gui_.setSources(detector_, &miniLidar_);
}

JoystickController::~JoystickController() {
    close();
}

void JoystickController::resetNavGuardMonitor()
{
    navGuardSafeStopLatched_ = false;
    navGuardHasPrevSample_ = false;
    navGuardLastSampleMs_ = 0;
    navFrozenAccumMs_ = 0;
    navDegradedAccumMs_ = 0;
    navGuardPrevNav_ = NavPoseState{};
    navGuardPrevOdom_ = OdomState{};
    navGuardPrevLidar_ = LidarPoseState{};
    robotState_.navGuard = NavGuardState{};
    robotState_.cameraServo = CameraServoState{};
}

bool JoystickController::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0) {
        std::cerr << "[joy] SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            gc_ = SDL_GameControllerOpen(i);
            if (gc_) break;
        }
    }

    if (!gc_) {
        std::cerr << "[joy] No SDL GameController found.\n";
        SDL_Quit();
        return false;
    }

    SDL_Joystick* js = SDL_GameControllerGetJoystick(gc_);
    haptic_ = SDL_HapticOpenFromJoystick(js);
    if (haptic_) {
        if (SDL_HapticRumbleInit(haptic_) != 0) {
            SDL_HapticClose(haptic_);
            haptic_ = nullptr;
        }
    }

    std::cout << "[joy] Controller: " << SDL_GameControllerName(gc_) << "\n";
    if (haptic_) {
        SDL_HapticRumblePlay(haptic_, 0.6f, 200);
    }

    sdl_ok_ = true;

    if (m5stick_.start()) {
        std::cout << "[joy] M5Stick serial connected\n";
        m5stick_.sendGetStatus();
    } else {
        std::cout << "[joy] M5Stick serial not available\n";
    }

    navRuntime_.reset();
    navManager_.reset();
    odomRuntime_.reset();
    lidarPoseRuntime_.reset();
    lidarHintsRuntime_.reset();
    resetNavGuardMonitor();
    lidarOwner_ = miniLidar_.isRunning() ? LidarOwner::InternalMiniLidar : LidarOwner::None;
    syncRos2SlamState();

    prevR2Pressed_ = false;
    prevGoalReached_ = false;
    autoStopHoldActive_ = false;

    if (debugFlags_.testYaw) {
        yawDebugLogger_.start("Debugging/test_yaw/yaw_debug_log.csv");
    }

    if (debugFlags_.testNavOdom) {
        navOdomDebugLogger_.start("Debugging/test_nav_odom/nav_odom_debug_log.csv");
    }

    if (debugFlags_.testNavLidar) {
        navLidarDebugLogger_.start("Debugging/test_nav_lidar/nav_lidar_debug_log.csv");
    }

    std::cout << "[toClient] TO_CLIENT_JSON runtime control ready: L1 starts a session, L2 stops it.\n";

    return true;
}

void JoystickController::close() {
    gui_.close();
    stopRos2Mapping("controller_close", nowMs());
    stopDevFarmVideo("controller_close", nowMs());
    stopDevFarmLidarMap("controller_close", nowMs());
    miniLidar_.stop();
    lidarOwner_ = LidarOwner::None;
    stopTrackingSafely();
    m5stick_.stop();
    navRuntime_.reset();
    navManager_.reset();
    odomRuntime_.reset();
    lidarPoseRuntime_.reset();
    lidarHintsRuntime_.reset();
    resetNavGuardMonitor();

    yawDebugLogger_.stop();
    navOdomDebugLogger_.stop();
    navLidarDebugLogger_.stop();
    toClientJsonLogger_.stop("controller_close", nowMs());

    const uint64_t now = nowMs();

    robotState_.health.guiRunning = false;
    robotState_.health.lidarAvailable = false;
    robotState_.health.detectorRunning = false;
    robotState_.health.trackerRunning = false;
    robotState_.health.joystickConnected = false;
    robotState_.health.m5stickAvailable = false;

    robotState_.drive.isFresh = false;
    robotState_.detections.isFresh = false;
    robotState_.tracking.isFresh = false;
    robotState_.lidar.isFresh = false;
    robotState_.lidarSummary.isFresh = false;

    robotState_.health.driveFresh = false;
    robotState_.health.detectionsFresh = false;
    robotState_.health.trackingFresh = false;
    robotState_.health.lidarFresh = false;
    robotState_.health.m5stickFresh = false;

    robotState_.health.driveStale = true;
    robotState_.health.detectorStale = true;
    robotState_.health.trackerStale = true;
    robotState_.health.lidarStale = true;
    robotState_.health.m5stickStale = true;

    robotState_.m5stick = M5StickState{};
    robotState_.nav = NavPoseState{};
    robotState_.odom = OdomState{};
    robotState_.lidarPose = LidarPoseState{};
    robotState_.lidarHints = LidarCorrectionHintsState{};
    robotState_.navGuard = NavGuardState{};

    robotState_.health.valid = true;
    robotState_.health.timestampMs = now;
    robotState_.unifiedGuiOpen = false;
    robotState_.lidarSummary = LidarSummary{};
    robotState_.timestampMs = now;
    robotState_.behavior = behaviorManager_.evaluate(robotState_);

    if (haptic_) {
        SDL_HapticClose(haptic_);
        haptic_ = nullptr;
    }

    if (gc_) {
        SDL_GameControllerClose(gc_);
        gc_ = nullptr;
    }

    if (sdl_ok_) {
        SDL_Quit();
        sdl_ok_ = false;
    }
}

void JoystickController::setTrackingPump(std::function<void()> pump) {
    trackingPump_ = std::move(pump);
}

void JoystickController::setDebugFlags(const DebugFlags& flags) {
    debugFlags_ = flags;
}

int JoystickController::axisToSignedCmd(Sint16 v, int deadZone) {
    if (std::abs(v) < deadZone) return 0;
    return (v > 0) ? +1 : -1;
}

uint64_t JoystickController::nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

double JoystickController::normalizeAngleDeg(double angleDeg) {
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}

bool JoystickController::angleInRange(double angleDeg, double minDeg, double maxDeg) {
    angleDeg = normalizeAngleDeg(angleDeg);
    minDeg = normalizeAngleDeg(minDeg);
    maxDeg = normalizeAngleDeg(maxDeg);

    if (minDeg <= maxDeg) {
        return angleDeg >= minDeg && angleDeg <= maxDeg;
    }

    return angleDeg >= minDeg || angleDeg <= maxDeg;
}

double JoystickController::computeSectorMinDistance(const std::vector<LidarPoint>& points,
                                                    double minAngleDeg,
                                                    double maxAngleDeg) {
    double best = -1.0;

    for (const auto& p : points) {
        if (p.dist <= 0.05) {
            continue;
        }

        const double angleDeg = std::atan2(p.y, p.x) * 180.0 / M_PI;
        if (!angleInRange(angleDeg, minAngleDeg, maxAngleDeg)) {
            continue;
        }

        if (best < 0.0 || p.dist < best) {
            best = p.dist;
        }
    }

    return best;
}

bool JoystickController::canToggleTracking(uint64_t nowMsValue) const {
    return (nowMsValue - lastTrackingToggleMs_) >= kTrackingToggleDebounceMs;
}

bool JoystickController::canToggleLidar(uint64_t nowMsValue) const {
    return (nowMsValue - lastLidarToggleMs_) >= kLidarToggleDebounceMs;
}

bool JoystickController::canToggleImu(uint64_t nowMsValue) const {
    return (nowMsValue - lastImuToggleMs_) >= kM5ToggleDebounceMs;
}

bool JoystickController::canToggleEnv(uint64_t nowMsValue) const {
    return (nowMsValue - lastEnvToggleMs_) >= kM5ToggleDebounceMs;
}

RobotState JoystickController::getRobotState() const {
    return robotState_;
}

BehaviorDecision JoystickController::getBehaviorDecision() const {
    return robotState_.behavior;
}

std::string JoystickController::makeDevFarmVideoPath()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmValue{};
    localtime_r(&t, &tmValue);

    std::ostringstream oss;
    oss << "Debugging/devFarm/videos/farm_video_"
        << std::put_time(&tmValue, "%Y%m%d_%H%M%S")
        << ".avi";
    return oss.str();
}

void JoystickController::beepBuzzer(unsigned durationMs)
{
    if (!board_) {
        std::cout << "[buzzer] board pointer is null\n";
        return;
    }

    if (!board_->isOpen()) {
        std::cout << "[buzzer] RaspbotBoard bus is not open\n";
        return;
    }

    board_->setBuzzer(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    board_->setBuzzer(false);
}

void JoystickController::beepPattern(unsigned count, unsigned durationMs, unsigned gapMs)
{
    if (count == 0) {
        return;
    }

    for (unsigned i = 0; i < count; ++i) {
        beepBuzzer(durationMs);
        if (i + 1 < count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
        }
    }
}

bool JoystickController::startDevFarmVideo(uint64_t nowMsValue)
{
    if (!detector_) {
        std::cout << "[devFarm][video] cannot record: ObjectDetector is null\n";
        return false;
    }

    if (tracker_ && tracker_->isEnabled()) {
        tracker_->stop();
    }

    if (!detector_->isReady()) {
        std::cout << "[devFarm][video] detector init/start for camera stream...\n";
        if (!detector_->init()) {
            std::cout << "[devFarm][video] detector init failed; recording not started\n";
            return false;
        }
    }

    if (!detector_->isRunning()) {
        detector_->start();
    }

    if (!detector_->isRunning()) {
        std::cout << "[devFarm][video] camera/detector failed to start\n";
        return false;
    }

    const std::string path = makeDevFarmVideoPath();
    const double fps = static_cast<double>(detector_->config().cameraFps);

    if (!devFarmVideoRecorder_.start(path, kDevFarmVideoMaxBytes, fps, nowMsValue)) {
        return false;
    }

    devFarmVideoWasRecording_ = true;
    beepBuzzer(120);

    std::cout << "[devFarm][video] z START recording up to 1GB: "
              << path << "\n";

    return true;
}

void JoystickController::stopDevFarmVideo(const std::string& reason, uint64_t nowMsValue)
{
    const bool wasRecording = devFarmVideoRecorder_.isRecording();

    devFarmVideoRecorder_.stop(reason, nowMsValue);
    devFarmVideoWasRecording_ = false;

    if (wasRecording) {
        beepBuzzer(120);
    }

    stopTrackingSafely();

    if (wasRecording) {
        std::cout << "[devFarm][video] z STOP camera stream. reason="
                  << reason << "\n";
    }
}

void JoystickController::toggleDevFarmVideo(uint64_t nowMsValue)
{
    if ((nowMsValue - lastDevFarmVideoToggleMs_) < kDevFarmVideoToggleDebounceMs) {
        return;
    }

    lastDevFarmVideoToggleMs_ = nowMsValue;

    if (devFarmVideoRecorder_.isRecording()) {
        stopDevFarmVideo("manual_z_toggle", nowMsValue);
    } else {
        startDevFarmVideo(nowMsValue);
    }
}

void JoystickController::updateDevFarmVideo(uint64_t nowMsValue)
{
    if (devFarmVideoRecorder_.isRecording()) {
        if (!detector_ || !detector_->isRunning()) {
            stopDevFarmVideo("camera_stream_stopped", nowMsValue);
            return;
        }

        ObjectDetector::Snapshot snapshot{};
        if (detector_->getLatestSnapshot(snapshot)) {
            devFarmVideoRecorder_.updateFromSnapshot(snapshot, nowMsValue);
        }
    }

    const bool nowRecording = devFarmVideoRecorder_.isRecording();
    const auto st = devFarmVideoRecorder_.getStatus();

    if (devFarmVideoWasRecording_ && !nowRecording) {
        if (st.maxSizeReached) {
            beepBuzzer(120);
            stopTrackingSafely();
            std::cout << "[devFarm][video] max size reached -> camera stream closed\n";
        }
    }

    devFarmVideoWasRecording_ = nowRecording;
}


bool JoystickController::canStartInternalLidar() const
{
    return lidarOwner_ != LidarOwner::Ros2Mapping &&
           !ros2SlamManager_.isMappingActive();
}

bool JoystickController::canStartRos2Mapping() const
{
    return lidarOwner_ != LidarOwner::InternalMiniLidar &&
           !miniLidar_.isRunning() &&
           !devFarmLidarMapper_.isRecording();
}

void JoystickController::resetRos2OdomSession(uint64_t nowMsValue)
{
    // RBV2 ver29 fix4:
    // Start every ROS2 mapping session from a clean estimated odom origin.
    // This makes each saved map easy to interpret and calibrate:
    // R1 START => odom x=0, y=0, yaw=0 for that session.
    navManager_.cancelLocalAuto();
    navManager_.resetLocalReference(navRuntime_.getState(), nowMsValue);

    odomRuntime_.reset();
    lidarPoseRuntime_.reset();
    lidarHintsRuntime_.reset();
    devFarmPoseProvider_.reset();
    resetNavGuardMonitor();

    robotState_.odom = odomRuntime_.getState();
    robotState_.lidarPose = lidarPoseRuntime_.getState();
    robotState_.lidarHints = lidarHintsRuntime_.getState();

    prevR2Pressed_ = false;
    prevGoalReached_ = false;
    autoStopHoldActive_ = false;
    navGuardSafeStopLatched_ = false;

    std::cout << "[ros2_slam] R1 odom session reset: x=0 y=0 yaw=0 "
              << "linear_scale=" << odomRuntime_.config().linearDistanceScale << "\n";
}


bool JoystickController::loadPgmImage(const std::string& path,
                                      int& width,
                                      int& height,
                                      std::vector<std::uint8_t>& pixels)
{
    width = 0;
    height = 0;
    pixels.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    auto nextToken = [&]() -> std::string {
        std::string token;
        char ch = 0;

        while (in.get(ch)) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                continue;
            }
            if (ch == '#') {
                std::string dummy;
                std::getline(in, dummy);
                continue;
            }
            token.push_back(ch);
            break;
        }

        while (in.get(ch)) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                break;
            }
            if (ch == '#') {
                std::string dummy;
                std::getline(in, dummy);
                break;
            }
            token.push_back(ch);
        }

        return token;
    };

    const std::string magic = nextToken();
    if (magic != "P5" && magic != "P2") {
        return false;
    }

    const std::string wTok = nextToken();
    const std::string hTok = nextToken();
    const std::string maxTok = nextToken();
    if (wTok.empty() || hTok.empty() || maxTok.empty()) {
        return false;
    }

    try {
        width = std::stoi(wTok);
        height = std::stoi(hTok);
        const int maxVal = std::stoi(maxTok);
        if (width <= 0 || height <= 0 || maxVal <= 0 || maxVal > 255) {
            return false;
        }
    } catch (...) {
        return false;
    }

    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels.assign(count, 205);

    if (magic == "P5") {
        in.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        return static_cast<std::size_t>(in.gcount()) == pixels.size();
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::string tok = nextToken();
        if (tok.empty()) return false;
        int v = 205;
        try { v = std::stoi(tok); } catch (...) { return false; }
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        pixels[i] = static_cast<std::uint8_t>(v);
    }

    return true;
}

bool JoystickController::loadRosMapYaml(const std::string& yamlPath,
                                        double& resolution,
                                        double& originX,
                                        double& originY,
                                        double& originYawRad,
                                        std::string& imagePath)
{
    resolution = 0.05;
    originX = 0.0;
    originY = 0.0;
    originYawRad = 0.0;
    imagePath.clear();

    std::ifstream in(yamlPath);
    if (!in) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string value;
        if (parseYamlScalar(line, "resolution", value)) {
            try { resolution = std::stod(value); } catch (...) {}
            continue;
        }
        if (parseYamlScalar(line, "image", value)) {
            imagePath = value;
            continue;
        }
        if (parseYamlScalar(line, "origin", value)) {
            for (char& c : value) {
                if (c == '[' || c == ']' || c == ',') c = ' ';
            }
            std::istringstream ss(value);
            ss >> originX >> originY >> originYawRad;
            continue;
        }
    }

    if (resolution <= 0.0) resolution = 0.05;

    if (imagePath.empty()) {
        imagePath = "map.pgm";
    }

    const std::filesystem::path imageFs(imagePath);
    if (!imageFs.is_absolute()) {
        imagePath = (std::filesystem::path(yamlPath).parent_path() / imageFs).lexically_normal().string();
    }

    return true;
}

void JoystickController::resetMapOverlayPose(uint64_t nowMsValue)
{
    navManager_.cancelLocalAuto();
    navManager_.resetLocalReference(navRuntime_.getState(), nowMsValue);

    odomRuntime_.reset();
    lidarPoseRuntime_.reset();
    lidarHintsRuntime_.reset();
    devFarmPoseProvider_.reset();
    resetNavGuardMonitor();

    robotState_.odom = odomRuntime_.getState();
    robotState_.lidarPose = lidarPoseRuntime_.getState();
    robotState_.lidarHints = lidarHintsRuntime_.getState();

    auto& m = robotState_.ros2Map;
    m.robotDistanceM = 0.0;
    m.timestampMs = nowMsValue;
    m.trail.clear();

    if (m.manualStartSet) {
        m.robotX = m.manualStartX;
        m.robotY = m.manualStartY;
        m.robotYawDeg = m.manualStartYawDeg;
        m.poseValid = true;
        m.trail.push_back(MapTrailPoint{m.robotX, m.robotY, m.robotYawDeg, 0.0, nowMsValue});
        std::cout << "[ros2_map_gui] pose overlay reset: manual start x="
                  << m.manualStartX << " y=" << m.manualStartY
                  << " yaw=" << m.manualStartYawDeg << "\n";
    } else {
        m.robotX = 0.0;
        m.robotY = 0.0;
        m.robotYawDeg = 0.0;
        m.poseValid = false;
        std::cout << "[ros2_map_gui] pose overlay reset: no manual start yet. Click inside the R2 map.\n";
    }

    m.trailCount = m.trail.size();
    lastRos2MapTrailX_ = m.robotX;
    lastRos2MapTrailY_ = m.robotY;
    lastRos2MapTrailYawDeg_ = m.robotYawDeg;
}

void JoystickController::setRos2MapManualStartPose(double mapX, double mapY, uint64_t nowMsValue)
{
    if (!robotState_.ros2Map.loaded || !robotState_.ros2Map.valid) {
        return;
    }

    const double maxX = std::max(0.0, static_cast<double>(robotState_.ros2Map.width) * robotState_.ros2Map.resolutionM);
    const double maxY = std::max(0.0, static_cast<double>(robotState_.ros2Map.height) * robotState_.ros2Map.resolutionM);

    robotState_.ros2Map.manualStartX = std::max(0.0, std::min(maxX, mapX));
    robotState_.ros2Map.manualStartY = std::max(0.0, std::min(maxY, mapY));
    robotState_.ros2Map.manualStartSet = true;
    robotState_.ros2Map.statusText = "manual map start set";

    resetMapOverlayPose(nowMsValue);
    beepPattern(1, 70, 40);

    std::cout << "[ros2_map_gui] mouse start set: x=" << robotState_.ros2Map.manualStartX
              << " y=" << robotState_.ros2Map.manualStartY
              << " yaw=" << robotState_.ros2Map.manualStartYawDeg << "\n";
}

void JoystickController::adjustRos2MapManualStartYaw(double deltaDeg, uint64_t nowMsValue)
{
    if (!robotState_.ros2Map.loaded || !robotState_.ros2Map.valid) {
        std::cout << "[ros2_map_gui] yaw adjust ignored: no ROS2 map loaded. Press R2 first.\n";
        return;
    }

    robotState_.ros2Map.manualStartYawDeg = normalizeAngleDeg(robotState_.ros2Map.manualStartYawDeg + deltaDeg);
    robotState_.ros2Map.manualStartSet = true;
    robotState_.ros2Map.statusText = "manual map yaw adjusted";
    resetMapOverlayPose(nowMsValue);
    beepPattern(1, 55, 35);

    std::cout << "[ros2_map_gui] manual start yaw -> "
              << robotState_.ros2Map.manualStartYawDeg << " deg\n";
}

bool JoystickController::loadRos2MapForGuiOverlay(uint64_t nowMsValue)
{
    const auto slamState = ros2SlamManager_.state();

    Ros2LoadedMapState map{};
    map.loadedAtMs = nowMsValue;
    map.timestampMs = nowMsValue;
    map.sessionDir = slamState.activeSessionDir;
    map.mapYaml = slamState.latestMapYaml;
    map.mapPgm = slamState.latestMapPgm;

    std::string yamlImagePath;
    if (!loadRosMapYaml(map.mapYaml, map.resolutionM, map.originX, map.originY, map.originYawRad, yamlImagePath)) {
        map.loaded = false;
        map.valid = false;
        map.lastError = "failed to read map yaml: " + map.mapYaml;
        map.statusText = "ROS2 map GUI load failed";
        robotState_.ros2Map = map;
        std::cout << "[ros2_map_gui] " << map.lastError << "\n";
        return false;
    }

    if (map.mapPgm.empty()) {
        map.mapPgm = yamlImagePath;
    }

    if (!loadPgmImage(map.mapPgm, map.width, map.height, map.pixels)) {
        // Fallback to the image path inside map.yaml if latest_map.json points elsewhere.
        map.mapPgm = yamlImagePath;
        if (!loadPgmImage(map.mapPgm, map.width, map.height, map.pixels)) {
            map.loaded = false;
            map.valid = false;
            map.lastError = "failed to read map pgm: " + map.mapPgm;
            map.statusText = "ROS2 map GUI load failed";
            robotState_.ros2Map = map;
            std::cout << "[ros2_map_gui] " << map.lastError << "\n";
            return false;
        }
    }

    map.loaded = true;
    map.valid = true;
    map.poseValid = false;
    map.statusText = "ROS2 map loaded - click map to set start";
    map.manualStartSet = false;
    map.manualStartX = 0.0;
    map.manualStartY = 0.0;
    map.manualStartYawDeg = 0.0;
    map.robotX = 0.0;
    map.robotY = 0.0;
    map.robotYawDeg = 0.0;
    map.robotDistanceM = 0.0;
    map.trail.clear();
    map.trailCount = 0;

    robotState_.ros2Map = std::move(map);
    resetMapOverlayPose(nowMsValue);

    std::cout << "[ros2_map_gui] R2 loaded map for LIDAR panel AS-IS: "
              << robotState_.ros2Map.mapYaml
              << " size=" << robotState_.ros2Map.width << "x" << robotState_.ros2Map.height
              << " res=" << robotState_.ros2Map.resolutionM
              << " origin=(" << robotState_.ros2Map.originX << "," << robotState_.ros2Map.originY << ")\n"
              << "[ros2_map_gui] click on the map to set robot start. Keys 1/2 rotate start yaw.\n";

    return true;
}

void JoystickController::updateRos2MapOverlayPose(uint64_t nowMsValue)
{
    if (!robotState_.ros2Map.loaded || !robotState_.ros2Map.valid) {
        return;
    }

    auto& m = robotState_.ros2Map;
    if (!m.manualStartSet) {
        m.poseValid = false;
        m.timestampMs = nowMsValue;
        return;
    }

    // Display-map convention: X=image-right, Y=image-down.
    // manualStartYawDeg: 0=up, +right/clockwise.
    const double startYawRad = m.manualStartYawDeg * PI_D / 180.0;
    const double s0 = std::sin(startYawRad);
    const double c0 = std::cos(startYawRad);

    const double odomForward = robotState_.odom.xMeters;
    const double odomRight = robotState_.odom.yMeters;

    m.robotX = m.manualStartX + odomForward * s0 + odomRight * c0;
    m.robotY = m.manualStartY - odomForward * c0 + odomRight * s0;

    // Keep the yaw sign convention that made left/right turns look correct in the GUI.
    m.robotYawDeg = normalizeAngleDeg(m.manualStartYawDeg - robotState_.odom.yawDeg);
    m.robotDistanceM = std::hypot(robotState_.odom.xMeters, robotState_.odom.yMeters);
    m.poseValid = robotState_.odom.valid;
    m.timestampMs = nowMsValue;

    const double dx = m.robotX - lastRos2MapTrailX_;
    const double dy = m.robotY - lastRos2MapTrailY_;
    const double dYaw = absAngleDeltaDeg(m.robotYawDeg, lastRos2MapTrailYawDeg_);
    const bool shouldAdd = m.trail.empty() ||
                           std::hypot(dx, dy) >= 0.04 ||
                           dYaw >= 5.0 ||
                           (nowMsValue - m.trail.back().timestampMs) >= 1500;

    if (shouldAdd) {
        m.trail.push_back(MapTrailPoint{
            m.robotX,
            m.robotY,
            m.robotYawDeg,
            m.robotDistanceM,
            nowMsValue
        });
        if (m.trail.size() > 2500) {
            m.trail.erase(m.trail.begin(), m.trail.begin() + 500);
        }
        m.trailCount = m.trail.size();
        lastRos2MapTrailX_ = m.robotX;
        lastRos2MapTrailY_ = m.robotY;
        lastRos2MapTrailYawDeg_ = m.robotYawDeg;
    }
}

void JoystickController::syncRos2SlamState()
{
    robotState_.ros2Slam = ros2SlamManager_.state();

    const auto odomStats = ros2OdomUdpPublisher_.stats();
    robotState_.ros2Slam.odomUdpRunning = odomStats.running;
    robotState_.ros2Slam.odomBridgeActive = odomStats.running;
    robotState_.ros2Slam.odomUdpLastPublishOk = odomStats.lastPublishOk;
    robotState_.ros2Slam.odomUdpInputValid = odomStats.lastInputValid;
    robotState_.ros2Slam.odomUdpInputFresh = odomStats.lastInputFresh;
    robotState_.ros2Slam.odomUdpPublishCalls = odomStats.publishCalls;
    robotState_.ros2Slam.odomUdpPacketsSent = odomStats.packetsSent;
    robotState_.ros2Slam.odomUdpKeepAlivePacketsSent = odomStats.keepAlivePacketsSent;
    robotState_.ros2Slam.odomUdpSkippedThrottle = odomStats.skippedThrottle;
    robotState_.ros2Slam.odomUdpSendErrors = odomStats.sendErrors;
    robotState_.ros2Slam.odomUdpLastSentMs = odomStats.lastSentMs;
    robotState_.ros2Slam.odomUdpLastX = odomStats.lastX;
    robotState_.ros2Slam.odomUdpLastY = odomStats.lastY;
    robotState_.ros2Slam.odomUdpLastYawDeg = odomStats.lastYawDeg;
    robotState_.ros2Slam.odomUdpLastLinearMps = odomStats.lastLinearMps;
    robotState_.ros2Slam.odomUdpLastAngularDegPs = odomStats.lastAngularDegPs;
}

bool JoystickController::startRos2Mapping(uint64_t nowMsValue)
{
    if ((nowMsValue - lastRos2MappingToggleMs_) < kRos2MappingToggleDebounceMs) {
        return false;
    }
    lastRos2MappingToggleMs_ = nowMsValue;

    if (!canStartRos2Mapping()) {
        std::cout << "[ros2_slam] R1 ignored: LiDAR is used by internal MiniLidarSDL/debug. "
                  << "Press Q to stop Mini LiDAR first.\n";
        syncRos2SlamState();
        return false;
    }

    resetRos2OdomSession(nowMsValue);

    if (!ros2OdomUdpPublisher_.start()) {
        std::cout << "[ros2_slam] failed to start odom UDP publisher: "
                  << ros2OdomUdpPublisher_.lastError() << "\n";
        syncRos2SlamState();
        return false;
    }

    const bool ok = ros2SlamManager_.startMapping(nowMsValue);
    if (ok) {
        lidarOwner_ = LidarOwner::Ros2Mapping;
        // R1 start feedback: one short beep.
        beepPattern(1, 85, 70);
        if (haptic_) {
            SDL_HapticRumblePlay(haptic_, 0.65f, 140);
        }
    } else {
        ros2OdomUdpPublisher_.stop();
    }

    syncRos2SlamState();
    return ok;
}

void JoystickController::stopRos2Mapping(const std::string& reason, uint64_t nowMsValue)
{
    const bool wasActive = ros2SlamManager_.isMappingActive();
    bool stopOk = true;
    if (wasActive) {
        stopOk = ros2SlamManager_.stopMapping(reason, nowMsValue);
        // R1 stop feedback: two short beeps on normal finish, three on save failure.
        beepPattern(stopOk ? 2u : 3u, 85, 80);
    }

    ros2OdomUdpPublisher_.stop();
    if (lidarOwner_ == LidarOwner::Ros2Mapping) {
        lidarOwner_ = LidarOwner::None;
    }
    syncRos2SlamState();
}

void JoystickController::toggleRos2Mapping(uint64_t nowMsValue)
{
    if (ros2SlamManager_.isMappingActive()) {
        stopRos2Mapping("manual_R1_toggle", nowMsValue);
    } else {
        startRos2Mapping(nowMsValue);
    }
}

bool JoystickController::loadLatestRos2Map(uint64_t nowMsValue)
{
    if ((nowMsValue - lastRos2MapLoadMs_) < kRos2MapLoadDebounceMs) {
        return false;
    }
    lastRos2MapLoadMs_ = nowMsValue;

    if (ros2SlamManager_.isMappingActive()) {
        std::cout << "[ros2_slam] R2 ignored: mapping is active. Press R1 first to stop and save.\n";
        // R2 ignored/failure feedback: two short beeps.
        beepPattern(2, 70, 70);
        syncRos2SlamState();
        return false;
    }

    const bool metadataOk = ros2SlamManager_.loadLatestMap(nowMsValue);
    const bool guiMapOk = metadataOk && loadRos2MapForGuiOverlay(nowMsValue);

    // R2 feedback: one short beep if latest map loaded into GUI, two if not.
    beepPattern(guiMapOk ? 1u : 2u, 70, 70);
    if (guiMapOk && haptic_) {
        SDL_HapticRumblePlay(haptic_, 0.45f, 100);
    }
    syncRos2SlamState();
    return guiMapOk;
}

void JoystickController::updateRos2MappingState(uint64_t nowMsValue)
{
    ros2SlamManager_.update(nowMsValue);

    if (ros2SlamManager_.isMappingActive()) {
        if (lidarOwner_ != LidarOwner::Ros2Mapping) {
            lidarOwner_ = LidarOwner::Ros2Mapping;
        }
        const bool publishOk = ros2OdomUdpPublisher_.publish(robotState_.odom, nowMsValue);
        if (!publishOk && !ros2OdomUdpPublisher_.lastError().empty()) {
            static std::uint64_t lastOdomUdpErrorPrintMs = 0;
            if (lastOdomUdpErrorPrintMs == 0 ||
                (nowMsValue - lastOdomUdpErrorPrintMs) > 1000) {
                lastOdomUdpErrorPrintMs = nowMsValue;
                std::cout << "[ros2_odom_udp] publish failed: "
                          << ros2OdomUdpPublisher_.lastError() << "\n";
            }
        }
    } else {
        if (lidarOwner_ == LidarOwner::Ros2Mapping) {
            lidarOwner_ = LidarOwner::None;
        }
        if (ros2OdomUdpPublisher_.isRunning()) {
            ros2OdomUdpPublisher_.stop();
        }
    }

    syncRos2SlamState();
}

void JoystickController::syncDevFarmLidarMapState()
{
    robotState_.devFarmMap = devFarmLidarMapper_.getState();
}

bool JoystickController::startDevFarmLidarMap(uint64_t nowMsValue)
{
    if (!canStartInternalLidar()) {
        std::cout << "[devFarm][lidar-map] x ignored: LiDAR is used by ROS2 mapping. Press R1 to stop mapping first.\n";
        syncDevFarmLidarMapState();
        return false;
    }

    if (!miniLidar_.isRunning()) {
        std::cout << "[devFarm][lidar-map] Mini LiDAR is OFF -> starting it automatically...\n";
        if (!miniLidar_.start()) {
            std::cout << "[devFarm][lidar-map] failed to start Mini LiDAR: "
                      << miniLidar_.getLastError() << "\n";
            syncDevFarmLidarMapState();
            return false;
        }
        lidarOwner_ = LidarOwner::InternalMiniLidar;
    } else {
        lidarOwner_ = LidarOwner::InternalMiniLidar;
    }

    devFarmPoseProvider_.reset();

    if (!devFarmLidarMapper_.start(nowMsValue)) {
        syncDevFarmLidarMapState();
        return false;
    }

    syncDevFarmLidarMapState();

    std::cout << "[devFarm][lidar-map] x START point-cloud recording\n";
    return true;
}

void JoystickController::stopDevFarmLidarMap(const std::string& reason, uint64_t nowMsValue)
{
    if (!devFarmLidarMapper_.isRecording()) {
        syncDevFarmLidarMapState();
        return;
    }

    devFarmLidarMapper_.stopAndSave(reason, nowMsValue);
    syncDevFarmLidarMapState();
}

void JoystickController::toggleDevFarmLidarMap(uint64_t nowMsValue)
{
    if ((nowMsValue - lastDevFarmLidarMapToggleMs_) < kDevFarmLidarMapToggleDebounceMs) {
        return;
    }

    lastDevFarmLidarMapToggleMs_ = nowMsValue;

    if (devFarmLidarMapper_.isRecording()) {
        stopDevFarmLidarMap("manual_x_toggle", nowMsValue);
    } else {
        startDevFarmLidarMap(nowMsValue);
    }
}

bool JoystickController::loadDevFarmLidarMap(uint64_t nowMsValue)
{
    if (ros2SlamManager_.isMappingActive()) {
        std::cout << "[devFarm][lidar-map] c ignored: ROS2 mapping is active. Press R1 to stop mapping first.\n";
        syncDevFarmLidarMapState();
        return false;
    }

    if ((nowMsValue - lastDevFarmLidarMapLoadMs_) < kDevFarmLidarMapLoadDebounceMs) {
        return false;
    }

    lastDevFarmLidarMapLoadMs_ = nowMsValue;

    if (devFarmLidarMapper_.isRecording()) {
        std::cout << "[devFarm][lidar-map] c ignored: mapping is recording. Press x first to save/stop.\n";
        syncDevFarmLidarMapState();
        return false;
    }

    const bool ok = devFarmLidarMapper_.loadLatestMap(nowMsValue);
    syncDevFarmLidarMapState();

    if (ok) {
        std::cout << "[devFarm][lidar-map] c LOAD latest map OK\n";
    } else {
        std::cout << "[devFarm][lidar-map] c LOAD latest map FAILED\n";
    }

    return ok;
}

void JoystickController::updateDevFarmLidarMap(uint64_t nowMsValue)
{
    if (devFarmLidarMapper_.isRecording()) {
        const DevFarmPoseProvider::Pose mappingPose =
            devFarmPoseProvider_.estimate(robotState_.odom, robotState_.nav, nowMsValue);

        if (!mappingPose.valid) {
            static std::uint64_t lastPoseWarnMs = 0;
            if (lastPoseWarnMs == 0 || (nowMsValue - lastPoseWarnMs) > 2000) {
                lastPoseWarnMs = nowMsValue;
                std::cout << "[devFarm][lidar-map] MAP WAIT POSE: "
                          << (mappingPose.rejectReason.empty() ? "pose_not_valid" : mappingPose.rejectReason)
                          << " odom(valid=" << robotState_.odom.valid
                          << ", ref=" << robotState_.odom.referenceInitialized
                          << ", yaw=" << robotState_.odom.yawValid
                          << ", fresh=" << robotState_.odom.isFresh
                          << ") nav(valid=" << robotState_.nav.valid
                          << ", ref=" << robotState_.nav.referenceInitialized
                          << ", yaw=" << robotState_.nav.yawValid
                          << ", fresh=" << robotState_.nav.isFresh
                          << ")\n";
            }
        }

        devFarmLidarMapper_.updateFromSnapshot(robotState_.lidar,
                                               mappingPose,
                                               nowMsValue);
    }

    syncDevFarmLidarMapState();
}

void JoystickController::startTrackingSafely() {
    // Tomato mode: A starts the camera detector only.
    // Servo TargetTracker is intentionally not started because multiple tomatoes/bunches
    // make the servo camera jitter. Stabilization is now frame-level inside ObjectDetector.
    if (!detector_) {
        return;
    }

    if (tracker_ && tracker_->isEnabled()) {
        tracker_->stop();
    }

    std::cout << "[joy] tomato detector init/start (servo tracking disabled)...\n";

    if (!detector_->isReady()) {
        if (!detector_->init()) {
            std::cout << "[joy] detector init failed\n";
            return;
        }
    }

    detector_->start();

    if (!detector_->isRunning()) {
        std::cout << "[joy] detector failed to start\n";
        return;
    }

    std::cout << "[joy] tomato detector ON + frame-level tracking ON\n";
}

void JoystickController::stopTrackingSafely() {
    if (tracker_) {
        tracker_->stop();
    }

    if (detector_) {
        detector_->stop();
    }
}

void JoystickController::updateM5StickState(uint64_t nowMsValue) {
    auto& ms = robotState_.m5stick;

    const auto boot = m5stick_.getLastBoot();
    const auto status = m5stick_.getLastStatus();
    const auto telemetry = m5stick_.getLastTelemetry();

    const std::uint64_t bootRx = m5stick_.getLastBootRxTimeMs();
    const std::uint64_t statusRx = m5stick_.getLastStatusRxTimeMs();
    const std::uint64_t telemetryRx = m5stick_.getLastTelemetryRxTimeMs();

    ms.portOpen = m5stick_.isRunning();

    const bool anyRecent =
        (bootRx > 0 && (nowMsValue - bootRx) <= kM5ConnectedTimeoutMs) ||
        (statusRx > 0 && (nowMsValue - statusRx) <= kM5ConnectedTimeoutMs) ||
        (telemetryRx > 0 && (nowMsValue - telemetryRx) <= kM5ConnectedTimeoutMs);

    ms.connected = ms.portOpen && anyRecent;

    if (boot) {
        ms.lastBootTimestampMs = bootRx;
        ms.imuHwOk = boot->imu_hw_ok;
        ms.envHwOk = boot->env_hw_ok;
    }

    if (status) {
        ms.statusValid = true;
        ms.lastStatusTimestampMs = statusRx;
        ms.imuEnabled = status->imu_enabled;
        ms.envEnabled = status->env_enabled;
        ms.imuHwOk = status->imu_hw_ok;
        ms.envHwOk = status->env_hw_ok;
    } else {
        ms.statusValid = false;
    }

    ms.statusFresh = (statusRx > 0) && ((nowMsValue - statusRx) <= kM5FreshTimeoutMs);
    ms.statusStale = ms.portOpen && !ms.statusFresh;

    if (telemetry) {
        ms.telemetryValid = true;
        ms.lastTelemetryTimestampMs = telemetryRx;

        ms.imu.enabled = ms.imuEnabled;
        ms.imu.hwOk = ms.imuHwOk;
        ms.imu.timestampMs = telemetryRx;

        if (telemetry->imu) {
            ms.imu.readOk = telemetry->imu->ok;
            ms.imu.ax = telemetry->imu->ax;
            ms.imu.ay = telemetry->imu->ay;
            ms.imu.az = telemetry->imu->az;
            ms.imu.gx = telemetry->imu->gx;
            ms.imu.gy = telemetry->imu->gy;
            ms.imu.gz = telemetry->imu->gz;
            ms.imu.valid = telemetry->imu->ok;
        } else {
            ms.imu.readOk = false;
            ms.imu.valid = false;
        }

        ms.env.enabled = ms.envEnabled;
        ms.env.hwOk = ms.envHwOk;
        ms.env.timestampMs = telemetryRx;

        if (telemetry->env) {
            ms.env.readOk = telemetry->env->ok;
            ms.env.tempC = telemetry->env->temp_c;
            ms.env.humidityPct = telemetry->env->humidity_pct;
            ms.env.pressureHpa = telemetry->env->pressure_hpa;
            ms.env.gasKohm = telemetry->env->gas_kohm;
            ms.env.valid = telemetry->env->ok;
        } else {
            ms.env.readOk = false;
            ms.env.valid = false;
        }
    } else {
        ms.telemetryValid = false;
    }

    ms.telemetryFresh = (telemetryRx > 0) && ((nowMsValue - telemetryRx) <= kM5FreshTimeoutMs);
    ms.telemetryStale = ms.portOpen && !ms.telemetryFresh;

    ms.imu.isFresh = ms.telemetryFresh && ms.imu.valid;
    ms.imu.isStale = ms.imu.enabled && !ms.imu.isFresh;

    ms.env.isFresh = ms.telemetryFresh && ms.env.valid;
    ms.env.isStale = ms.env.enabled && !ms.env.isFresh;
}

void JoystickController::updateOdomState(uint64_t nowMsValue) {
    odomRuntime_.update(
        robotState_.m5stick.imu,
        robotState_.drive,
        nowMsValue
    );

    robotState_.odom = odomRuntime_.getState();
}

void JoystickController::updateNavState(uint64_t nowMsValue) {
    const bool lidarFreshNow = robotState_.lidarPose.isFresh;
    const double steeringCmd = robotState_.drive.currentSteeringSpeed;
    const double forwardCmd = robotState_.drive.currentForwardSpeed;

    navRuntime_.updateFromImu(
        robotState_.m5stick.imu,
        lidarFreshNow,
        steeringCmd,
        forwardCmd,
        nowMsValue
    );

    navManager_.update(
        navRuntime_.getState(),
        robotState_.drive,
        nowMsValue
    );

    robotState_.nav = navManager_.getState();

    if (debugFlags_.testYaw && yawDebugLogger_.isEnabled()) {
        yawDebugLogger_.log(navRuntime_.getDebug(), robotState_.nav);
    }
}

void JoystickController::updateNavGuardState(uint64_t nowMsValue)
{
    auto& guard = robotState_.navGuard;
    guard = NavGuardState{};
    guard.timestampMs = nowMsValue;

    guard.imuAvailableForNav =
        robotState_.m5stick.imu.valid &&
        robotState_.m5stick.imu.isFresh;

    guard.navPoseValid = robotState_.nav.valid && robotState_.nav.yawValid;
    guard.navPoseFresh = robotState_.nav.isFresh;

    guard.movingCommand =
        std::fabs(robotState_.drive.currentForwardSpeed) > 1.0f ||
        std::fabs(robotState_.drive.currentSteeringSpeed) > 5.0f;

    if (!navGuardHasPrevSample_) {
        navGuardHasPrevSample_ = true;
        navGuardLastSampleMs_ = nowMsValue;
        navGuardPrevNav_ = robotState_.nav;
        navGuardPrevOdom_ = robotState_.odom;
        navGuardPrevLidar_ = robotState_.lidarPose;

        guard.frozenAccumMs = navFrozenAccumMs_;
        guard.degradedAccumMs = navDegradedAccumMs_;
        guard.safeStopTriggered = navGuardSafeStopLatched_;
        return;
    }

    const uint64_t dtMs = (nowMsValue > navGuardLastSampleMs_) ? (nowMsValue - navGuardLastSampleMs_) : 0;
    navGuardLastSampleMs_ = nowMsValue;

    const double navPosDelta =
        std::hypot(robotState_.nav.xMeters - navGuardPrevNav_.xMeters,
                   robotState_.nav.yMeters - navGuardPrevNav_.yMeters);
    const double navYawDelta =
        absAngleDeltaDeg(robotState_.nav.yawRelativeDeg, navGuardPrevNav_.yawRelativeDeg);

    const double odomPosDelta =
        std::hypot(robotState_.odom.xMeters - navGuardPrevOdom_.xMeters,
                   robotState_.odom.yMeters - navGuardPrevOdom_.yMeters);
    const double odomYawDelta =
        absAngleDeltaDeg(robotState_.odom.yawDeg, navGuardPrevOdom_.yawDeg);

    guard.odomMotionEvidence =
        (odomPosDelta > 0.010) || (odomYawDelta > 2.5);

    double lidarDeltaMax = 0.0;
    auto updateLidarDelta = [&](double a, double b) {
        if (validDist(a) && validDist(b)) {
            lidarDeltaMax = std::max(lidarDeltaMax, std::fabs(a - b));
        }
    };

    updateLidarDelta(robotState_.lidarPose.frontDistanceM, navGuardPrevLidar_.frontDistanceM);
    updateLidarDelta(robotState_.lidarPose.leftDistanceM, navGuardPrevLidar_.leftDistanceM);
    updateLidarDelta(robotState_.lidarPose.rightDistanceM, navGuardPrevLidar_.rightDistanceM);
    updateLidarDelta(robotState_.lidarPose.rearDistanceM, navGuardPrevLidar_.rearDistanceM);
    updateLidarDelta(robotState_.lidarPose.frontLeftDistanceM, navGuardPrevLidar_.frontLeftDistanceM);
    updateLidarDelta(robotState_.lidarPose.frontRightDistanceM, navGuardPrevLidar_.frontRightDistanceM);

    guard.lidarMotionEvidence =
        robotState_.lidarPose.valid &&
        robotState_.lidarPose.isFresh &&
        (lidarDeltaMax > 0.06);

    const bool navLooksFrozen =
        guard.movingCommand &&
        guard.navPoseValid &&
        guard.navPoseFresh &&
        (navPosDelta < 0.005) &&
        (navYawDelta < 1.0) &&
        (guard.odomMotionEvidence || guard.lidarMotionEvidence);

    if (navLooksFrozen) {
        navFrozenAccumMs_ += dtMs;
    } else {
        navFrozenAccumMs_ = 0;
    }

    guard.navFrozen = (navFrozenAccumMs_ >= 700);

    const bool navUnavailableWhileMoving =
        guard.movingCommand &&
        (!guard.imuAvailableForNav || !guard.navPoseValid || !guard.navPoseFresh);

    guard.navDegraded =
        navUnavailableWhileMoving ||
        guard.navFrozen;

    if (guard.navDegraded) {
        navDegradedAccumMs_ += dtMs;
    } else {
        navDegradedAccumMs_ = 0;
    }

    guard.safeStopRequested =
        robotState_.nav.localAutoEnabled &&
        robotState_.nav.localAutoActive &&
        guard.movingCommand &&
        (navDegradedAccumMs_ >= 500);

    guard.safeStopTriggered = navGuardSafeStopLatched_;
    guard.frozenAccumMs = navFrozenAccumMs_;
    guard.degradedAccumMs = navDegradedAccumMs_;

    navGuardPrevNav_ = robotState_.nav;
    navGuardPrevOdom_ = robotState_.odom;
    navGuardPrevLidar_ = robotState_.lidarPose;
}

void JoystickController::logNavOdomIfNeeded()
{
    if (debugFlags_.testNavOdom && navOdomDebugLogger_.isEnabled()) {
        navOdomDebugLogger_.log(
            navRuntime_.getDebug(),
            robotState_.nav,
            robotState_.odom,
            robotState_.drive
        );
    }
}

void JoystickController::logNavLidarIfNeeded()
{
    if (debugFlags_.testNavLidar && navLidarDebugLogger_.isEnabled()) {
        navLidarDebugLogger_.log(
            robotState_.nav,
            robotState_.odom,
            robotState_.lidarPose,
            robotState_.lidarHints,
            robotState_.navGuard,
            robotState_.drive
        );
    }
}

void JoystickController::logToClientIfNeeded()
{
    if (toClientJsonLogger_.isEnabled()) {
        toClientJsonLogger_.log(robotState_);
    }
}

bool JoystickController::startToClientDebugSession(uint64_t nowMsValue)
{
    // L1 is the direct runtime control for TO_CLIENT_JSON sessions in joystick mode.
    // Do not require the old main debug-menu flag; that flag is for other debug flows.
    if ((nowMsValue - lastToClientDebugToggleMs_) < kToClientDebugToggleDebounceMs) {
        return false;
    }
    lastToClientDebugToggleMs_ = nowMsValue;

    if (toClientJsonLogger_.isEnabled()) {
        std::cout << "[toClient] L1 ignored: session already running at "
                  << toClientJsonLogger_.sessionDir() << "\n";
        return true;
    }

    // Start camera/detector automatically if the user did not press Cross/X yet.
    // L2 stops only the toClient session; it does not stop the tomato detector.
    if (detector_ && !detector_->isRunning()) {
        startTrackingSafely();
    }

    ToClientJsonLogger::Config cfg;
    cfg.baseDir = "Debugging/toClient";
    cfg.timelinePeriodMs = 250;
    cfg.latestPeriodMs = 250;
    cfg.okImagePeriodMs = 1000;
    cfg.weakImagePeriodMs = 1500;
    cfg.weakImageMinCount = 3;
    cfg.jpegQuality = 82;
    cfg.videoFps = 10.0;
    cfg.videoMaxWidth = 1280;
    cfg.videoMaxHeight = 720;
    cfg.videoBitrateKbps = 2500;
    cfg.videoMaxBytes = 1024ULL * 1024ULL * 1024ULL;

    const bool ok = toClientJsonLogger_.startSession("Debugging/toClient", nowMsValue, cfg);
    if (ok) {
        beepBuzzer(100);
        if (haptic_) {
            SDL_HapticRumblePlay(haptic_, 0.55f, 120);
        }
    }
    return ok;
}

void JoystickController::stopToClientDebugSession(const std::string& reason, uint64_t nowMsValue)
{
    const bool manualStop = (reason == "manual_L2");
    if (manualStop && (nowMsValue - lastToClientDebugToggleMs_) < kToClientDebugToggleDebounceMs) {
        return;
    }
    if (manualStop) {
        lastToClientDebugToggleMs_ = nowMsValue;
    }

    if (!toClientJsonLogger_.isEnabled()) {
        return;
    }

    toClientJsonLogger_.stop(reason, nowMsValue);
    beepBuzzer(100);
    if (haptic_) {
        SDL_HapticRumblePlay(haptic_, 0.35f, 100);
    }
}

void JoystickController::updateCameraServoState(uint64_t nowMsValue)
{
    CameraServoState servo{};
    servo.timestampMs = nowMsValue;

    if (tracker_) {
        const auto angles = tracker_->cameraAngles();
        servo.valid = true;
        servo.panDeg = angles.first;
        servo.tiltDeg = angles.second;

        // Current project calibration. Change these two constants if you later
        // recalibrate the mechanical center of the pan/tilt bracket.
        servo.centerPanDeg = 90;
        servo.centerTiltDeg = 90;

        servo.panRelativeDeg = static_cast<double>(servo.panDeg - servo.centerPanDeg);
        servo.tiltRelativeDeg = static_cast<double>(servo.centerTiltDeg - servo.tiltDeg);

        const double panRad = servo.panRelativeDeg * M_PI / 180.0;
        const double tiltRad = servo.tiltRelativeDeg * M_PI / 180.0;

        servo.dirForward = std::cos(tiltRad) * std::cos(panRad);
        servo.dirLeft = std::cos(tiltRad) * std::sin(panRad);
        servo.dirUp = std::sin(tiltRad);
    }

    if (detector_) {
        servo.digitalZoom = static_cast<double>(detector_->getDigitalZoom());
    }

    robotState_.cameraServo = servo;
}

void JoystickController::updateRobotState(DriveController& drive, uint64_t nowMsValue) {
    robotState_.drive = drive.getDriveState();
    robotState_.drive.lastUpdateTimeMs = nowMsValue;
    robotState_.drive.isFresh =
        (nowMsValue - robotState_.drive.lastUpdateTimeMs) <= robotState_.drive.timeoutMs;

    if (detector_) {
        ObjectDetector::Snapshot snapshot{};
        if (detector_->getLatestSnapshot(snapshot)) {
            robotState_.detections = snapshot;
            robotState_.detections.lastUpdateTimeMs =
                snapshot.frame.timestampMs > 0 ? snapshot.frame.timestampMs : nowMsValue;
            robotState_.detections.valid = true;
        } else {
            robotState_.detections = DetectionSnapshot{};
            robotState_.detections.valid = false;
        }
    } else {
        robotState_.detections = DetectionSnapshot{};
    }

    if (robotState_.detections.lastUpdateTimeMs > 0) {
        robotState_.detections.isFresh =
            (nowMsValue - robotState_.detections.lastUpdateTimeMs) <= robotState_.detections.timeoutMs;
    } else {
        robotState_.detections.isFresh = false;
    }

    if (miniLidar_.isRunning()) {
        std::vector<MiniLidarSDL::LidarPoint> rawPoints;
        miniLidar_.getLatestPoints(rawPoints);

        robotState_.lidar.points.clear();
        robotState_.lidar.points.reserve(rawPoints.size());

        for (const auto& p : rawPoints) {
            LidarPoint lp;
            lp.x = p.x;
            lp.y = p.y;
            lp.dist = p.dist;
            robotState_.lidar.points.push_back(lp);
        }

        robotState_.lidar.valid = !robotState_.lidar.points.empty();
        robotState_.lidar.timestampMs = nowMsValue;
        robotState_.lidar.isFresh =
            robotState_.lidar.valid &&
            ((nowMsValue - robotState_.lidar.timestampMs) <= robotState_.lidar.timeoutMs);

        const double rawFront =
            computeSectorMinDistance(robotState_.lidar.points, -30.0, 30.0);
        const double rawLeft =
            computeSectorMinDistance(robotState_.lidar.points, 60.0, 120.0);
        const double rawRight =
            computeSectorMinDistance(robotState_.lidar.points, -120.0, -60.0);
        const double rawRear =
            computeSectorMinDistance(robotState_.lidar.points, 150.0, -150.0);

        robotState_.lidarSummary.frontMinMeters = rawLeft;
        robotState_.lidarSummary.rightMinMeters = rawFront;
        robotState_.lidarSummary.leftMinMeters  = rawRear;
        robotState_.lidarSummary.rearMinMeters  = rawRight;

        const double front = robotState_.lidarSummary.frontMinMeters;
        const double left  = robotState_.lidarSummary.leftMinMeters;
        const double right = robotState_.lidarSummary.rightMinMeters;

        robotState_.lidarSummary.frontObstacleClose =
            (front > 0.0 && front < 0.50);
        robotState_.lidarSummary.leftObstacleClose =
            (left > 0.0 && left < 0.50);
        robotState_.lidarSummary.rightObstacleClose =
            (right > 0.0 && right < 0.50);

        robotState_.lidarSummary.obstacleClose =
            robotState_.lidarSummary.frontObstacleClose ||
            robotState_.lidarSummary.leftObstacleClose ||
            robotState_.lidarSummary.rightObstacleClose;

        robotState_.lidarSummary.valid = robotState_.lidar.valid;
        robotState_.lidarSummary.timestampMs = nowMsValue;
        robotState_.lidarSummary.isFresh =
            robotState_.lidarSummary.valid &&
            ((nowMsValue - robotState_.lidarSummary.timestampMs) <= robotState_.lidarSummary.timeoutMs);

        lidarPoseRuntime_.update(robotState_.lidar, nowMsValue);
        robotState_.lidarPose = lidarPoseRuntime_.getState();

        lidarHintsRuntime_.update(robotState_.lidarPose, robotState_.nav, nowMsValue);
        robotState_.lidarHints = lidarHintsRuntime_.getState();
    } else {
        robotState_.lidar = LidarSnapshot{};
        robotState_.lidarSummary = LidarSummary{};
        robotState_.lidarPose = LidarPoseState{};
        robotState_.lidarHints = LidarCorrectionHintsState{};
    }

    // Frame-level tracking state: this is based on stable ObjectDetector tracks only.
    // It does not command the servo camera.
    robotState_.tracking.trackingEnabled =
        (detector_ != nullptr) && detector_->isRunning();

    robotState_.tracking.targetSelected = false;
    robotState_.tracking.confidence = 0.0f;
    robotState_.tracking.targetOffsetX = 0.0f;
    robotState_.tracking.targetOffsetY = 0.0f;

    if (robotState_.tracking.trackingEnabled &&
        robotState_.detections.valid &&
        robotState_.detections.isFresh &&
        !robotState_.detections.detections.empty()) {
        const Detection* best = nullptr;

        for (const auto& d : robotState_.detections.detections) {
            if (!d.valid || d.weak) continue;
            if (!best || d.confidence > best->confidence) {
                best = &d;
            }
        }

        if (best) {
            robotState_.tracking.targetSelected = true;
            robotState_.tracking.confidence = best->confidence;

            const float cx = best->x + best->w * 0.5f;
            const float cy = best->y + best->h * 0.5f;
            const float frameCx = robotState_.detections.frame.width  * 0.5f;
            const float frameCy = robotState_.detections.frame.height * 0.5f;

            robotState_.tracking.targetOffsetX = cx - frameCx;
            robotState_.tracking.targetOffsetY = cy - frameCy;
        }
    }

    if (robotState_.tracking.trackingEnabled) {
        robotState_.tracking.lastUpdateTimeMs =
            robotState_.detections.lastUpdateTimeMs > 0
                ? robotState_.detections.lastUpdateTimeMs
                : nowMsValue;
    } else {
        robotState_.tracking.lastUpdateTimeMs = 0;
    }

    if (robotState_.tracking.lastUpdateTimeMs > 0 && robotState_.tracking.trackingEnabled) {
        robotState_.tracking.isFresh =
            (nowMsValue - robotState_.tracking.lastUpdateTimeMs) <= robotState_.tracking.timeoutMs;
    } else {
        robotState_.tracking.isFresh = false;
    }

    updateCameraServoState(nowMsValue);
    updateM5StickState(nowMsValue);
    updateOdomState(nowMsValue);
    updateNavState(nowMsValue);
    updateNavGuardState(nowMsValue);
    updateDevFarmLidarMap(nowMsValue);
    updateDevFarmVideo(nowMsValue);
    updateRos2MappingState(nowMsValue);
    updateRos2MapOverlayPose(nowMsValue);

    robotState_.health.cameraAvailable = detector_ != nullptr;
    robotState_.health.lidarAvailable = miniLidar_.isRunning() || ros2SlamManager_.isMappingActive() || robotState_.ros2Map.loaded;
    robotState_.health.joystickConnected = (gc_ != nullptr);
    robotState_.health.m5stickAvailable = robotState_.m5stick.connected;

    robotState_.health.detectorRunning = (detector_ != nullptr) && detector_->isRunning();
    robotState_.health.trackerRunning = false; // servo TargetTracker disabled in tomato mode
    robotState_.health.guiRunning = gui_.isOpen();

    robotState_.health.driveFresh = robotState_.drive.isFresh;
    robotState_.health.detectionsFresh = robotState_.detections.isFresh;
    robotState_.health.trackingFresh = robotState_.tracking.isFresh;
    robotState_.health.lidarFresh = robotState_.lidarPose.isFresh;
    robotState_.health.m5stickFresh =
        robotState_.m5stick.statusFresh || robotState_.m5stick.telemetryFresh;

    robotState_.health.driveStale = !robotState_.drive.isFresh;
    robotState_.health.detectorStale =
        robotState_.health.detectorRunning && !robotState_.detections.isFresh;
    robotState_.health.trackerStale = false;
    robotState_.health.lidarStale =
        robotState_.health.lidarAvailable && robotState_.lidarPose.isStale;
    robotState_.health.m5stickStale =
        robotState_.m5stick.portOpen && !robotState_.health.m5stickFresh;

    robotState_.health.valid = true;
    robotState_.health.timestampMs = nowMsValue;

    robotState_.unifiedGuiOpen = gui_.isOpen();
    robotState_.emergencyStop = robotState_.drive.emergencyStop;
    robotState_.timestampMs = nowMsValue;

    robotState_.behavior = behaviorManager_.evaluate(robotState_);
}

bool JoystickController::run(DriveController& drive) {
    if (!sdl_ok_ || !gc_) {
        std::cerr << "[joy] Not initialized.\n";
        return false;
    }

    navRuntime_.reset();
    navManager_.reset();
    odomRuntime_.reset();
    lidarPoseRuntime_.reset();
    lidarHintsRuntime_.reset();
    resetNavGuardMonitor();
    lidarOwner_ = miniLidar_.isRunning() ? LidarOwner::InternalMiniLidar : LidarOwner::None;
    syncRos2SlamState();

    prevR2Pressed_ = false;
    prevGoalReached_ = false;
    autoStopHoldActive_ = false;
    robotState_.nav = NavPoseState{};
    robotState_.odom = OdomState{};
    robotState_.lidarPose = LidarPoseState{};
    robotState_.lidarHints = LidarCorrectionHintsState{};
    robotState_.navGuard = NavGuardState{};

    if (!gui_.open()) {
        std::cerr << "[joy] Failed to open Unified GUI.\n";
        return false;
    }

    const int SPEED_STEP   = 5;
    const int FB_DEADZONE  = 8000;
    const int LR_DEADZONE  = 10000;
    const int L2_THRESHOLD = 20000;
    const int R2_THRESHOLD = 20000;
    const int HOLD_RELEASE_AXIS_THRESHOLD = 22000;

    const double kGoalEditStep = 0.01;
    const int SERVO_MANUAL_STEP_DEG = 2;
    const int SERVO_MANUAL_FINE_STEP_DEG = 1;
    const uint64_t SERVO_MANUAL_REPEAT_MS = 70;
    const uint64_t SERVO_MANUAL_FINE_REPEAT_MS = 110;

    bool l2Latched = false;
    uint64_t lastManualServoMoveMs = 0;

    drive.stopAll();
    drive.resetSpeeds();
    drive.setEmergencyStop(false, nowMs());

    std::cout << "\n[joy] Joystick drive mode:\n"
              << "  Left stick Y : drive forward/backward (CH1)\n"
              << "  Right stick X: steer left/right (CH2)\n"
              << "  Joystick buttons:\n"
              << "    Cross/X -> toggle tomato detector / frame tracking\n"
              << "    D-PAD   -> manual camera servo: up/down tilt, left/right pan\n"
              << "    L1      -> START TO_CLIENT_JSON debug session\n"
              << "    L2      -> STOP TO_CLIENT_JSON debug session\n"
              << "    R1      -> START/STOP ROS2 slam_toolbox mapping\n"
              << "    R2      -> LOAD latest ROS2 map into LIDAR GUI\n"
              << "    Mouse   -> click loaded ROS2 map to set robot start\n"
              << "    Square  -> camera digital zoom in\n"
              << "    Circle  -> camera digital zoom out\n"
              << "    Y      -> toggle E-STOP\n"
              << "    START  -> back to menu\n"
              << "    BACK   -> quit program\n"
              << "  Keyboard while GUI open:\n"
              << "    Q     -> toggle Mini LiDAR (blocked while R1 ROS2 mapping is active)\n"
              << "    A     -> reset local pose + yaw reference + odom\n"
              << "    W     -> toggle M5 IMU\n"
              << "    S     -> toggle M5 ENV\n"
              << "    E     -> toggle local auto nav\n"
              << "    1/2   -> rotate ROS2 map start yaw left/right\n"
              << "    U/J   -> FB speed CH1 +/-\n"
              << "    I/K   -> LR steering speed CH2 +/-\n"
              << "    T/G   -> GX +/- 0.01m\n"
              << "    Y/H   -> GY +/- 0.01m\n"
              << "    R     -> reset goal to default\n"
              << "    Z     -> toggle devFarm camera video recording up to 1GB\n"
              << "    X     -> toggle devFarm LiDAR map recording to JSON\n"
              << "    C     -> load latest devFarm LiDAR map from JSON\n\n"
              << "[speed] CH1(FB)=" << drive.getFbSpeed()
              << "  CH2(LR)=" << drive.getLrSpeed() << "\n\n";

    bool quit_all  = false;
    bool back_menu = false;

    auto toggleMiniLidarFromKeyboard = [&]() {
        if (drive.isEmergencyStopActive()) {
            return;
        }

        const uint64_t tNow = nowMs();
        if (!canToggleLidar(tNow)) {
            return;
        }

        if (!miniLidar_.isRunning() && !canStartInternalLidar()) {
            std::cout << "[key] Q ignored: LiDAR is used by ROS2 mapping. Press R1 to stop mapping first.\n";
            return;
        }

        lastLidarToggleMs_ = tNow;

        miniLidar_.toggle();
        lidarOwner_ = miniLidar_.isRunning() ? LidarOwner::InternalMiniLidar : LidarOwner::None;
        std::cout << "[key] Q -> Mini LiDAR "
                  << (miniLidar_.isRunning() ? "ON" : "OFF") << "\n";

        if (haptic_) {
            SDL_HapticRumblePlay(haptic_, 0.5f, 100);
        }
    };

    auto resetLocalPoseFromKeyboard = [&]() {
        const uint64_t tNow = nowMs();

        navManager_.cancelLocalAuto();
        navManager_.resetLocalReference(
            navRuntime_.getState(),
            tNow
        );

        odomRuntime_.reset();
        lidarPoseRuntime_.reset();
        lidarHintsRuntime_.reset();
        devFarmPoseProvider_.reset();
        resetNavGuardMonitor();

        robotState_.odom = odomRuntime_.getState();
        robotState_.lidarPose = lidarPoseRuntime_.getState();
        robotState_.lidarHints = lidarHintsRuntime_.getState();

        drive.stopAll();
        prevR2Pressed_ = false;
        prevGoalReached_ = false;
        autoStopHoldActive_ = false;

        updateRobotState(drive, tNow);
        updateOdomState(tNow);
        logNavOdomIfNeeded();
        logNavLidarIfNeeded();
        logToClientIfNeeded();

        std::cout << "[key] A -> local pose + yaw reference + odom reset\n";

        if (haptic_) {
            SDL_HapticRumblePlay(haptic_, 0.55f, 120);
        }
    };

    auto toggleM5ImuFromKeyboard = [&]() {
        if (drive.isEmergencyStopActive()) {
            return;
        }

        const uint64_t tNow = nowMs();
        if (!canToggleImu(tNow)) {
            return;
        }
        lastImuToggleMs_ = tNow;

        const bool newEnabled = !robotState_.m5stick.imuEnabled;
        if (m5stick_.sendSetSensor("imu", newEnabled)) {
            std::cout << "[key] W -> M5 IMU "
                      << (newEnabled ? "ON" : "OFF") << "\n";
            if (haptic_) {
                SDL_HapticRumblePlay(haptic_, 0.4f, 90);
            }
        }
    };

    auto toggleM5EnvFromKeyboard = [&]() {
        if (drive.isEmergencyStopActive()) {
            return;
        }

        const uint64_t tNow = nowMs();
        if (!canToggleEnv(tNow)) {
            return;
        }
        lastEnvToggleMs_ = tNow;

        const bool newEnabled = !robotState_.m5stick.envEnabled;
        if (m5stick_.sendSetSensor("env", newEnabled)) {
            std::cout << "[key] S -> M5 ENV "
                      << (newEnabled ? "ON" : "OFF") << "\n";
            if (haptic_) {
                SDL_HapticRumblePlay(haptic_, 0.4f, 90);
            }
        }
    };

    auto toggleLocalAutoNavFromKeyboard = [&]() {
        if (drive.isEmergencyStopActive()) {
            return;
        }

        navManager_.toggleLocalAutoEnabled();
        robotState_.nav = navManager_.getState();
        prevGoalReached_ = false;
        autoStopHoldActive_ = false;
        navGuardSafeStopLatched_ = false;

        std::cout << "[key] E -> local auto nav "
                  << (navManager_.isLocalAutoEnabled() ? "ON" : "OFF") << "\n";

        if (haptic_) {
            SDL_HapticRumblePlay(haptic_, 0.45f, 100);
        }
    };

    auto adjustCameraZoomFromJoystick = [&](float delta, const char* buttonName) {
        if (!detector_) {
            return;
        }

        detector_->adjustDigitalZoom(delta);
        const float zoom = detector_->getDigitalZoom();

        std::ostringstream zoomText;
        zoomText << std::fixed << std::setprecision(2) << zoom;

        std::cout << "[joy] " << buttonName << " -> camera digital zoom "
                  << zoomText.str() << "x\n";

        if (haptic_) {
            SDL_HapticRumblePlay(haptic_, 0.35f, 70);
        }
    };

    auto printSpeeds = [&]() {
        std::cout << "[speed] CH1(FB)=" << drive.getFbSpeed()
                  << "  CH2(LR)=" << drive.getLrSpeed() << "\n";
    };

    while (!quit_all && !back_menu) {
        SDL_Event e{};
        while (SDL_PollEvent(&e)) {
            gui_.handleSDLEvent(e);

            if (!gui_.isOpen()) {
                std::cout << "[joy] Unified GUI closed -> back to menu.\n";
                stopToClientDebugSession("gui_closed", nowMs());
                stopRos2Mapping("gui_closed", nowMs());
                stopDevFarmVideo("gui_closed", nowMs());
                stopDevFarmLidarMap("gui_closed", nowMs());
                miniLidar_.stop();
                if (lidarOwner_ == LidarOwner::InternalMiniLidar) {
                    lidarOwner_ = LidarOwner::None;
                }
                stopTrackingSafely();
                drive.setEmergencyStop(false, nowMs());
                updateRobotState(drive, nowMs());
                back_menu = true;
                break;
            }

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                double mapClickX = 0.0;
                double mapClickY = 0.0;
                if (gui_.ros2MapScreenToDisplayMeters(e.button.x, e.button.y, mapClickX, mapClickY)) {
                    setRos2MapManualStartPose(mapClickX, mapClickY, nowMs());
                }
            }

            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                switch (e.key.keysym.sym) {
                    case SDLK_t:
                        navManager_.nudgeGoal(+kGoalEditStep, 0.0);
                        robotState_.nav = navManager_.getState();
                        std::cout << "[goal] GX -> " << robotState_.nav.goalXMeters
                                  << " , GY -> " << robotState_.nav.goalYMeters << "\n";
                        break;

                    case SDLK_g:
                        navManager_.nudgeGoal(-kGoalEditStep, 0.0);
                        robotState_.nav = navManager_.getState();
                        std::cout << "[goal] GX -> " << robotState_.nav.goalXMeters
                                  << " , GY -> " << robotState_.nav.goalYMeters << "\n";
                        break;

                    case SDLK_y:
                        navManager_.nudgeGoal(0.0, +kGoalEditStep);
                        robotState_.nav = navManager_.getState();
                        std::cout << "[goal] GX -> " << robotState_.nav.goalXMeters
                                  << " , GY -> " << robotState_.nav.goalYMeters << "\n";
                        break;

                    case SDLK_h:
                        navManager_.nudgeGoal(0.0, -kGoalEditStep);
                        robotState_.nav = navManager_.getState();
                        std::cout << "[goal] GX -> " << robotState_.nav.goalXMeters
                                  << " , GY -> " << robotState_.nav.goalYMeters << "\n";
                        break;

                    case SDLK_r:
                        navManager_.resetGoalToDefault();
                        robotState_.nav = navManager_.getState();
                        std::cout << "[goal] reset -> GX " << robotState_.nav.goalXMeters
                                  << " , GY " << robotState_.nav.goalYMeters << "\n";
                        break;

                    case SDLK_z:
                        toggleDevFarmVideo(nowMs());
                        break;

                    case SDLK_x:
                        toggleDevFarmLidarMap(nowMs());
                        break;

                    case SDLK_c:
                        loadDevFarmLidarMap(nowMs());
                        break;

                    case SDLK_q:
                        toggleMiniLidarFromKeyboard();
                        break;

                    case SDLK_a:
                        resetLocalPoseFromKeyboard();
                        break;

                    case SDLK_w:
                        toggleM5ImuFromKeyboard();
                        break;

                    case SDLK_s:
                        toggleM5EnvFromKeyboard();
                        break;

                    case SDLK_e:
                        toggleLocalAutoNavFromKeyboard();
                        break;

                    case SDLK_1:
                        adjustRos2MapManualStartYaw(-5.0, nowMs());
                        break;

                    case SDLK_2:
                        adjustRos2MapManualStartYaw(+5.0, nowMs());
                        break;

                    case SDLK_u:
                        if (!drive.isEmergencyStopActive()) {
                            drive.adjustFbSpeed(+SPEED_STEP);
                            autoStopHoldActive_ = false;
                            navGuardSafeStopLatched_ = false;
                            printSpeeds();
                        }
                        break;

                    case SDLK_j:
                        if (!drive.isEmergencyStopActive()) {
                            drive.adjustFbSpeed(-SPEED_STEP);
                            autoStopHoldActive_ = false;
                            navGuardSafeStopLatched_ = false;
                            printSpeeds();
                        }
                        break;

                    case SDLK_i:
                        if (!drive.isEmergencyStopActive()) {
                            drive.adjustLrSpeed(+SPEED_STEP);
                            autoStopHoldActive_ = false;
                            navGuardSafeStopLatched_ = false;
                            printSpeeds();
                        }
                        break;

                    case SDLK_k:
                        if (!drive.isEmergencyStopActive()) {
                            drive.adjustLrSpeed(-SPEED_STEP);
                            autoStopHoldActive_ = false;
                            navGuardSafeStopLatched_ = false;
                            printSpeeds();
                        }
                        break;

                    default:
                        break;
                }
            }

            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    std::cout << "[joy] START -> back to menu.\n";
                    gui_.close();
                    stopToClientDebugSession("start_button", nowMs());
                    stopRos2Mapping("start_button", nowMs());
                    stopDevFarmVideo("start_button", nowMs());
                    stopDevFarmLidarMap("start_button", nowMs());
                    miniLidar_.stop();
                    if (lidarOwner_ == LidarOwner::InternalMiniLidar) {
                        lidarOwner_ = LidarOwner::None;
                    }
                    stopTrackingSafely();
                    drive.setEmergencyStop(false, nowMs());
                    updateRobotState(drive, nowMs());
                    back_menu = true;
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    std::cout << "[joy] BACK -> quit program.\n";
                    gui_.close();
                    stopToClientDebugSession("back_button", nowMs());
                    stopRos2Mapping("back_button", nowMs());
                    stopDevFarmVideo("back_button", nowMs());
                    stopDevFarmLidarMap("back_button", nowMs());
                    miniLidar_.stop();
                    if (lidarOwner_ == LidarOwner::InternalMiniLidar) {
                        lidarOwner_ = LidarOwner::None;
                    }
                    stopTrackingSafely();
                    drive.setEmergencyStop(false, nowMs());
                    updateRobotState(drive, nowMs());
                    quit_all = true;
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
                    const uint64_t tNow = nowMs();
                    const bool newState = !drive.isEmergencyStopActive();
                    drive.setEmergencyStop(newState, tNow);

                    std::cout << "[joy] E-STOP "
                              << (newState ? "ON" : "OFF") << "\n";

                    if (newState) {
                        navManager_.onEmergencyStop(tNow);
                        robotState_.nav = navManager_.getState();
                        drive.stopAll();
                        prevR2Pressed_ = false;
                        autoStopHoldActive_ = false;
                        navGuardSafeStopLatched_ = false;
                    } else {
                        navManager_.clearEmergencyStopBlock();
                        robotState_.nav = navManager_.getState();
                    }

                    if (haptic_) {
                        SDL_HapticRumblePlay(haptic_, newState ? 1.0f : 0.4f, newState ? 250 : 120);
                    }
                }
                // Physical PlayStation Cross/X button.
                // SDL maps Cross to SDL_CONTROLLER_BUTTON_A.
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    if (drive.isEmergencyStopActive()) {
                        continue;
                    }

                    if (devFarmVideoRecorder_.isRecording()) {
                        std::cout << "[joy] Cross/X ignored: devFarm video recording is active. Press Z to stop recording first.\n";
                        continue;
                    }

                    const uint64_t tNow = nowMs();
                    if (!canToggleTracking(tNow)) {
                        continue;
                    }
                    lastTrackingToggleMs_ = tNow;

                    if (detector_) {
                        const bool enable = !detector_->isRunning();

                        if (enable) {
                            startTrackingSafely();
                        } else {
                            stopTrackingSafely();
                            std::cout << "[joy] tomato detector OFF\n";
                        }

                        if (haptic_) {
                            SDL_HapticRumblePlay(haptic_, 0.7f, 120);
                        }
                    }
                }
                // Physical PlayStation L1 button. Starts a compact TO_CLIENT_JSON session.
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                    startToClientDebugSession(nowMs());
                }
                // Physical PlayStation R1 button. Starts/stops ROS2 slam_toolbox mapping.
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    if (!drive.isEmergencyStopActive()) {
                        toggleRos2Mapping(nowMs());
                    }
                }
                // Physical PlayStation Square button.
                // SDL maps Square to SDL_CONTROLLER_BUTTON_X.
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                    const float step = detector_ ? detector_->config().digitalZoomStep : 0.15f;
                    adjustCameraZoomFromJoystick(+step, "Square");
                }
                // Physical PlayStation Circle button.
                // SDL maps Circle to SDL_CONTROLLER_BUTTON_B.
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    const float step = detector_ ? detector_->config().digitalZoomStep : 0.15f;
                    adjustCameraZoomFromJoystick(-step, "Circle");
                }
                // L1/L2 are reserved for TO_CLIENT_JSON sessions.
                // R1 controls ROS2 slam_toolbox mapping.
                // R2 loads the latest saved ROS2 map into the LIDAR GUI.
                // D-PAD is reserved for manual camera servo control.
            }
        }

        const uint64_t tickNow = nowMs();

        // L2 is an analog trigger in SDL. Use it as STOP for TO_CLIENT_JSON.
        // R2 is an analog trigger too. Use its rising edge to load latest ROS2 map into the LIDAR GUI.
        const Sint16 l2AxisNow = SDL_GameControllerGetAxis(gc_, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        const bool l2PressedNow = l2AxisNow > L2_THRESHOLD;
        if (l2PressedNow && !l2Latched) {
            stopToClientDebugSession("manual_L2", tickNow);
        }
        l2Latched = l2PressedNow;

        const Sint16 r2AxisNow = SDL_GameControllerGetAxis(gc_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        const bool r2PressedNow = r2AxisNow > R2_THRESHOLD;
        if (r2PressedNow && !prevR2Pressed_) {
            loadLatestRos2Map(tickNow);
        }
        prevR2Pressed_ = r2PressedNow;

        if (tracker_) {
            const bool dpadUp = SDL_GameControllerGetButton(gc_, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
            const bool dpadDown = SDL_GameControllerGetButton(gc_, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
            const bool dpadLeft = SDL_GameControllerGetButton(gc_, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
            const bool dpadRight = SDL_GameControllerGetButton(gc_, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;

            float zoomForServo = 1.0f;
            if (detector_) {
                zoomForServo = detector_->getDigitalZoom();
            }
            const bool fineServo = zoomForServo >= 5.50f;
            const int servoStepDeg = fineServo ? SERVO_MANUAL_FINE_STEP_DEG : SERVO_MANUAL_STEP_DEG;
            const uint64_t servoRepeatMs = fineServo ? SERVO_MANUAL_FINE_REPEAT_MS : SERVO_MANUAL_REPEAT_MS;

            if ((dpadUp || dpadDown || dpadLeft || dpadRight) &&
                (tickNow - lastManualServoMoveMs) >= servoRepeatMs) {

                int panDelta = 0;
                int tiltDelta = 0;

                if (dpadLeft)  panDelta -= servoStepDeg;
                if (dpadRight) panDelta += servoStepDeg;
                if (dpadUp)    tiltDelta -= servoStepDeg;
                if (dpadDown)  tiltDelta += servoStepDeg;

                if (panDelta != 0 || tiltDelta != 0) {
                    tracker_->manualNudgeCamera(panDelta, tiltDelta);
                    if (detector_) {
                        detector_->notifyCameraMotion();
                    }
                    lastManualServoMoveMs = tickNow;
                }
            }
        }

        int fbCmd = 0;
        int lrCmd = 0;

        const Sint16 leftY  = SDL_GameControllerGetAxis(gc_, SDL_CONTROLLER_AXIS_LEFTY);
        const Sint16 rightX = SDL_GameControllerGetAxis(gc_, SDL_CONTROLLER_AXIS_RIGHTX);

        const bool manualReleaseRequested =
            (std::abs(leftY) > HOLD_RELEASE_AXIS_THRESHOLD) ||
            (std::abs(rightX) > HOLD_RELEASE_AXIS_THRESHOLD);

        if (autoStopHoldActive_ && manualReleaseRequested) {
            autoStopHoldActive_ = false;
            prevGoalReached_ = false;
            navGuardSafeStopLatched_ = false;
        }

        if (!drive.isEmergencyStopActive()) {
            const int leftYCmd = axisToSignedCmd(leftY, FB_DEADZONE);
            if (leftYCmd < 0) fbCmd = +1;
            else if (leftYCmd > 0) fbCmd = -1;

            lrCmd = axisToSignedCmd(rightX, LR_DEADZONE);
        }

        updateRobotState(drive, tickNow);

        if (!drive.isEmergencyStopActive() &&
            robotState_.navGuard.safeStopRequested &&
            !navGuardSafeStopLatched_) {

            navGuardSafeStopLatched_ = true;
            autoStopHoldActive_ = true;
            prevGoalReached_ = false;

            navManager_.cancelLocalAuto();
            drive.stopAll();

            const uint64_t safeNow = nowMs();
            updateRobotState(drive, safeNow);
            updateOdomState(safeNow);
            robotState_.navGuard.safeStopTriggered = true;

            std::cout << "[joy] NAV guard safe-stop triggered\n";

            if (haptic_) {
                SDL_HapticRumblePlay(haptic_, 0.9f, 180);
            }

            logNavOdomIfNeeded();
            logNavLidarIfNeeded();
                    logToClientIfNeeded();
            gui_.setRobotState(robotState_);
            gui_.render();
            SDL_Delay(1);
            continue;
        }

        if (robotState_.nav.localAutoGoalReached && !prevGoalReached_) {
            autoStopHoldActive_ = true;
        }
        prevGoalReached_ = robotState_.nav.localAutoGoalReached;

        if (autoStopHoldActive_) {
            drive.stopAll();
            fbCmd = 0;
            lrCmd = 0;

            const uint64_t holdNow = nowMs();
            updateRobotState(drive, holdNow);
            updateOdomState(holdNow);
            robotState_.navGuard.safeStopTriggered = navGuardSafeStopLatched_;
            logNavOdomIfNeeded();
            logNavLidarIfNeeded();
                    logToClientIfNeeded();
            gui_.setRobotState(robotState_);
            gui_.render();
            SDL_Delay(1);
            continue;
        }

        if (!drive.isEmergencyStopActive() &&
            robotState_.nav.localAutoEnabled &&
            robotState_.nav.localAutoActive &&
            !robotState_.nav.localAutoGoalReached) {

            if (std::fabs(robotState_.nav.localAutoForwardCmd) > 1.0) {
                fbCmd = (robotState_.nav.localAutoForwardCmd > 0.0) ? +1 : -1;
            } else {
                fbCmd = 0;
            }

            if (std::fabs(robotState_.nav.localAutoSteeringCmd) > 5.0) {
                lrCmd = (robotState_.nav.localAutoSteeringCmd > 0.0) ? +1 : -1;
            } else {
                lrCmd = 0;
            }
        }

        drive.step(fbCmd, lrCmd, tickNow);

        // Servo TargetTracker is disabled in tomato mode.
        // ObjectDetector performs frame-level IoU tracking without moving the camera.
        if (!drive.isEmergencyStopActive()) {
            if (detector_ && !detector_->isRunning() && robotState_.tracking.trackingEnabled) {
                std::cout << "[joy] detector stopped unexpectedly -> frame tracking OFF\n";
                stopTrackingSafely();
            }
        }

        const uint64_t postNow = nowMs();
        updateRobotState(drive, postNow);
        updateOdomState(postNow);
        robotState_.navGuard.safeStopTriggered = navGuardSafeStopLatched_;
        logNavOdomIfNeeded();
        logNavLidarIfNeeded();
                    logToClientIfNeeded();

        gui_.setRobotState(robotState_);
        gui_.render();

        SDL_Delay(1);
    }

    gui_.close();
    stopToClientDebugSession("joystick_loop_end", nowMs());
    stopRos2Mapping("joystick_loop_end", nowMs());
    stopDevFarmVideo("joystick_loop_end", nowMs());
    stopDevFarmLidarMap("joystick_loop_end", nowMs());
    miniLidar_.stop();
    if (lidarOwner_ == LidarOwner::InternalMiniLidar) {
        lidarOwner_ = LidarOwner::None;
    }
    stopTrackingSafely();

    drive.setEmergencyStop(false, nowMs());
    drive.stopAll();

    const uint64_t endNow = nowMs();
    updateRobotState(drive, endNow);
    updateOdomState(endNow);
    robotState_.navGuard.safeStopTriggered = navGuardSafeStopLatched_;
    logNavOdomIfNeeded();
    logNavLidarIfNeeded();
                    logToClientIfNeeded();

    return quit_all;
}

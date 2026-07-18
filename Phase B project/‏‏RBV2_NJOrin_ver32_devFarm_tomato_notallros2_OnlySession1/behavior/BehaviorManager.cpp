#include "behavior/BehaviorManager.h"




#include <string>
#include <vector>




namespace {
std::string joinParts(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}
}




RobotMode BehaviorManager::determineMode(const RobotState& state) const {
    if (state.emergencyStop) {
        return RobotMode::EmergencyStop;
    }




    const bool moving =
        (state.drive.currentForwardSpeed != 0.0f) ||
        (state.drive.currentSteeringSpeed != 0.0f);




    const bool lidarOn = state.health.lidarAvailable;
    const bool trackingArmed = state.tracking.trackingEnabled;
    const bool trackingLock =
        trackingArmed &&
        state.tracking.isFresh &&
        state.tracking.targetSelected;




    const bool searching = trackingArmed && !trackingLock;




    if (trackingLock && lidarOn) {
        return RobotMode::TrackLockWithLidar;
    }




    if (trackingLock) {
        return RobotMode::TrackLock;
    }




    if (searching && lidarOn) {
        return RobotMode::SearchWithLidar;
    }




    if (searching) {
        return RobotMode::Search;
    }




    if (moving && lidarOn) {
        return RobotMode::ManualDriveWithLidar;
    }




    if (moving) {
        return RobotMode::ManualDrive;
    }




    if (lidarOn) {
        return RobotMode::LidarOnly;
    }




    return RobotMode::Idle;
}




BehaviorDecision BehaviorManager::evaluate(const RobotState& state) const {
    BehaviorDecision decision{};
    decision.mode = determineMode(state);
    decision.timestampMs = state.timestampMs;




    if (state.emergencyStop) {
        decision.emergencyStop = true;
        decision.warningActive = true;
        decision.statusText = "E-STOP";
        return decision;
    }




    const bool moving =
        (state.drive.currentForwardSpeed != 0.0f) ||
        (state.drive.currentSteeringSpeed != 0.0f);




    const bool lidarOn = state.health.lidarAvailable;
    const bool trackingArmed = state.tracking.trackingEnabled;
    const bool trackingLock =
        trackingArmed &&
        state.tracking.isFresh &&
        state.tracking.targetSelected;




    const bool searching = trackingArmed && !trackingLock;




    const bool detectionsStale = state.health.detectorStale;
    const bool trackerStale    = state.health.trackerStale;
    const bool lidarStale      = state.health.lidarStale;
    const bool driveStale      = state.health.driveStale;
    const bool m5stickStale    = state.health.m5stickStale;




    decision.obstacleFront = state.lidarSummary.frontObstacleClose;
    decision.obstacleLeft  = state.lidarSummary.leftObstacleClose;
    decision.obstacleRight = state.lidarSummary.rightObstacleClose;
    decision.obstacleClose =
        decision.obstacleFront || decision.obstacleLeft || decision.obstacleRight;




    decision.targetLost =
        trackingArmed &&
        !trackingLock &&
        state.health.detectionsFresh;




    decision.m5stickDisconnected = !state.m5stick.connected;




    decision.imuOffline =
        state.m5stick.imuEnabled &&
        (!state.m5stick.imu.valid || state.m5stick.imu.isStale || !state.m5stick.imu.hwOk);




    decision.envOffline =
        state.m5stick.envEnabled &&
        (!state.m5stick.env.valid || state.m5stick.env.isStale || !state.m5stick.env.hwOk);




    decision.m5stickWarning =
        decision.m5stickDisconnected ||
        decision.imuOffline ||
        decision.envOffline ||
        m5stickStale;




    decision.slamActive = state.nav.slamActive;
    decision.slamLost = state.nav.slamActive && !state.nav.localized;
    decision.mapReady = state.nav.mapReady;
    decision.poseValid = state.nav.valid;
    decision.navWarning = state.nav.isStale || decision.slamLost;




    decision.warningActive =
        decision.obstacleClose ||
        detectionsStale ||
        trackerStale ||
        lidarStale ||
        driveStale ||
        decision.targetLost ||
        decision.m5stickWarning ||
        decision.navWarning;




    std::vector<std::string> primaryParts;




    if (moving) {
        primaryParts.push_back("MANUAL");
    }




    if (trackingLock) {
        primaryParts.push_back("TRACK LOCK");
    } else if (searching) {
        primaryParts.push_back("SEARCH");
    }




    if (lidarOn) {
        primaryParts.push_back("LIDAR");
    }




    if (primaryParts.empty()) {
        primaryParts.push_back("IDLE");
    }




    std::vector<std::string> detailParts;




    if (decision.obstacleClose) {
        std::vector<std::string> dirs;
        if (decision.obstacleFront) dirs.push_back("FRONT");
        if (decision.obstacleLeft)  dirs.push_back("LEFT");
        if (decision.obstacleRight) dirs.push_back("RIGHT");




        detailParts.push_back("OBSTACLE: " + joinParts(dirs, " / "));
    }




    std::vector<std::string> staleParts;
    if (detectionsStale) staleParts.push_back("DETECTIONS");
    if (trackerStale)    staleParts.push_back("TRACKER");
    if (lidarStale)      staleParts.push_back("LIDAR");
    if (driveStale)      staleParts.push_back("DRIVE");
    if (m5stickStale)    staleParts.push_back("M5");
    if (state.nav.isStale)  staleParts.push_back("NAV");




    if (!staleParts.empty()) {
        detailParts.push_back("STALE: " + joinParts(staleParts, " / "));
    }




    if (decision.targetLost) {
        detailParts.push_back("TARGET LOST");
    }




    if (decision.m5stickDisconnected) {
        detailParts.push_back("M5 DISCONNECTED");
    }




    if (decision.imuOffline) {
        detailParts.push_back("IMU OFFLINE");
    }




    if (decision.envOffline) {
        detailParts.push_back("ENV OFFLINE");
    }




    if (!decision.poseValid) {
        detailParts.push_back("POSE N/A");
    }




    if (!decision.mapReady) {
        detailParts.push_back("MAP NO");
    }




    if (decision.slamLost) {
        detailParts.push_back("SLAM LOST");
    }




    decision.statusText = joinParts(primaryParts, "  ");
    if (!detailParts.empty()) {
        decision.statusText += "  " + joinParts(detailParts, "  ");
    }




    return decision;
}




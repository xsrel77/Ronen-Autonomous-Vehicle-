#include "Debugging/toClient/ToClientJsonLogger.h"


#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>


ToClientJsonLogger::~ToClientJsonLogger()
{
    stop();
}


bool ToClientJsonLogger::start(const std::string& jsonlPath,
                               const std::string& latestJsonPath)
{
    stop();


    file_.open(jsonlPath, std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[debug][toClient] failed to open JSONL file: " << jsonlPath << "\n";
        enabled_ = false;
        return false;
    }


    latestJsonPath_ = latestJsonPath;
    enabled_ = true;


    std::cout << "[debug][toClient] JSONL logging enabled: " << jsonlPath << "\n";
    if (!latestJsonPath_.empty()) {
        std::cout << "[debug][toClient] latest JSON snapshot: " << latestJsonPath_ << "\n";
    }


    return true;
}


void ToClientJsonLogger::stop()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }


    latestJsonPath_.clear();
    enabled_ = false;
}


bool ToClientJsonLogger::isEnabled() const
{
    return enabled_ && file_.is_open();
}


const char* ToClientJsonLogger::boolText(bool v)
{
    return v ? "true" : "false";
}


std::string ToClientJsonLogger::jsonText(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');


    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(ch >> 4) & 0x0F]);
                    out.push_back(hex[ch & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }


    out.push_back('"');
    return out;
}


const char* ToClientJsonLogger::robotModeName(RobotMode mode)
{
    switch (mode) {
        case RobotMode::Idle: return "Idle";
        case RobotMode::ManualDrive: return "ManualDrive";
        case RobotMode::LidarOnly: return "LidarOnly";
        case RobotMode::ManualDriveWithLidar: return "ManualDriveWithLidar";
        case RobotMode::Search: return "Search";
        case RobotMode::SearchWithLidar: return "SearchWithLidar";
        case RobotMode::TrackLock: return "TrackLock";
        case RobotMode::TrackLockWithLidar: return "TrackLockWithLidar";
        case RobotMode::EmergencyStop: return "EmergencyStop";
        default: return "Unknown";
    }
}


const char* ToClientJsonLogger::driveCommandTypeName(DriveCommandType type)
{
    switch (type) {
        case DriveCommandType::None: return "None";
        case DriveCommandType::Stop: return "Stop";
        case DriveCommandType::Forward: return "Forward";
        case DriveCommandType::Backward: return "Backward";
        case DriveCommandType::Left: return "Left";
        case DriveCommandType::Right: return "Right";
        case DriveCommandType::ForwardLeft: return "ForwardLeft";
        case DriveCommandType::ForwardRight: return "ForwardRight";
        case DriveCommandType::BackwardLeft: return "BackwardLeft";
        case DriveCommandType::BackwardRight: return "BackwardRight";
        default: return "Unknown";
    }
}


const char* ToClientJsonLogger::navPoseSourceName(NavPoseSource source)
{
    switch (source) {
        case NavPoseSource::None: return "None";
        case NavPoseSource::YawOnly: return "YawOnly";
        case NavPoseSource::YawCmd: return "YawCmd";
        case NavPoseSource::YawOdom: return "YawOdom";
        case NavPoseSource::Slam: return "Slam";
        default: return "Unknown";
    }
}


const char* ToClientJsonLogger::navMotionStateName(NavMotionState state)
{
    switch (state) {
        case NavMotionState::Stop: return "Stop";
        case NavMotionState::Forward: return "Forward";
        case NavMotionState::Reverse: return "Reverse";
        default: return "Unknown";
    }
}


const char* ToClientJsonLogger::navTurnStateName(NavTurnState state)
{
    switch (state) {
        case NavTurnState::Straight: return "Straight";
        case NavTurnState::Left: return "Left";
        case NavTurnState::Right: return "Right";
        default: return "Unknown";
    }
}


const char* ToClientJsonLogger::odomPoseSourceName(OdomPoseSource source)
{
    switch (source) {
        case OdomPoseSource::None: return "None";
        case OdomPoseSource::CmdYawRate: return "CmdYawRate";
        case OdomPoseSource::ImuYawRate: return "ImuYawRate";
        case OdomPoseSource::ImuYawRateCmdLinear: return "ImuYawRateCmdLinear";
        case OdomPoseSource::Encoder: return "Encoder";
        case OdomPoseSource::Fused: return "Fused";
        default: return "Unknown";
    }
}


const Detection* ToClientJsonLogger::findBestDetection(const DetectionSnapshot& snapshot)
{
    const Detection* best = nullptr;


    for (const auto& d : snapshot.detections) {
        if (!d.valid) {
            continue;
        }


        if (!best || d.confidence > best->confidence) {
            best = &d;
        }
    }


    return best;
}


void ToClientJsonLogger::writeJsonObject(std::ostream& os,
                                         const RobotState& s,
                                         bool pretty)
{
    const Detection* best = findBestDetection(s.detections);
    const int detectionCount = static_cast<int>(s.detections.detections.size());
    const int lidarPointCount = static_cast<int>(s.lidar.points.size());


    const char* nl = pretty ? "\n" : "";
    const char* i1 = pretty ? "  " : "";
    const char* i2 = pretty ? "    " : "";
    const char* i3 = pretty ? "      " : "";
    const char* i4 = pretty ? "        " : "";
    const char* sp = pretty ? " " : "";


    os << std::fixed << std::setprecision(6);


    os << "{" << nl;


    os << i1 << "\"schema_version\":" << sp << 1 << "," << nl;
    os << i1 << "\"timestamp_ms\":" << sp << s.timestampMs << "," << nl;


    os << i1 << "\"robot\":" << sp << "{" << nl;
    os << i2 << "\"mode\":" << sp << jsonText(robotModeName(s.behavior.mode)) << "," << nl;
    os << i2 << "\"mode_id\":" << sp << static_cast<int>(s.behavior.mode) << "," << nl;
    os << i2 << "\"status_text\":" << sp << jsonText(s.behavior.statusText) << "," << nl;
    os << i2 << "\"warning_active\":" << sp << boolText(s.behavior.warningActive) << "," << nl;
    os << i2 << "\"emergency_stop\":" << sp << boolText(s.emergencyStop) << "," << nl;
    os << i2 << "\"unified_gui_open\":" << sp << boolText(s.unifiedGuiOpen) << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"drive\":" << sp << "{" << nl;
    os << i2 << "\"forward_speed\":" << sp << s.drive.currentForwardSpeed << "," << nl;
    os << i2 << "\"steering_speed\":" << sp << s.drive.currentSteeringSpeed << "," << nl;
    os << i2 << "\"fresh\":" << sp << boolText(s.drive.isFresh) << "," << nl;
    os << i2 << "\"emergency_stop\":" << sp << boolText(s.drive.emergencyStop) << "," << nl;
    os << i2 << "\"last_command\":" << sp << "{" << nl;
    os << i3 << "\"type\":" << sp << jsonText(driveCommandTypeName(s.drive.lastCommand.type)) << "," << nl;
    os << i3 << "\"type_id\":" << sp << static_cast<int>(s.drive.lastCommand.type) << "," << nl;
    os << i3 << "\"source\":" << sp << jsonText(s.drive.lastCommand.source) << "," << nl;
    os << i3 << "\"forward_speed\":" << sp << s.drive.lastCommand.forwardSpeed << "," << nl;
    os << i3 << "\"steering_speed\":" << sp << s.drive.lastCommand.steeringSpeed << "," << nl;
    os << i3 << "\"timestamp_ms\":" << sp << s.drive.lastCommand.timestampMs << nl;
    os << i2 << "}" << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"health\":" << sp << "{" << nl;
    os << i2 << "\"valid\":" << sp << boolText(s.health.valid) << "," << nl;
    os << i2 << "\"camera_available\":" << sp << boolText(s.health.cameraAvailable) << "," << nl;
    os << i2 << "\"lidar_available\":" << sp << boolText(s.health.lidarAvailable) << "," << nl;
    os << i2 << "\"joystick_connected\":" << sp << boolText(s.health.joystickConnected) << "," << nl;
    os << i2 << "\"m5stick_available\":" << sp << boolText(s.health.m5stickAvailable) << "," << nl;
    os << i2 << "\"detector_running\":" << sp << boolText(s.health.detectorRunning) << "," << nl;
    os << i2 << "\"tracker_running\":" << sp << boolText(s.health.trackerRunning) << "," << nl;
    os << i2 << "\"gui_running\":" << sp << boolText(s.health.guiRunning) << "," << nl;
    os << i2 << "\"fresh\":" << sp << "{" << nl;
    os << i3 << "\"drive\":" << sp << boolText(s.health.driveFresh) << "," << nl;
    os << i3 << "\"detections\":" << sp << boolText(s.health.detectionsFresh) << "," << nl;
    os << i3 << "\"tracking\":" << sp << boolText(s.health.trackingFresh) << "," << nl;
    os << i3 << "\"lidar\":" << sp << boolText(s.health.lidarFresh) << "," << nl;
    os << i3 << "\"m5stick\":" << sp << boolText(s.health.m5stickFresh) << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"stale\":" << sp << "{" << nl;
    os << i3 << "\"drive\":" << sp << boolText(s.health.driveStale) << "," << nl;
    os << i3 << "\"detector\":" << sp << boolText(s.health.detectorStale) << "," << nl;
    os << i3 << "\"tracker\":" << sp << boolText(s.health.trackerStale) << "," << nl;
    os << i3 << "\"lidar\":" << sp << boolText(s.health.lidarStale) << "," << nl;
    os << i3 << "\"m5stick\":" << sp << boolText(s.health.m5stickStale) << nl;
    os << i2 << "}" << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"perception\":" << sp << "{" << nl;
    os << i2 << "\"frame\":" << sp << "{" << nl;
    os << i3 << "\"width\":" << sp << s.detections.frame.width << "," << nl;
    os << i3 << "\"height\":" << sp << s.detections.frame.height << "," << nl;
    os << i3 << "\"channels\":" << sp << s.detections.frame.channels << "," << nl;
    os << i3 << "\"timestamp_ms\":" << sp << s.detections.frame.timestampMs << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"detections_valid\":" << sp << boolText(s.detections.valid) << "," << nl;
    os << i2 << "\"detections_fresh\":" << sp << boolText(s.detections.isFresh) << "," << nl;
    os << i2 << "\"detection_count\":" << sp << detectionCount << "," << nl;
    os << i2 << "\"best_detection\":" << sp;
    if (best) {
        os << "{" << nl;
        os << i3 << "\"label\":" << sp << jsonText(best->label) << "," << nl;
        os << i3 << "\"class_id\":" << sp << best->classId << "," << nl;
        os << i3 << "\"confidence\":" << sp << best->confidence << "," << nl;
        os << i3 << "\"bbox\":" << sp << "{" << nl;
        os << i4 << "\"x\":" << sp << best->x << "," << nl;
        os << i4 << "\"y\":" << sp << best->y << "," << nl;
        os << i4 << "\"w\":" << sp << best->w << "," << nl;
        os << i4 << "\"h\":" << sp << best->h << nl;
        os << i3 << "}" << nl;
        os << i2 << "}";
    } else {
        os << "null";
    }
    os << "," << nl;
    os << i2 << "\"detections\":" << sp << "[";
    for (std::size_t idx = 0; idx < s.detections.detections.size(); ++idx) {
        const auto& d = s.detections.detections[idx];
        if (idx > 0) {
            os << ",";
        }
        os << nl << i3 << "{";
        os << "\"label\":" << jsonText(d.label) << ",";
        os << "\"class_id\":" << d.classId << ",";
        os << "\"confidence\":" << d.confidence << ",";
        os << "\"valid\":" << boolText(d.valid) << ",";
        os << "\"bbox\":{";
        os << "\"x\":" << d.x << ",";
        os << "\"y\":" << d.y << ",";
        os << "\"w\":" << d.w << ",";
        os << "\"h\":" << d.h;
        os << "}}";
    }
    if (!s.detections.detections.empty()) {
        os << nl << i2;
    }
    os << "]," << nl;
    os << i2 << "\"tracking\":" << sp << "{" << nl;
    os << i3 << "\"enabled\":" << sp << boolText(s.tracking.trackingEnabled) << "," << nl;
    os << i3 << "\"target_selected\":" << sp << boolText(s.tracking.targetSelected) << "," << nl;
    os << i3 << "\"confidence\":" << sp << s.tracking.confidence << "," << nl;
    os << i3 << "\"target_offset_x\":" << sp << s.tracking.targetOffsetX << "," << nl;
    os << i3 << "\"target_offset_y\":" << sp << s.tracking.targetOffsetY << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.tracking.isFresh) << nl;
    os << i2 << "}" << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"lidar\":" << sp << "{" << nl;
    os << i2 << "\"snapshot\":" << sp << "{" << nl;
    os << i3 << "\"valid\":" << sp << boolText(s.lidar.valid) << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.lidar.isFresh) << "," << nl;
    os << i3 << "\"point_count\":" << sp << lidarPointCount << "," << nl;
    os << i3 << "\"timestamp_ms\":" << sp << s.lidar.timestampMs << "," << nl;
    os << i3 << "\"coordinate_frame\":" << sp << jsonText("robot_base_lidar_2d") << "," << nl;
    os << i3 << "\"units\":" << sp << "{" << "\"x\":\"m\",\"y\":\"m\",\"distance\":\"m\",\"angle\":\"deg\"" << "}," << nl;
    os << i3 << "\"point_cloud\":" << sp << "[";
    for (std::size_t idx = 0; idx < s.lidar.points.size(); ++idx) {
        const auto& p = s.lidar.points[idx];
        if (idx > 0) {
            os << ",";
        }


        const double angleDeg = std::atan2(p.y, p.x) * 180.0 / 3.14159265358979323846;


        os << nl << i4 << "{";
        os << "\"x_m\":" << p.x << ",";
        os << "\"y_m\":" << p.y << ",";
        os << "\"distance_m\":" << p.dist << ",";
        os << "\"angle_deg\":" << angleDeg;
        os << "}";
    }
    if (!s.lidar.points.empty()) {
        os << nl << i3;
    }
    os << "]" << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"summary\":" << sp << "{" << nl;
    os << i3 << "\"valid\":" << sp << boolText(s.lidarSummary.valid) << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.lidarSummary.isFresh) << "," << nl;
    os << i3 << "\"front_m\":" << sp << s.lidarSummary.frontMinMeters << "," << nl;
    os << i3 << "\"left_m\":" << sp << s.lidarSummary.leftMinMeters << "," << nl;
    os << i3 << "\"right_m\":" << sp << s.lidarSummary.rightMinMeters << "," << nl;
    os << i3 << "\"rear_m\":" << sp << s.lidarSummary.rearMinMeters << "," << nl;
    os << i3 << "\"front_close\":" << sp << boolText(s.lidarSummary.frontObstacleClose) << "," << nl;
    os << i3 << "\"left_close\":" << sp << boolText(s.lidarSummary.leftObstacleClose) << "," << nl;
    os << i3 << "\"right_close\":" << sp << boolText(s.lidarSummary.rightObstacleClose) << "," << nl;
    os << i3 << "\"any_close\":" << sp << boolText(s.lidarSummary.obstacleClose) << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"pose\":" << sp << "{" << nl;
    os << i3 << "\"valid\":" << sp << boolText(s.lidarPose.valid) << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.lidarPose.isFresh) << "," << nl;
    os << i3 << "\"stale\":" << sp << boolText(s.lidarPose.isStale) << "," << nl;
    os << i3 << "\"scan_valid\":" << sp << boolText(s.lidarPose.scanValid) << "," << nl;
    os << i3 << "\"enough_points\":" << sp << boolText(s.lidarPose.enoughPoints) << "," << nl;
    os << i3 << "\"confidence\":" << sp << s.lidarPose.confidence << "," << nl;
    os << i3 << "\"sectors_m\":" << sp << "{" << nl;
    os << i4 << "\"front\":" << sp << s.lidarPose.frontDistanceM << "," << nl;
    os << i4 << "\"front_left\":" << sp << s.lidarPose.frontLeftDistanceM << "," << nl;
    os << i4 << "\"left\":" << sp << s.lidarPose.leftDistanceM << "," << nl;
    os << i4 << "\"rear_left\":" << sp << s.lidarPose.rearLeftDistanceM << "," << nl;
    os << i4 << "\"rear\":" << sp << s.lidarPose.rearDistanceM << "," << nl;
    os << i4 << "\"rear_right\":" << sp << s.lidarPose.rearRightDistanceM << "," << nl;
    os << i4 << "\"right\":" << sp << s.lidarPose.rightDistanceM << "," << nl;
    os << i4 << "\"front_right\":" << sp << s.lidarPose.frontRightDistanceM << nl;
    os << i3 << "}," << nl;
    os << i3 << "\"nearest_m\":" << sp << s.lidarPose.nearestDistanceM << "," << nl;
    os << i3 << "\"nearest_sector_index\":" << sp << s.lidarPose.nearestSectorIndex << "," << nl;
    os << i3 << "\"lateral_balance_m\":" << sp << s.lidarPose.lateralBalanceM << "," << nl;
    os << i3 << "\"front_balance_m\":" << sp << s.lidarPose.frontBalanceM << "," << nl;
    os << i3 << "\"rear_balance_m\":" << sp << s.lidarPose.rearBalanceM << "," << nl;
    os << i3 << "\"center_error_m\":" << sp << s.lidarPose.centerErrorM << "," << nl;
    os << i3 << "\"heading_hint_deg\":" << sp << s.lidarPose.headingHintDeg << "," << nl;
    os << i3 << "\"front_clearance_m\":" << sp << s.lidarPose.frontClearanceM << "," << nl;
    os << i3 << "\"rear_clearance_m\":" << sp << s.lidarPose.rearClearanceM << "," << nl;
    os << i3 << "\"obstacle_ahead\":" << sp << boolText(s.lidarPose.obstacleAhead) << "," << nl;
    os << i3 << "\"obstacle_rear\":" << sp << boolText(s.lidarPose.obstacleRear) << "," << nl;
    os << i3 << "\"blocked\":" << sp << "{" << nl;
    os << i4 << "\"front_left\":" << sp << boolText(s.lidarPose.frontLeftBlocked) << "," << nl;
    os << i4 << "\"front_right\":" << sp << boolText(s.lidarPose.frontRightBlocked) << "," << nl;
    os << i4 << "\"left\":" << sp << boolText(s.lidarPose.leftBlocked) << "," << nl;
    os << i4 << "\"right\":" << sp << boolText(s.lidarPose.rightBlocked) << "," << nl;
    os << i4 << "\"rear_left\":" << sp << boolText(s.lidarPose.rearLeftBlocked) << "," << nl;
    os << i4 << "\"rear_right\":" << sp << boolText(s.lidarPose.rearRightBlocked) << nl;
    os << i3 << "}" << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"hints\":" << sp << "{" << nl;
    os << i3 << "\"valid\":" << sp << boolText(s.lidarHints.valid) << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.lidarHints.isFresh) << "," << nl;
    os << i3 << "\"stale\":" << sp << boolText(s.lidarHints.isStale) << "," << nl;
    os << i3 << "\"forward_clearance_ok\":" << sp << boolText(s.lidarHints.forwardClearanceOk) << "," << nl;
    os << i3 << "\"reverse_clearance_ok\":" << sp << boolText(s.lidarHints.reverseClearanceOk) << "," << nl;
    os << i3 << "\"corridor_centered\":" << sp << boolText(s.lidarHints.corridorCentered) << "," << nl;
    os << i3 << "\"steer_correction_suggested\":" << sp << boolText(s.lidarHints.steerCorrectionSuggested) << "," << nl;
    os << i3 << "\"suggested_steer_sign\":" << sp << s.lidarHints.suggestedSteerSign << "," << nl;
    os << i3 << "\"suggested_steer_strength\":" << sp << s.lidarHints.suggestedSteerStrength << "," << nl;
    os << i3 << "\"reverse_correction_suggested\":" << sp << boolText(s.lidarHints.reverseCorrectionSuggested) << "," << nl;
    os << i3 << "\"preferred_reverse_side\":" << sp << s.lidarHints.preferredReverseSide << "," << nl;
    os << i3 << "\"reverse_preference_strength\":" << sp << s.lidarHints.reversePreferenceStrength << "," << nl;
    os << i3 << "\"center_error_m\":" << sp << s.lidarHints.centerErrorM << "," << nl;
    os << i3 << "\"heading_hint_deg\":" << sp << s.lidarHints.headingHintDeg << "," << nl;
    os << i3 << "\"front_clearance_m\":" << sp << s.lidarHints.frontClearanceM << "," << nl;
    os << i3 << "\"rear_clearance_m\":" << sp << s.lidarHints.rearClearanceM << nl;
    os << i2 << "}" << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"navigation\":" << sp << "{" << nl;
    os << i2 << "\"valid\":" << sp << boolText(s.nav.valid) << "," << nl;
    os << i2 << "\"fresh\":" << sp << boolText(s.nav.isFresh) << "," << nl;
    os << i2 << "\"stale\":" << sp << boolText(s.nav.isStale) << "," << nl;
    os << i2 << "\"yaw_valid\":" << sp << boolText(s.nav.yawValid) << "," << nl;
    os << i2 << "\"reference_initialized\":" << sp << boolText(s.nav.referenceInitialized) << "," << nl;
    os << i2 << "\"estimated_pose\":" << sp << boolText(s.nav.estimatedPose) << "," << nl;
    os << i2 << "\"pose_source\":" << sp << jsonText(navPoseSourceName(s.nav.poseSource)) << "," << nl;
    os << i2 << "\"pose_source_id\":" << sp << static_cast<int>(s.nav.poseSource) << "," << nl;
    os << i2 << "\"motion_state\":" << sp << jsonText(navMotionStateName(s.nav.motionState)) << "," << nl;
    os << i2 << "\"turn_state\":" << sp << jsonText(navTurnStateName(s.nav.turnState)) << "," << nl;
    os << i2 << "\"pose\":" << sp << "{" << nl;
    os << i3 << "\"x_m\":" << sp << s.nav.xMeters << "," << nl;
    os << i3 << "\"y_m\":" << sp << s.nav.yMeters << "," << nl;
    os << i3 << "\"yaw_deg\":" << sp << s.nav.yawDeg << "," << nl;
    os << i3 << "\"yaw_zero_deg\":" << sp << s.nav.yawZeroDeg << "," << nl;
    os << i3 << "\"yaw_relative_deg\":" << sp << s.nav.yawRelativeDeg << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"velocity\":" << sp << "{" << nl;
    os << i3 << "\"linear_mps\":" << sp << s.nav.linearVelocityMps << "," << nl;
    os << i3 << "\"angular_degps\":" << sp << s.nav.angularVelocityDegPs << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"command\":" << sp << "{" << nl;
    os << i3 << "\"forward\":" << sp << s.nav.forwardCommand << "," << nl;
    os << i3 << "\"steering\":" << sp << s.nav.steeringCommand << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"goal\":" << sp << "{" << nl;
    os << i3 << "\"active\":" << sp << boolText(s.nav.goalActive) << "," << nl;
    os << i3 << "\"x_m\":" << sp << s.nav.goalXMeters << "," << nl;
    os << i3 << "\"y_m\":" << sp << s.nav.goalYMeters << "," << nl;
    os << i3 << "\"distance_m\":" << sp << s.nav.goalDistanceMeters << "," << nl;
    os << i3 << "\"bearing_deg\":" << sp << s.nav.goalBearingDeg << "," << nl;
    os << i3 << "\"heading_error_deg\":" << sp << s.nav.headingErrorDeg << "," << nl;
    os << i3 << "\"reached\":" << sp << boolText(s.nav.localAutoGoalReached) << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"local_auto\":" << sp << "{" << nl;
    os << i3 << "\"enabled\":" << sp << boolText(s.nav.localAutoEnabled) << "," << nl;
    os << i3 << "\"active\":" << sp << boolText(s.nav.localAutoActive) << "," << nl;
    os << i3 << "\"blocked_by_estop\":" << sp << boolText(s.nav.localAutoBlockedByEStop) << "," << nl;
    os << i3 << "\"forward_cmd\":" << sp << s.nav.localAutoForwardCmd << "," << nl;
    os << i3 << "\"steering_cmd\":" << sp << s.nav.localAutoSteeringCmd << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"localized\":" << sp << boolText(s.nav.localized) << "," << nl;
    os << i2 << "\"map_ready\":" << sp << boolText(s.nav.mapReady) << "," << nl;
    os << i2 << "\"slam_active\":" << sp << boolText(s.nav.slamActive) << "," << nl;
    os << i2 << "\"tracking_lost\":" << sp << boolText(s.nav.trackingLost) << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"odom\":" << sp << "{" << nl;
    os << i2 << "\"valid\":" << sp << boolText(s.odom.valid) << "," << nl;
    os << i2 << "\"fresh\":" << sp << boolText(s.odom.isFresh) << "," << nl;
    os << i2 << "\"stale\":" << sp << boolText(s.odom.isStale) << "," << nl;
    os << i2 << "\"yaw_valid\":" << sp << boolText(s.odom.yawValid) << "," << nl;
    os << i2 << "\"reference_initialized\":" << sp << boolText(s.odom.referenceInitialized) << "," << nl;
    os << i2 << "\"integration_active\":" << sp << boolText(s.odom.integrationActive) << "," << nl;
    os << i2 << "\"estimated_pose\":" << sp << boolText(s.odom.estimatedPose) << "," << nl;
    os << i2 << "\"pose_source\":" << sp << jsonText(odomPoseSourceName(s.odom.poseSource)) << "," << nl;
    os << i2 << "\"pose_source_id\":" << sp << static_cast<int>(s.odom.poseSource) << "," << nl;
    os << i2 << "\"pose\":" << sp << "{" << nl;
    os << i3 << "\"x_m\":" << sp << s.odom.xMeters << "," << nl;
    os << i3 << "\"y_m\":" << sp << s.odom.yMeters << "," << nl;
    os << i3 << "\"yaw_deg\":" << sp << s.odom.yawDeg << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"velocity\":" << sp << "{" << nl;
    os << i3 << "\"linear_mps\":" << sp << s.odom.linearVelocityMps << "," << nl;
    os << i3 << "\"angular_degps\":" << sp << s.odom.angularVelocityDegPs << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"command\":" << sp << "{" << nl;
    os << i3 << "\"forward\":" << sp << s.odom.forwardCommand << "," << nl;
    os << i3 << "\"steering\":" << sp << s.odom.steeringCommand << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"raw_dt_sec\":" << sp << s.odom.rawDtSec << "," << nl;
    os << i2 << "\"dt_sec\":" << sp << s.odom.dtSec << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"m5stick\":" << sp << "{" << nl;
    os << i2 << "\"connected\":" << sp << boolText(s.m5stick.connected) << "," << nl;
    os << i2 << "\"port_open\":" << sp << boolText(s.m5stick.portOpen) << "," << nl;
    os << i2 << "\"status_valid\":" << sp << boolText(s.m5stick.statusValid) << "," << nl;
    os << i2 << "\"status_fresh\":" << sp << boolText(s.m5stick.statusFresh) << "," << nl;
    os << i2 << "\"status_stale\":" << sp << boolText(s.m5stick.statusStale) << "," << nl;
    os << i2 << "\"telemetry_valid\":" << sp << boolText(s.m5stick.telemetryValid) << "," << nl;
    os << i2 << "\"telemetry_fresh\":" << sp << boolText(s.m5stick.telemetryFresh) << "," << nl;
    os << i2 << "\"telemetry_stale\":" << sp << boolText(s.m5stick.telemetryStale) << "," << nl;
    os << i2 << "\"imu\":" << sp << "{" << nl;
    os << i3 << "\"enabled\":" << sp << boolText(s.m5stick.imu.enabled) << "," << nl;
    os << i3 << "\"hw_ok\":" << sp << boolText(s.m5stick.imu.hwOk) << "," << nl;
    os << i3 << "\"read_ok\":" << sp << boolText(s.m5stick.imu.readOk) << "," << nl;
    os << i3 << "\"valid\":" << sp << boolText(s.m5stick.imu.valid) << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.m5stick.imu.isFresh) << "," << nl;
    os << i3 << "\"stale\":" << sp << boolText(s.m5stick.imu.isStale) << "," << nl;
    os << i3 << "\"accel\":" << sp << "{" << "\"x\":" << s.m5stick.imu.ax << ",\"y\":" << s.m5stick.imu.ay << ",\"z\":" << s.m5stick.imu.az << "}," << nl;
    os << i3 << "\"gyro\":" << sp << "{" << "\"x\":" << s.m5stick.imu.gx << ",\"y\":" << s.m5stick.imu.gy << ",\"z\":" << s.m5stick.imu.gz << "}" << nl;
    os << i2 << "}," << nl;
    os << i2 << "\"env\":" << sp << "{" << nl;
    os << i3 << "\"enabled\":" << sp << boolText(s.m5stick.env.enabled) << "," << nl;
    os << i3 << "\"hw_ok\":" << sp << boolText(s.m5stick.env.hwOk) << "," << nl;
    os << i3 << "\"read_ok\":" << sp << boolText(s.m5stick.env.readOk) << "," << nl;
    os << i3 << "\"valid\":" << sp << boolText(s.m5stick.env.valid) << "," << nl;
    os << i3 << "\"fresh\":" << sp << boolText(s.m5stick.env.isFresh) << "," << nl;
    os << i3 << "\"stale\":" << sp << boolText(s.m5stick.env.isStale) << "," << nl;
    os << i3 << "\"temp_c\":" << sp << s.m5stick.env.tempC << "," << nl;
    os << i3 << "\"humidity_pct\":" << sp << s.m5stick.env.humidityPct << "," << nl;
    os << i3 << "\"pressure_hpa\":" << sp << s.m5stick.env.pressureHpa << "," << nl;
    os << i3 << "\"gas_kohm\":" << sp << s.m5stick.env.gasKohm << nl;
    os << i2 << "}" << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"behavior\":" << sp << "{" << nl;
    os << i2 << "\"obstacle_close\":" << sp << boolText(s.behavior.obstacleClose) << "," << nl;
    os << i2 << "\"obstacle_front\":" << sp << boolText(s.behavior.obstacleFront) << "," << nl;
    os << i2 << "\"obstacle_left\":" << sp << boolText(s.behavior.obstacleLeft) << "," << nl;
    os << i2 << "\"obstacle_right\":" << sp << boolText(s.behavior.obstacleRight) << "," << nl;
    os << i2 << "\"target_lost\":" << sp << boolText(s.behavior.targetLost) << "," << nl;
    os << i2 << "\"m5stick_warning\":" << sp << boolText(s.behavior.m5stickWarning) << "," << nl;
    os << i2 << "\"m5stick_disconnected\":" << sp << boolText(s.behavior.m5stickDisconnected) << "," << nl;
    os << i2 << "\"imu_offline\":" << sp << boolText(s.behavior.imuOffline) << "," << nl;
    os << i2 << "\"env_offline\":" << sp << boolText(s.behavior.envOffline) << "," << nl;
    os << i2 << "\"nav_warning\":" << sp << boolText(s.behavior.navWarning) << "," << nl;
    os << i2 << "\"slam_active\":" << sp << boolText(s.behavior.slamActive) << "," << nl;
    os << i2 << "\"slam_lost\":" << sp << boolText(s.behavior.slamLost) << "," << nl;
    os << i2 << "\"map_ready\":" << sp << boolText(s.behavior.mapReady) << "," << nl;
    os << i2 << "\"pose_valid\":" << sp << boolText(s.behavior.poseValid) << nl;
    os << i1 << "}," << nl;


    os << i1 << "\"nav_guard\":" << sp << "{" << nl;
    os << i2 << "\"imu_available_for_nav\":" << sp << boolText(s.navGuard.imuAvailableForNav) << "," << nl;
    os << i2 << "\"nav_pose_valid\":" << sp << boolText(s.navGuard.navPoseValid) << "," << nl;
    os << i2 << "\"nav_pose_fresh\":" << sp << boolText(s.navGuard.navPoseFresh) << "," << nl;
    os << i2 << "\"moving_command\":" << sp << boolText(s.navGuard.movingCommand) << "," << nl;
    os << i2 << "\"odom_motion_evidence\":" << sp << boolText(s.navGuard.odomMotionEvidence) << "," << nl;
    os << i2 << "\"lidar_motion_evidence\":" << sp << boolText(s.navGuard.lidarMotionEvidence) << "," << nl;
    os << i2 << "\"nav_frozen\":" << sp << boolText(s.navGuard.navFrozen) << "," << nl;
    os << i2 << "\"nav_degraded\":" << sp << boolText(s.navGuard.navDegraded) << "," << nl;
    os << i2 << "\"safe_stop_requested\":" << sp << boolText(s.navGuard.safeStopRequested) << "," << nl;
    os << i2 << "\"safe_stop_triggered\":" << sp << boolText(s.navGuard.safeStopTriggered) << "," << nl;
    os << i2 << "\"frozen_accum_ms\":" << sp << s.navGuard.frozenAccumMs << "," << nl;
    os << i2 << "\"degraded_accum_ms\":" << sp << s.navGuard.degradedAccumMs << nl;
    os << i1 << "}" << nl;


    os << "}";
}


void ToClientJsonLogger::log(const RobotState& s)
{
    if (!isEnabled()) {
        return;
    }


    writeJsonObject(file_, s, false);
    file_ << "\n";
    file_.flush();


    if (!latestJsonPath_.empty()) {
        const std::string tmpPath = latestJsonPath_ + ".tmp";
        std::ofstream latest(tmpPath, std::ios::out | std::ios::trunc);
        if (latest.is_open()) {
            writeJsonObject(latest, s, true);
            latest << "\n";
            latest.flush();
            latest.close();
            std::remove(latestJsonPath_.c_str());
            std::rename(tmpPath.c_str(), latestJsonPath_.c_str());
        }
    }
}






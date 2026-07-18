#include "Debugging/test_nav_lidar/NavLidarDebugLogger.h"


#include <iomanip>


NavLidarDebugLogger::~NavLidarDebugLogger()
{
    stop();
}


bool NavLidarDebugLogger::start(const std::string& path)
{
    stop();


    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_.is_open()) {
        enabled_ = false;
        return false;
    }


    enabled_ = true;
    writeHeader();
    return true;
}


void NavLidarDebugLogger::stop()
{
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
    enabled_ = false;
}


bool NavLidarDebugLogger::isEnabled() const
{
    return enabled_ && out_.is_open();
}


const char* NavLidarDebugLogger::bool01(bool v)
{
    return v ? "1" : "0";
}


void NavLidarDebugLogger::writeHeader()
{
    if (!isEnabled()) {
        return;
    }


    out_
        << "timestamp_ms,"
        << "goal_x,goal_y,"
        << "nav_x,nav_y,nav_yaw,"
        << "odom_x,odom_y,odom_yaw,"
        << "lidar_valid,lidar_fresh,lidar_stale,lidar_confidence,"
        << "front_m,front_left_m,left_m,rear_left_m,rear_m,rear_right_m,right_m,front_right_m,"
        << "nearest_m,nearest_sector,"
        << "lateral_balance_m,front_balance_m,rear_balance_m,center_error_m,heading_hint_deg,"
        << "front_clearance_m,rear_clearance_m,"
        << "obstacle_ahead,obstacle_rear,"
        << "front_left_blocked,front_right_blocked,left_blocked,right_blocked,rear_left_blocked,rear_right_blocked,"
        << "hint_valid,hint_fresh,forward_clear_ok,reverse_clear_ok,corridor_centered,"
        << "steer_suggested,steer_sign,steer_strength,"
        << "reverse_suggested,preferred_reverse_side,reverse_pref_strength,"
        << "imu_available_for_nav,nav_pose_valid,nav_pose_fresh,moving_command,"
        << "odom_motion_evidence,lidar_motion_evidence,"
        << "nav_frozen,nav_degraded,safe_stop_requested,safe_stop_triggered,"
        << "frozen_accum_ms,degraded_accum_ms,"
        << "drive_forward_cmd,drive_steering_cmd,drive_emergency_stop"
        << "\n";
}


void NavLidarDebugLogger::log(const NavPoseState& nav,
                              const OdomState& odom,
                              const LidarPoseState& lidar,
                              const LidarCorrectionHintsState& hints,
                              const NavGuardState& navGuard,
                              const DriveState& drive)
{
    if (!isEnabled()) {
        return;
    }


    const std::uint64_t ts =
        (lidar.timestampMs > 0) ? lidar.timestampMs :
        (nav.timestampMs > 0)   ? nav.timestampMs :
        (odom.timestampMs > 0)  ? odom.timestampMs :
                                  drive.lastUpdateTimeMs;


    out_ << std::fixed << std::setprecision(6)
         << ts << ","


         << nav.goalXMeters << ","
         << nav.goalYMeters << ","


         << nav.xMeters << ","
         << nav.yMeters << ","
         << nav.yawRelativeDeg << ","


         << odom.xMeters << ","
         << odom.yMeters << ","
         << odom.yawDeg << ","


         << bool01(lidar.valid) << ","
         << bool01(lidar.isFresh) << ","
         << bool01(lidar.isStale) << ","
         << lidar.confidence << ","


         << lidar.frontDistanceM << ","
         << lidar.frontLeftDistanceM << ","
         << lidar.leftDistanceM << ","
         << lidar.rearLeftDistanceM << ","
         << lidar.rearDistanceM << ","
         << lidar.rearRightDistanceM << ","
         << lidar.rightDistanceM << ","
         << lidar.frontRightDistanceM << ","


         << lidar.nearestDistanceM << ","
         << lidar.nearestSectorIndex << ","


         << lidar.lateralBalanceM << ","
         << lidar.frontBalanceM << ","
         << lidar.rearBalanceM << ","
         << lidar.centerErrorM << ","
         << lidar.headingHintDeg << ","


         << lidar.frontClearanceM << ","
         << lidar.rearClearanceM << ","


         << bool01(lidar.obstacleAhead) << ","
         << bool01(lidar.obstacleRear) << ","


         << bool01(lidar.frontLeftBlocked) << ","
         << bool01(lidar.frontRightBlocked) << ","
         << bool01(lidar.leftBlocked) << ","
         << bool01(lidar.rightBlocked) << ","
         << bool01(lidar.rearLeftBlocked) << ","
         << bool01(lidar.rearRightBlocked) << ","


         << bool01(hints.valid) << ","
         << bool01(hints.isFresh) << ","
         << bool01(hints.forwardClearanceOk) << ","
         << bool01(hints.reverseClearanceOk) << ","
         << bool01(hints.corridorCentered) << ","
         << bool01(hints.steerCorrectionSuggested) << ","
         << hints.suggestedSteerSign << ","
         << hints.suggestedSteerStrength << ","
         << bool01(hints.reverseCorrectionSuggested) << ","
         << hints.preferredReverseSide << ","
         << hints.reversePreferenceStrength << ","


         << bool01(navGuard.imuAvailableForNav) << ","
         << bool01(navGuard.navPoseValid) << ","
         << bool01(navGuard.navPoseFresh) << ","
         << bool01(navGuard.movingCommand) << ","
         << bool01(navGuard.odomMotionEvidence) << ","
         << bool01(navGuard.lidarMotionEvidence) << ","
         << bool01(navGuard.navFrozen) << ","
         << bool01(navGuard.navDegraded) << ","
         << bool01(navGuard.safeStopRequested) << ","
         << bool01(navGuard.safeStopTriggered) << ","
         << navGuard.frozenAccumMs << ","
         << navGuard.degradedAccumMs << ","


         << drive.currentForwardSpeed << ","
         << drive.currentSteeringSpeed << ","
         << bool01(drive.emergencyStop)
         << "\n";
}




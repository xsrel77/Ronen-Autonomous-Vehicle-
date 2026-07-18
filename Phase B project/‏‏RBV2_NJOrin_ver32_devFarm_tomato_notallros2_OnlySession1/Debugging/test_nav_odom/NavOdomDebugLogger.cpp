#include "Debugging/test_nav_odom/NavOdomDebugLogger.h"


#include <iomanip>
#include <iostream>


NavOdomDebugLogger::~NavOdomDebugLogger()
{
    stop();
}


bool NavOdomDebugLogger::start(const std::string& csvPath)
{
    stop();


    file_.open(csvPath, std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[debug][nav_odom] failed to open log file: " << csvPath << "\n";
        enabled_ = false;
        return false;
    }


    file_
        << "t_ms,"
        << "dt_sec_nav_runtime,"
        << "yaw_before,yaw_after,yaw_rate_filt,"
        << "gx_raw,gy_raw,gz_raw,"
        << "ax,ay,az,"
        << "steering_cmd_runtime,"
        << "fb_step_cmd,lr_step_cmd,selected_fb_speed,selected_lr_speed,"
        << "imu_usable,bias_ready,stationary,shock,cross_axis,steering_active,sign_mismatch,straight_suppressed,integrated,zero_snapped,"
        << "nav_x_m,nav_y_m,nav_yaw_deg,nav_yaw_rel_deg,"
        << "nav_lin_mps,nav_ang_degps,"
        << "nav_goal_x_m,nav_goal_y_m,nav_goal_dist_m,nav_goal_bearing_deg,nav_heading_err_deg,"
        << "nav_forward_cmd,nav_steering_cmd,"
        << "nav_auto_forward_cmd,nav_auto_steering_cmd,"
        << "nav_local_auto_enabled,nav_local_auto_active,nav_local_auto_goal_reached,nav_local_auto_blocked,"
        << "nav_reference_initialized,nav_estimated_pose,nav_goal_active,nav_valid,nav_yaw_valid,"
        << "nav_pose_source,nav_motion_state,nav_turn_state,"
        << "odom_x_m,odom_y_m,odom_yaw_deg,"
        << "odom_lin_mps,odom_ang_degps,"
        << "odom_forward_cmd,odom_steering_cmd,"
        << "odom_raw_dt_sec,odom_dt_sec,"
        << "odom_valid,odom_yaw_valid,odom_ref_initialized,odom_integration_active,odom_estimated_pose,odom_fresh,odom_stale,"
        << "odom_pose_source,"
        << "drive_current_fb,drive_current_lr,drive_estop"
        << "\n";


    file_.flush();
    enabled_ = true;


    std::cout << "[debug][nav_odom] logging enabled: " << csvPath << "\n";
    return true;
}


void NavOdomDebugLogger::stop()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    enabled_ = false;
}


bool NavOdomDebugLogger::isEnabled() const
{
    return enabled_;
}


void NavOdomDebugLogger::log(const NavDebugSnapshot& dbg,
                             const NavPoseState& nav,
                             const OdomState& odom,
                             const DriveState& drive)
{
    if (!enabled_ || !file_.is_open()) {
        return;
    }


    file_ << std::fixed << std::setprecision(6)
          << dbg.timestampMs << ","
          << dbg.dtSec << ","
          << dbg.yawBefore << "," << dbg.yawAfter << "," << dbg.yawRateFiltered << ","
          << dbg.gxRaw << "," << dbg.gyRaw << "," << dbg.gzRaw << ","
          << dbg.ax << "," << dbg.ay << "," << dbg.az << ","
          << dbg.steeringCmd << ","
          << dbg.fbStepCmd << "," << dbg.lrStepCmd << ","
          << dbg.selectedFbSpeed << "," << dbg.selectedLrSpeed << ","
          << (dbg.imuUsable ? 1 : 0) << ","
          << (dbg.biasReady ? 1 : 0) << ","
          << (dbg.stationary ? 1 : 0) << ","
          << (dbg.shockDetected ? 1 : 0) << ","
          << (dbg.crossAxisTooHigh ? 1 : 0) << ","
          << (dbg.steeringActive ? 1 : 0) << ","
          << (dbg.signMismatch ? 1 : 0) << ","
          << (dbg.straightMotionSuppressed ? 1 : 0) << ","
          << (dbg.integrated ? 1 : 0) << ","
          << (dbg.zeroSnapped ? 1 : 0) << ","


          << nav.xMeters << "," << nav.yMeters << "," << nav.yawDeg << "," << nav.yawRelativeDeg << ","
          << nav.linearVelocityMps << "," << nav.angularVelocityDegPs << ","
          << nav.goalXMeters << "," << nav.goalYMeters << "," << nav.goalDistanceMeters << ","
          << nav.goalBearingDeg << "," << nav.headingErrorDeg << ","
          << nav.forwardCommand << "," << nav.steeringCommand << ","
          << nav.localAutoForwardCmd << "," << nav.localAutoSteeringCmd << ","
          << (nav.localAutoEnabled ? 1 : 0) << ","
          << (nav.localAutoActive ? 1 : 0) << ","
          << (nav.localAutoGoalReached ? 1 : 0) << ","
          << (nav.localAutoBlockedByEStop ? 1 : 0) << ","
          << (nav.referenceInitialized ? 1 : 0) << ","
          << (nav.estimatedPose ? 1 : 0) << ","
          << (nav.goalActive ? 1 : 0) << ","
          << (nav.valid ? 1 : 0) << ","
          << (nav.yawValid ? 1 : 0) << ","
          << static_cast<int>(nav.poseSource) << ","
          << static_cast<int>(nav.motionState) << ","
          << static_cast<int>(nav.turnState) << ","


          << odom.xMeters << "," << odom.yMeters << "," << odom.yawDeg << ","
          << odom.linearVelocityMps << "," << odom.angularVelocityDegPs << ","
          << odom.forwardCommand << "," << odom.steeringCommand << ","
          << odom.rawDtSec << "," << odom.dtSec << ","
          << (odom.valid ? 1 : 0) << ","
          << (odom.yawValid ? 1 : 0) << ","
          << (odom.referenceInitialized ? 1 : 0) << ","
          << (odom.integrationActive ? 1 : 0) << ","
          << (odom.estimatedPose ? 1 : 0) << ","
          << (odom.isFresh ? 1 : 0) << ","
          << (odom.isStale ? 1 : 0) << ","
          << static_cast<int>(odom.poseSource) << ","


          << drive.currentForwardSpeed << ","
          << drive.currentSteeringSpeed << ","
          << (drive.emergencyStop ? 1 : 0)
          << "\n";


    file_.flush();
}




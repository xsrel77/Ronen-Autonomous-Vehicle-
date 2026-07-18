#include "Debugging/test_yaw/YawDebugLogger.h"


#include <iomanip>
#include <iostream>


YawDebugLogger::~YawDebugLogger()
{
    stop();
}


bool YawDebugLogger::start(const std::string& csvPath)
{
    stop();


    file_.open(csvPath, std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[debug][yaw] failed to open log file: " << csvPath << "\n";
        enabled_ = false;
        return false;
    }


    file_
        << "t_ms,"
        << "ax,ay,az,"
        << "gx_raw,gy_raw,gz_raw,"
        << "yaw_axis_raw,yaw_axis_median,"
        << "accel_norm,jerk,"
        << "gz_median,bias_z,bias_yaw,"
        << "gx_filt,gy_filt,gz_filt,yaw_rate_filt,"
        << "dt_sec,"
        << "yaw_before,yaw_after,"
        << "steering_cmd,"
        << "imu_usable,bias_ready,stationary,shock,cross_axis,steering_active,sign_mismatch,straight_suppressed,integrated,zero_snapped,"
        << "fb_step_cmd,lr_step_cmd,selected_fb_speed,selected_lr_speed,"
        << "nav_x_m,nav_y_m,"
        << "goal_x_m,goal_y_m,goal_dist_m,goal_bearing_deg,heading_err_deg,"
        << "nav_forward_cmd,nav_steering_cmd,"
        << "auto_forward_cmd,auto_steering_cmd,"
        << "local_auto_enabled,local_auto_active,local_auto_goal_reached,local_auto_blocked,"
        << "reference_initialized,estimated_pose,goal_active,"
        << "pose_source,motion_state,turn_state,"
        << "yaw_deg,yaw_valid,ang_vel_degps\n";


    file_.flush();
    enabled_ = true;


    std::cout << "[debug][yaw] logging enabled: " << csvPath << "\n";
    return true;
}


void YawDebugLogger::stop()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    enabled_ = false;
}


bool YawDebugLogger::isEnabled() const
{
    return enabled_;
}


void YawDebugLogger::log(const NavDebugSnapshot& dbg, const NavPoseState& state)
{
    if (!enabled_ || !file_.is_open()) {
        return;
    }


    file_ << std::fixed << std::setprecision(6)
          << dbg.timestampMs << ","
          << dbg.ax << "," << dbg.ay << "," << dbg.az << ","
          << dbg.gxRaw << "," << dbg.gyRaw << "," << dbg.gzRaw << ","
          << dbg.yawAxisRaw << "," << dbg.yawAxisMedian << ","
          << dbg.accelNorm << "," << dbg.jerk << ","
          << dbg.gzMedian << "," << dbg.gyroBiasZ << "," << dbg.gyroBiasYaw << ","
          << dbg.gxFiltered << "," << dbg.gyFiltered << "," << dbg.gzFiltered << "," << dbg.yawRateFiltered << ","
          << dbg.dtSec << ","
          << dbg.yawBefore << "," << dbg.yawAfter << ","
          << dbg.steeringCmd << ","
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
          << dbg.fbStepCmd << ","
          << dbg.lrStepCmd << ","
          << dbg.selectedFbSpeed << ","
          << dbg.selectedLrSpeed << ","
          << state.xMeters << ","
          << state.yMeters << ","
          << state.goalXMeters << ","
          << state.goalYMeters << ","
          << state.goalDistanceMeters << ","
          << state.goalBearingDeg << ","
          << state.headingErrorDeg << ","
          << state.forwardCommand << ","
          << state.steeringCommand << ","
          << state.localAutoForwardCmd << ","
          << state.localAutoSteeringCmd << ","
          << (state.localAutoEnabled ? 1 : 0) << ","
          << (state.localAutoActive ? 1 : 0) << ","
          << (state.localAutoGoalReached ? 1 : 0) << ","
          << (state.localAutoBlockedByEStop ? 1 : 0) << ","
          << (state.referenceInitialized ? 1 : 0) << ","
          << (state.estimatedPose ? 1 : 0) << ","
          << (state.goalActive ? 1 : 0) << ","
          << static_cast<int>(state.poseSource) << ","
          << static_cast<int>(state.motionState) << ","
          << static_cast<int>(state.turnState) << ","
          << state.yawDeg << ","
          << (state.yawValid ? 1 : 0) << ","
          << state.angularVelocityDegPs
          << "\n";


    file_.flush();
}




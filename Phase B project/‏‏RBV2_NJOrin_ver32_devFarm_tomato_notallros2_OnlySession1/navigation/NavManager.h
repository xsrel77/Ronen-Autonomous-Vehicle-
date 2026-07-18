#pragma once


#include <cstdint>


#include "core/SystemState.h"


class NavManager
{
public:
    NavManager() = default;


    void reset();


    void update(const NavPoseState& runtimeNav,
                const DriveState& drive,
                std::uint64_t nowMs);


    void resetLocalReference(const NavPoseState& runtimeNav,
                             std::uint64_t nowMs);


    void setLocalAutoEnabled(bool enabled);
    void toggleLocalAutoEnabled();
    void cancelLocalAuto();
    void onEmergencyStop(std::uint64_t nowMs);
    void clearEmergencyStopBlock();
    bool isLocalAutoEnabled() const;


    void setGoal(double gxMeters, double gyMeters);
    void nudgeGoal(double dxMeters, double dyMeters);
    void resetGoalToDefault();


    const NavPoseState& getState() const;


private:
    static double normalizeAngleDeg(double angleDeg);
    static double commandToLinearVelocityMps(double forwardCmd);
    static double clamp(double v, double lo, double hi);


    void updateLocalAutoController();
    void refreshGoalMetrics();


private:
    NavPoseState state_{};


    bool yawReferenceInitialized_ = false;
    bool poseIntegratorInitialized_ = false;
    std::uint64_t lastPoseUpdateMs_ = 0;


    // midpoint heading integration for better X/Y on curved motion
    bool poseHeadingInitialized_ = false;
    double prevPoseYawRelativeDeg_ = 0.0;


    // once one local-auto run starts, keep the chosen direction stable
    bool localAutoDirectionLatched_ = false;
    bool localAutoUseReverse_ = false;


    // best euclidean distance seen during the current auto run
    double localAutoBestGoalDistance_ = 1.0e9;


    static constexpr double kForwardActiveThreshold = 18.0;
    static constexpr double kSteeringActiveThreshold = 18.0;


    static constexpr double kMinEffectiveDriveCmd = 22.0;
    static constexpr double kMinEstimatedLinearMps = 0.08;
    static constexpr double kMaxEstimatedLinearMps = 0.55;
    static constexpr double kMaxIntegrationDtSec = 0.10;


    // default local goal
    static constexpr double kDefaultGoalX = -0.17;
    static constexpr double kDefaultGoalY = -0.07;


    static constexpr double kGoalReachedDistanceMeters = 0.06;


    static constexpr double kReverseDecisionXMeters = 0.03;


    static constexpr double kGoalBodyXReachedMeters = 0.025;
    static constexpr double kGoalBodyYReachedMeters = 0.025;


    static constexpr double kForwardPassCompletionXMeters = 0.005;
    static constexpr double kForwardReachDistanceMeters = 0.080;
    static constexpr double kForwardOvershootMarginMeters = 0.004;


    static constexpr double kReversePassCompletionXMeters = 0.020;
    static constexpr double kReverseReachYBodyMeters = 0.070;
    static constexpr double kReverseReachDistanceMeters = 0.080;
    static constexpr double kReverseOvershootMarginMeters = 0.004;


    // forward settle logic
    static constexpr double kForwardSettleYBodyMeters = 0.040;
    static constexpr double kForwardSettleYTightMeters = 0.025;
    static constexpr double kForwardSettleXFarMeters = 0.055;
    static constexpr double kForwardSettleHeadingDeadbandDeg = 10.0;
    static constexpr double kForwardSettleSpeedCmd = 35.0;
    static constexpr double kForwardSettleSteeringScale = 0.35;
    static constexpr double kForwardSettleSteeringScaleTight = 0.15;


    // reverse settle logic
    static constexpr double kReverseSettleYBodyMeters = 0.040;
    static constexpr double kReverseSettleYTightMeters = 0.025;
    static constexpr double kReverseSettleXFarMeters = 0.055;
    static constexpr double kReverseSettleHeadingDeadbandDeg = 10.0;
    static constexpr double kReverseSettleSpeedCmd = 28.0;
    static constexpr double kReverseSettleSteeringScale = 0.35;
    static constexpr double kReverseSettleSteeringScaleTight = 0.15;


    static constexpr double kNearGoalSlowdownMeters = 0.12;
    static constexpr double kNearGoalSteeringRelaxMeters = 0.08;
    static constexpr double kNearGoalSteeringDeadbandDeg = 8.0;


    static constexpr double kHighErrorSlowDeg = 55.0;
    static constexpr double kMediumErrorSlowDeg = 25.0;


    static constexpr double kMaxAutoForwardCmd = 140.0;
    static constexpr double kMinAutoForwardCmd = 55.0;
    static constexpr double kCruiseAutoForwardCmd = 105.0;
    static constexpr double kForwardCreepAutoCmd = 40.0;
    static constexpr double kReverseCreepAutoCmd = 30.0;


    static constexpr double kSteeringKp = 3.2;
    static constexpr double kMaxAutoSteeringCmd = 180.0;
};




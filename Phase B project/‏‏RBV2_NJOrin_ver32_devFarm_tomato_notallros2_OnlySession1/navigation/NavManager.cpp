#include "navigation/NavManager.h"


#include <algorithm>
#include <cmath>


namespace
{
constexpr double kPi = 3.14159265358979323846;
}


void NavManager::reset()
{
    state_ = NavPoseState{};
    yawReferenceInitialized_ = false;
    poseIntegratorInitialized_ = false;
    lastPoseUpdateMs_ = 0;


    poseHeadingInitialized_ = false;
    prevPoseYawRelativeDeg_ = 0.0;


    localAutoDirectionLatched_ = false;
    localAutoUseReverse_ = false;
    localAutoBestGoalDistance_ = 1.0e9;


    state_.goalXMeters = kDefaultGoalX;
    state_.goalYMeters = kDefaultGoalY;
    state_.goalActive = true;


    state_.localAutoEnabled = false;
    state_.localAutoActive = false;
    state_.localAutoGoalReached = false;
    state_.localAutoBlockedByEStop = false;
    state_.localAutoForwardCmd = 0.0;
    state_.localAutoSteeringCmd = 0.0;


    refreshGoalMetrics();
}


double NavManager::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}


double NavManager::commandToLinearVelocityMps(double forwardCmd)
{
    const double absCmd = std::fabs(forwardCmd);
    if (absCmd < kMinEffectiveDriveCmd) {
        return 0.0;
    }


    const double norm =
        std::min(1.0,
                 std::max(0.0,
                          (absCmd - kMinEffectiveDriveCmd) / (255.0 - kMinEffectiveDriveCmd)));


    const double speed =
        kMinEstimatedLinearMps +
        norm * (kMaxEstimatedLinearMps - kMinEstimatedLinearMps);


    return (forwardCmd >= 0.0) ? speed : -speed;
}


double NavManager::clamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(v, hi));
}


void NavManager::setLocalAutoEnabled(bool enabled)
{
    state_.localAutoEnabled = enabled;
    state_.localAutoActive = enabled;
    state_.localAutoGoalReached = false;


    if (enabled) {
        state_.localAutoBlockedByEStop = false;
        localAutoBestGoalDistance_ = 1.0e9;
    }


    state_.localAutoForwardCmd = 0.0;
    state_.localAutoSteeringCmd = 0.0;


    localAutoDirectionLatched_ = false;
    localAutoUseReverse_ = false;
}


void NavManager::toggleLocalAutoEnabled()
{
    setLocalAutoEnabled(!state_.localAutoEnabled);
}


void NavManager::cancelLocalAuto()
{
    state_.localAutoEnabled = false;
    state_.localAutoActive = false;
    state_.localAutoGoalReached = false;
    state_.localAutoForwardCmd = 0.0;
    state_.localAutoSteeringCmd = 0.0;


    localAutoDirectionLatched_ = false;
    localAutoUseReverse_ = false;
    localAutoBestGoalDistance_ = 1.0e9;
}


void NavManager::onEmergencyStop(std::uint64_t nowMs)
{
    cancelLocalAuto();
    state_.localAutoBlockedByEStop = true;
    state_.timestampMs = nowMs;
}


void NavManager::clearEmergencyStopBlock()
{
    state_.localAutoBlockedByEStop = false;
}


bool NavManager::isLocalAutoEnabled() const
{
    return state_.localAutoEnabled;
}


void NavManager::setGoal(double gxMeters, double gyMeters)
{
    state_.goalXMeters = gxMeters;
    state_.goalYMeters = gyMeters;
    state_.goalActive = true;
    localAutoDirectionLatched_ = false;
    localAutoUseReverse_ = false;
    localAutoBestGoalDistance_ = 1.0e9;
    refreshGoalMetrics();
}


void NavManager::nudgeGoal(double dxMeters, double dyMeters)
{
    setGoal(state_.goalXMeters + dxMeters,
            state_.goalYMeters + dyMeters);
}


void NavManager::resetGoalToDefault()
{
    setGoal(kDefaultGoalX, kDefaultGoalY);
}


void NavManager::refreshGoalMetrics()
{
    const double dx = state_.goalXMeters - state_.xMeters;
    const double dy = state_.goalYMeters - state_.yMeters;


    state_.goalDistanceMeters = std::sqrt(dx * dx + dy * dy);


    if (state_.referenceInitialized) {
        state_.goalBearingDeg = std::atan2(dy, dx) * 180.0 / kPi;
        state_.headingErrorDeg =
            normalizeAngleDeg(state_.goalBearingDeg - state_.yawRelativeDeg);
    } else {
        state_.goalBearingDeg = 0.0;
        state_.headingErrorDeg = 0.0;
    }
}


void NavManager::updateLocalAutoController()
{
    state_.localAutoForwardCmd = 0.0;
    state_.localAutoSteeringCmd = 0.0;


    if (!state_.localAutoEnabled) {
        state_.localAutoActive = false;
        return;
    }


    if (!state_.referenceInitialized || !state_.goalActive) {
        state_.localAutoActive = false;
        return;
    }


    state_.localAutoActive = true;


    const double dx = state_.goalXMeters - state_.xMeters;
    const double dy = state_.goalYMeters - state_.yMeters;


    const double yawRad = state_.yawRelativeDeg * kPi / 180.0;
    const double cosYaw = std::cos(yawRad);
    const double sinYaw = std::sin(yawRad);


    const double xBody =  cosYaw * dx + sinYaw * dy;
    const double yBody = -sinYaw * dx + cosYaw * dy;


    if (!localAutoDirectionLatched_) {
        localAutoUseReverse_ = (xBody < -kReverseDecisionXMeters);
        localAutoDirectionLatched_ = true;
    }


    const bool useReverse = localAutoUseReverse_;


    if (state_.goalDistanceMeters < localAutoBestGoalDistance_) {
        localAutoBestGoalDistance_ = state_.goalDistanceMeters;
    }


    const bool reachedByCircle =
        (state_.goalDistanceMeters <= kGoalReachedDistanceMeters);


    const bool reachedByBodyBox =
        (std::fabs(xBody) <= kGoalBodyXReachedMeters) &&
        (std::fabs(yBody) <= kGoalBodyYReachedMeters);


    const bool reachedByForwardPass =
        (!useReverse) &&
        (xBody <= kForwardPassCompletionXMeters) &&
        (state_.goalDistanceMeters <= kForwardReachDistanceMeters);


    const bool reachedByForwardOvershoot =
        (!useReverse) &&
        (xBody <= kForwardPassCompletionXMeters) &&
        (localAutoBestGoalDistance_ <= kForwardReachDistanceMeters) &&
        (state_.goalDistanceMeters > (localAutoBestGoalDistance_ + kForwardOvershootMarginMeters));


    const bool reachedByReversePass =
        useReverse &&
        (xBody >= -kReversePassCompletionXMeters) &&
        (std::fabs(yBody) <= kReverseReachYBodyMeters) &&
        (state_.goalDistanceMeters <= kReverseReachDistanceMeters);


    const bool reachedByReverseOvershoot =
        useReverse &&
        (xBody >= -kReversePassCompletionXMeters) &&
        (localAutoBestGoalDistance_ <= kReverseReachDistanceMeters) &&
        (state_.goalDistanceMeters > (localAutoBestGoalDistance_ + kReverseOvershootMarginMeters));


    if (reachedByCircle ||
        reachedByBodyBox ||
        reachedByForwardPass ||
        reachedByForwardOvershoot ||
        reachedByReversePass ||
        reachedByReverseOvershoot) {


        state_.localAutoGoalReached = true;
        state_.localAutoEnabled = false;
        state_.localAutoActive = false;
        state_.localAutoForwardCmd = 0.0;
        state_.localAutoSteeringCmd = 0.0;
        state_.forwardCommand = 0.0;
        state_.steeringCommand = 0.0;
        state_.motionState = NavMotionState::Stop;
        state_.turnState = NavTurnState::Straight;


        localAutoDirectionLatched_ = false;
        localAutoUseReverse_ = false;
        localAutoBestGoalDistance_ = 1.0e9;
        return;
    }


    state_.localAutoGoalReached = false;


    double pathErrorDeg = 0.0;
    double steeringCmd = 0.0;


    if (!useReverse) {
        pathErrorDeg =
            std::atan2(yBody, std::max(xBody, 1.0e-6)) * 180.0 / kPi;


        steeringCmd =
            clamp(kSteeringKp * pathErrorDeg,
                  -kMaxAutoSteeringCmd,
                  kMaxAutoSteeringCmd);
    } else {
        pathErrorDeg =
            std::atan2(yBody, std::max(-xBody, 1.0e-6)) * 180.0 / kPi;


        steeringCmd =
            clamp(kSteeringKp * pathErrorDeg,
                  -kMaxAutoSteeringCmd,
                  kMaxAutoSteeringCmd);
    }


    const double absErr = std::fabs(pathErrorDeg);


    double speedMag = kCruiseAutoForwardCmd;


    if (absErr >= kHighErrorSlowDeg) {
        speedMag = kMinAutoForwardCmd;
    } else if (absErr >= kMediumErrorSlowDeg) {
        speedMag = 0.5 * (kCruiseAutoForwardCmd + kMinAutoForwardCmd);
    }


    if (state_.goalDistanceMeters < kNearGoalSlowdownMeters) {
        speedMag = std::min(speedMag,
                            useReverse ? kReverseCreepAutoCmd : kForwardCreepAutoCmd);
    }


    if (state_.goalDistanceMeters < kNearGoalSteeringRelaxMeters) {
        const double lateralTol =
            useReverse ? kReverseReachYBodyMeters : kGoalBodyYReachedMeters;


        if (absErr < kNearGoalSteeringDeadbandDeg ||
            std::fabs(yBody) <= lateralTol) {
            steeringCmd = 0.0;
        } else {
            steeringCmd *= 0.5;
        }
    }


    // Forward settle:
    // once Y is already close enough, relax steering earlier and finish mainly on X.
    if (!useReverse) {
        const double absYBody = std::fabs(yBody);
        const double forwardXRemain = xBody;


        if (absYBody <= kForwardSettleYBodyMeters &&
            forwardXRemain > kForwardSettleXFarMeters) {
            steeringCmd *= kForwardSettleSteeringScale;
            speedMag = std::min(speedMag, kForwardSettleSpeedCmd);
        }


        if (absYBody <= kForwardSettleYTightMeters &&
            forwardXRemain > kForwardPassCompletionXMeters &&
            absErr <= kForwardSettleHeadingDeadbandDeg) {
            steeringCmd *= kForwardSettleSteeringScaleTight;
            speedMag = std::min(speedMag, kForwardSettleSpeedCmd);
        }


        if (absYBody <= kForwardSettleYTightMeters &&
            absErr <= kNearGoalSteeringDeadbandDeg) {
            steeringCmd = 0.0;
            speedMag = std::min(speedMag, kForwardSettleSpeedCmd);
        }
    }


    // Reverse settle:
    // once Y is already close enough, relax steering earlier and finish mainly on X.
    if (useReverse) {
        const double absYBody = std::fabs(yBody);
        const double reverseXRemain = -xBody;


        if (absYBody <= kReverseSettleYBodyMeters &&
            reverseXRemain > kReverseSettleXFarMeters) {
            steeringCmd *= kReverseSettleSteeringScale;
            speedMag = std::min(speedMag, kReverseSettleSpeedCmd);
        }


        if (absYBody <= kReverseSettleYTightMeters &&
            reverseXRemain > kReversePassCompletionXMeters &&
            absErr <= kReverseSettleHeadingDeadbandDeg) {
            steeringCmd *= kReverseSettleSteeringScaleTight;
            speedMag = std::min(speedMag, kReverseSettleSpeedCmd);
        }


        if (absYBody <= kReverseSettleYTightMeters &&
            absErr <= kNearGoalSteeringDeadbandDeg) {
            steeringCmd = 0.0;
            speedMag = std::min(speedMag, kReverseSettleSpeedCmd);
        }
    }


    speedMag = clamp(speedMag, 0.0, kMaxAutoForwardCmd);
    steeringCmd = clamp(steeringCmd, -kMaxAutoSteeringCmd, kMaxAutoSteeringCmd);


    state_.localAutoForwardCmd = useReverse ? -speedMag : speedMag;
    state_.localAutoSteeringCmd = steeringCmd;
}


void NavManager::update(const NavPoseState& runtimeNav,
                        const DriveState& drive,
                        std::uint64_t nowMs)
{
    NavPoseState next = state_;


    next.timestampMs = nowMs;
    next.yawDeg = runtimeNav.yawDeg;
    next.angularVelocityDegPs = runtimeNav.angularVelocityDegPs;
    next.yawValid = runtimeNav.yawValid;
    next.slamActive = runtimeNav.slamActive;
    next.mapReady = runtimeNav.mapReady;
    next.localized = runtimeNav.localized;
    next.trackingLost = runtimeNav.trackingLost;
    next.isFresh = runtimeNav.isFresh;
    next.isStale = runtimeNav.isStale;


    next.forwardCommand = drive.currentForwardSpeed;
    next.steeringCommand = drive.currentSteeringSpeed;


    if (next.yawValid) {
        if (!yawReferenceInitialized_) {
            next.yawZeroDeg = next.yawDeg;
            yawReferenceInitialized_ = true;
        }
        next.referenceInitialized = true;
        next.yawRelativeDeg = normalizeAngleDeg(next.yawDeg - next.yawZeroDeg);
    } else {
        next.referenceInitialized = yawReferenceInitialized_;
    }


    if (!poseIntegratorInitialized_) {
        poseIntegratorInitialized_ = true;
        lastPoseUpdateMs_ = nowMs;
    }


    double dtSec = 0.0;
    if (nowMs > lastPoseUpdateMs_) {
        dtSec = static_cast<double>(nowMs - lastPoseUpdateMs_) / 1000.0;
        dtSec = std::min(dtSec, kMaxIntegrationDtSec);
    }
    lastPoseUpdateMs_ = nowMs;


    const double dxBefore = next.goalXMeters - next.xMeters;
    const double dyBefore = next.goalYMeters - next.yMeters;


    next.goalDistanceMeters = std::sqrt(dxBefore * dxBefore + dyBefore * dyBefore);


    if (next.referenceInitialized) {
        next.goalBearingDeg = std::atan2(dyBefore, dxBefore) * 180.0 / kPi;
        next.headingErrorDeg =
            normalizeAngleDeg(next.goalBearingDeg - next.yawRelativeDeg);
    } else {
        next.goalBearingDeg = 0.0;
        next.headingErrorDeg = 0.0;
    }


    state_ = next;
    updateLocalAutoController();


    NavPoseState integrated = state_;


    double effectiveForwardCmd = drive.currentForwardSpeed;
    double effectiveSteeringCmd = drive.currentSteeringSpeed;


    if (integrated.localAutoEnabled && integrated.localAutoActive) {
        const double currentFbAbs = std::fabs(drive.currentForwardSpeed);
        const double currentLrAbs = std::fabs(drive.currentSteeringSpeed);


        if (std::fabs(integrated.localAutoForwardCmd) > 1.0) {
            if (currentFbAbs > 1.0) {
                effectiveForwardCmd =
                    (integrated.localAutoForwardCmd > 0.0) ? currentFbAbs : -currentFbAbs;
            } else {
                effectiveForwardCmd = integrated.localAutoForwardCmd;
            }
        } else {
            effectiveForwardCmd = 0.0;
        }


        if (std::fabs(integrated.localAutoSteeringCmd) > 5.0) {
            if (currentLrAbs > 1.0) {
                effectiveSteeringCmd =
                    (integrated.localAutoSteeringCmd > 0.0) ? currentLrAbs : -currentLrAbs;
            } else {
                effectiveSteeringCmd = integrated.localAutoSteeringCmd;
            }
        } else {
            effectiveSteeringCmd = 0.0;
        }
    }


    integrated.forwardCommand = effectiveForwardCmd;
    integrated.steeringCommand = effectiveSteeringCmd;


    if (std::fabs(effectiveForwardCmd) >= kForwardActiveThreshold) {
        integrated.motionState =
            (effectiveForwardCmd > 0.0)
                ? NavMotionState::Forward
                : NavMotionState::Reverse;
    } else {
        integrated.motionState = NavMotionState::Stop;
    }


    if (std::fabs(effectiveSteeringCmd) >= kSteeringActiveThreshold) {
        integrated.turnState =
            (effectiveSteeringCmd > 0.0)
                ? NavTurnState::Right
                : NavTurnState::Left;
    } else {
        integrated.turnState = NavTurnState::Straight;
    }


    const double estimatedLinear = commandToLinearVelocityMps(effectiveForwardCmd);
    integrated.linearVelocityMps = estimatedLinear;


    if (integrated.referenceInitialized && integrated.yawValid) {
        if (!poseHeadingInitialized_) {
            prevPoseYawRelativeDeg_ = integrated.yawRelativeDeg;
            poseHeadingInitialized_ = true;
        }


        if (std::fabs(estimatedLinear) > 1.0e-4 && dtSec > 1.0e-6) {
            const double yawNow = integrated.yawRelativeDeg;
            const double yawPrev = prevPoseYawRelativeDeg_;
            const double deltaYaw = normalizeAngleDeg(yawNow - yawPrev);
            const double yawMid = normalizeAngleDeg(yawPrev + 0.5 * deltaYaw);


            const double headingRad = yawMid * kPi / 180.0;
            integrated.xMeters += estimatedLinear * std::cos(headingRad) * dtSec;
            integrated.yMeters += estimatedLinear * std::sin(headingRad) * dtSec;
        }


        prevPoseYawRelativeDeg_ = integrated.yawRelativeDeg;


        integrated.estimatedPose = true;
        integrated.valid = true;
        integrated.poseSource = NavPoseSource::YawCmd;
    } else if (integrated.yawValid) {
        prevPoseYawRelativeDeg_ = integrated.yawRelativeDeg;
        poseHeadingInitialized_ = true;


        integrated.estimatedPose = false;
        integrated.valid = false;
        integrated.poseSource = NavPoseSource::YawOnly;
    } else {
        poseHeadingInitialized_ = false;
        prevPoseYawRelativeDeg_ = 0.0;


        integrated.estimatedPose = false;
        integrated.valid = false;
        integrated.poseSource = NavPoseSource::None;
        integrated.linearVelocityMps = 0.0;
    }


    state_ = integrated;
    refreshGoalMetrics();
    updateLocalAutoController();
}


void NavManager::resetLocalReference(const NavPoseState& runtimeNav,
                                     std::uint64_t nowMs)
{
    const double keepGoalX = state_.goalActive ? state_.goalXMeters : kDefaultGoalX;
    const double keepGoalY = state_.goalActive ? state_.goalYMeters : kDefaultGoalY;


    state_.localAutoEnabled = false;
    state_.localAutoActive = false;
    state_.localAutoGoalReached = false;
    state_.localAutoBlockedByEStop = false;
    state_.localAutoForwardCmd = 0.0;
    state_.localAutoSteeringCmd = 0.0;


    poseHeadingInitialized_ = false;
    prevPoseYawRelativeDeg_ = 0.0;


    localAutoDirectionLatched_ = false;
    localAutoUseReverse_ = false;
    localAutoBestGoalDistance_ = 1.0e9;


    state_.xMeters = 0.0;
    state_.yMeters = 0.0;
    state_.linearVelocityMps = 0.0;
    state_.forwardCommand = 0.0;
    state_.steeringCommand = 0.0;
    state_.timestampMs = nowMs;


    state_.goalXMeters = keepGoalX;
    state_.goalYMeters = keepGoalY;
    state_.goalActive = true;


    poseIntegratorInitialized_ = true;
    lastPoseUpdateMs_ = nowMs;


    state_.yawDeg = runtimeNav.yawDeg;
    state_.angularVelocityDegPs = runtimeNav.angularVelocityDegPs;
    state_.yawValid = runtimeNav.yawValid;
    state_.isFresh = runtimeNav.isFresh;
    state_.isStale = runtimeNav.isStale;


    if (runtimeNav.yawValid) {
        state_.yawZeroDeg = runtimeNav.yawDeg;
        state_.yawRelativeDeg = 0.0;
        state_.referenceInitialized = true;
        yawReferenceInitialized_ = true;
        state_.poseSource = NavPoseSource::YawOnly;
    } else {
        state_.yawZeroDeg = 0.0;
        state_.yawRelativeDeg = 0.0;
        state_.referenceInitialized = false;
        yawReferenceInitialized_ = false;
        state_.poseSource = NavPoseSource::None;
    }


    state_.estimatedPose = false;
    state_.valid = false;


    refreshGoalMetrics();
}


const NavPoseState& NavManager::getState() const
{
    return state_;
}




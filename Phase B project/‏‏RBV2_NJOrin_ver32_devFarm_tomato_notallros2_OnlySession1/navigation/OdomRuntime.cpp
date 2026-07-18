#include "navigation/OdomRuntime.h"


#include <algorithm>
#include <cmath>


namespace
{
constexpr double kPi = 3.14159265358979323846;


static double degToRad(double deg)
{
    return deg * (kPi / 180.0);
}


static double radToDeg(double rad)
{
    return rad * (180.0 / kPi);
}
}


OdomRuntime::OdomRuntime(const Config& cfg)
    : cfg_(cfg)
{
}


void OdomRuntime::reset()
{
    state_ = OdomState{};
    state_.estimatedPose = true;
    state_.valid = false;
    state_.yawValid = false;
    state_.referenceInitialized = false;
    state_.integrationActive = false;
    state_.isFresh = false;
    state_.isStale = true;
    state_.poseSource = OdomPoseSource::None;
    state_.timestampMs = 0;
    state_.rawDtSec = 0.0;
    state_.dtSec = 0.0;
    lastUpdateMs_ = 0;
    filteredSteeringCmd_ = 0.0;
}


double OdomRuntime::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}


double OdomRuntime::clampUnit(double v)
{
    return std::max(-1.0, std::min(1.0, v));
}


double OdomRuntime::applyDeadband(double value, double deadbandAbs)
{
    return (std::fabs(value) >= deadbandAbs) ? value : 0.0;
}


void OdomRuntime::update(const M5ImuState& imu,
                         const DriveState& drive,
                         std::uint64_t nowMs)
{
    const double rawForwardCmd  = static_cast<double>(drive.currentForwardSpeed);
    const double rawSteeringCmd = static_cast<double>(drive.currentSteeringSpeed);
    const bool imuFreshValid = imu.valid && imu.isFresh;


    state_.forwardCommand = rawForwardCmd;
    state_.steeringCommand = rawSteeringCmd;
    state_.timestampMs = nowMs;
    state_.estimatedPose = true;


    if (!state_.referenceInitialized) {
        state_.referenceInitialized = true;
        state_.valid = true;
        state_.yawValid = imuFreshValid;
        state_.integrationActive = false;
        state_.isFresh = true;
        state_.isStale = false;
        state_.poseSource = OdomPoseSource::None;
        state_.linearVelocityMps = 0.0;
        state_.angularVelocityDegPs = 0.0;
        state_.rawDtSec = 0.0;
        state_.dtSec = 0.0;
        lastUpdateMs_ = nowMs;
        return;
    }


    double rawDtSec = 0.0;
    if (lastUpdateMs_ > 0 && nowMs > lastUpdateMs_) {
        rawDtSec = static_cast<double>(nowMs - lastUpdateMs_) / 1000.0;
    }


    if (rawDtSec < 0.0) {
        rawDtSec = 0.0;
    }


    state_.rawDtSec = rawDtSec;


    double effectiveDtSec = 0.0;


    // Fixed integration step when timing is acceptable.
    // If the loop had a large delay, skip integration for that tick.
    if (rawDtSec > 0.0 && rawDtSec <= cfg_.maxAcceptedDtSec) {
        effectiveDtSec = cfg_.fixedIntegrationDtSec;
    } else {
        effectiveDtSec = 0.0;
    }


    state_.dtSec = effectiveDtSec;


    if (rawDtSec > 0.0) {
        state_.isFresh = (rawDtSec * 1000.0) <= static_cast<double>(cfg_.staleTimeoutMs);
        state_.isStale = !state_.isFresh;
    } else {
        state_.isFresh = true;
        state_.isStale = false;
    }


    state_.valid = state_.referenceInitialized;
    state_.yawValid = imuFreshValid;


    const double normForward  = clampUnit(rawForwardCmd / 255.0);


    const bool estop = drive.emergencyStop;
    const bool hasForwardMotionCmd =
        std::fabs(rawForwardCmd) >= cfg_.minForwardCmdForMotion;

    const double steeringAlpha = std::max(0.0, std::min(1.0, cfg_.steeringSmoothingAlpha));
    const bool rawHasSteeringCmd =
        std::fabs(rawSteeringCmd) >= cfg_.minSteeringCmdForTurn;
    if (estop || !rawHasSteeringCmd) {
        // Decay to zero quickly when the stick is released. This prevents a
        // stale steering spike from bending the next straight segment.
        filteredSteeringCmd_ *= (1.0 - steeringAlpha);
        if (std::fabs(filteredSteeringCmd_) < cfg_.minSteeringCmdForTurn) {
            filteredSteeringCmd_ = 0.0;
        }
    } else {
        filteredSteeringCmd_ += steeringAlpha * (rawSteeringCmd - filteredSteeringCmd_);
    }

    const double normSteering = clampUnit(filteredSteeringCmd_ / 255.0);
    const bool hasSteeringCmd =
        std::fabs(filteredSteeringCmd_) >= cfg_.minSteeringCmdForTurn;


    double linearVelocityMps = 0.0;
    if (!estop && hasForwardMotionCmd) {
        const double scale = std::max(0.0, cfg_.linearDistanceScale);
        linearVelocityMps = normForward * cfg_.estimatedMaxLinearSpeedMps * scale;
    }
    state_.linearVelocityMps = linearVelocityMps;


    double steeringAngleDeg = 0.0;
    if (hasSteeringCmd) {
        steeringAngleDeg = normSteering * cfg_.maxSteeringAngleDeg;
    }
    const double steeringAngleRad = degToRad(steeringAngleDeg);


    // Ackermann approximation:
    // yaw_rate_rad = v * tan(delta) / L
    double ackermannYawRateDegPs = 0.0;
    const bool wheelbaseValid = cfg_.wheelBaseMeters > 1e-6;
    if (!estop && hasForwardMotionCmd && hasSteeringCmd && wheelbaseValid) {
        const double yawRateRadPs =
            linearVelocityMps * std::tan(steeringAngleRad) / cfg_.wheelBaseMeters;
        ackermannYawRateDegPs = radToDeg(yawRateRadPs) * cfg_.ackermannYawRateGain;
    }


    bool imuYawUsable = false;
    double imuYawRateDegPs = 0.0;


    if (imuFreshValid) {
        imuYawRateDegPs =
            cfg_.yawAxisSign *
            ((imu.gy * cfg_.yawAxisFromGy) + (imu.gz * cfg_.yawAxisFromGz));


        if (!hasForwardMotionCmd && !hasSteeringCmd) {
            imuYawRateDegPs =
                applyDeadband(imuYawRateDegPs, cfg_.gyroStationaryHoldDegPs);
        } else {
            imuYawRateDegPs =
                applyDeadband(imuYawRateDegPs, cfg_.gyroYawRateDeadbandDegPs);
        }


        imuYawUsable = (std::fabs(imuYawRateDegPs) > 1e-6);
    }


    double usedYawRateDegPs = 0.0;


    if (estop) {
        usedYawRateDegPs = 0.0;
        state_.poseSource = OdomPoseSource::None;
    } else if (hasForwardMotionCmd) {
        const bool ackermannUsable = std::fabs(ackermannYawRateDegPs) > 1e-6;


        if (imuYawUsable && ackermannUsable) {
            usedYawRateDegPs =
                (cfg_.imuYawBlendWeight * imuYawRateDegPs) +
                ((1.0 - cfg_.imuYawBlendWeight) * ackermannYawRateDegPs);
            state_.poseSource = OdomPoseSource::ImuYawRateCmdLinear;
        } else if (imuYawUsable) {
            usedYawRateDegPs = imuYawRateDegPs;
            state_.poseSource = OdomPoseSource::ImuYawRate;
        } else if (ackermannUsable) {
            usedYawRateDegPs = ackermannYawRateDegPs;
            state_.poseSource = OdomPoseSource::CmdYawRate;
        } else {
            usedYawRateDegPs = 0.0;
            state_.poseSource = OdomPoseSource::None;
        }
    } else {
        usedYawRateDegPs = 0.0;
        state_.poseSource = OdomPoseSource::None;
    }


    usedYawRateDegPs *= std::max(0.0, cfg_.turnYawScale);
    state_.angularVelocityDegPs = usedYawRateDegPs;


    const double yawDeltaDeg = (effectiveDtSec > 0.0) ? (usedYawRateDegPs * effectiveDtSec) : 0.0;

    // Midpoint integration: use the heading halfway through the tick for
    // translation, then apply the final yaw update. This reduces visible drift
    // on the R2 map overlay during turns.
    if (!estop && effectiveDtSec > 0.0 && std::fabs(linearVelocityMps) > 1e-6) {
        const double yawMidRad = degToRad(normalizeAngleDeg(state_.yawDeg + 0.5 * yawDeltaDeg));
        const double ds = linearVelocityMps * effectiveDtSec;


        state_.xMeters += std::cos(yawMidRad) * ds;
        state_.yMeters += std::sin(yawMidRad) * ds;
    }


    if (effectiveDtSec > 0.0 && std::fabs(yawDeltaDeg) > 1e-9) {
        state_.yawDeg = normalizeAngleDeg(state_.yawDeg + yawDeltaDeg);
    }


    state_.integrationActive =
        (!estop) &&
        (effectiveDtSec > 0.0) &&
        ((std::fabs(linearVelocityMps) > 1e-6) ||
         (std::fabs(usedYawRateDegPs) > 1e-6));


    lastUpdateMs_ = nowMs;
}


const OdomState& OdomRuntime::getState() const
{
    return state_;
}


const OdomRuntime::Config& OdomRuntime::config() const
{
    return cfg_;
}


OdomRuntime::Config& OdomRuntime::config()
{
    return cfg_;
}




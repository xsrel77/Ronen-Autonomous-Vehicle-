#include "dev_farm/DevFarmPoseProvider.h"


#include <cmath>
#include <algorithm>


void DevFarmPoseProvider::reset()
{
    lastAccepted_ = Pose{};
}


void DevFarmPoseProvider::setConfig(const Config& config)
{
    cfg_ = config;
    if (cfg_.freshTimeoutMs < 100) {
        cfg_.freshTimeoutMs = 100;
    }
    if (cfg_.maxDeltaMetersPerUpdate < 0.05) {
        cfg_.maxDeltaMetersPerUpdate = 0.05;
    }
    if (cfg_.maxDeltaYawDegPerUpdate < 1.0) {
        cfg_.maxDeltaYawDegPerUpdate = 1.0;
    }
}


const DevFarmPoseProvider::Config& DevFarmPoseProvider::config() const
{
    return cfg_;
}


const DevFarmPoseProvider::Pose& DevFarmPoseProvider::lastAcceptedPose() const
{
    return lastAccepted_;
}


bool DevFarmPoseProvider::isFinitePose(double xM, double yM, double yawDeg)
{
    return std::isfinite(xM) && std::isfinite(yM) && std::isfinite(yawDeg);
}


bool DevFarmPoseProvider::isFreshEnough(std::uint64_t sourceTimestampMs,
                                        std::uint64_t nowMs,
                                        std::uint64_t timeoutMs)
{
    if (sourceTimestampMs == 0) {
        // Some of the existing runtime structs can be valid before timestamp is populated.
        // Treat 0 as acceptable when the upstream state already says valid/fresh.
        return true;
    }
    if (sourceTimestampMs > nowMs) {
        return false;
    }
    return (nowMs - sourceTimestampMs) <= timeoutMs;
}


double DevFarmPoseProvider::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}


double DevFarmPoseProvider::absAngleDeltaDeg(double aDeg, double bDeg)
{
    return std::fabs(normalizeAngleDeg(aDeg - bDeg));
}


DevFarmPoseProvider::Pose DevFarmPoseProvider::makeOdomCandidate(const OdomState& odom,
                                                                 std::uint64_t nowMs) const
{
    Pose out{};
    out.source = "odom";
    out.timestampMs = odom.timestampMs;
    out.xM = odom.xMeters;
    out.yM = odom.yMeters;
    out.yawDeg = normalizeAngleDeg(odom.yawDeg);


    const bool poseFlagsOk = odom.valid && odom.yawValid;
    const bool referenceOk = odom.referenceInitialized || odom.integrationActive || odom.isFresh;
    const bool freshOk = odom.isFresh || isFreshEnough(odom.timestampMs, nowMs, cfg_.freshTimeoutMs);


    out.fresh = freshOk;
    out.valid = poseFlagsOk && referenceOk && freshOk && isFinitePose(out.xM, out.yM, out.yawDeg);
    if (!out.valid) {
        if (!poseFlagsOk) out.rejectReason = "odom_flags_not_ready";
        else if (!referenceOk) out.rejectReason = "odom_reference_not_ready";
        else if (!freshOk) out.rejectReason = "odom_stale";
        else out.rejectReason = "odom_not_finite";
    }
    return out;
}


DevFarmPoseProvider::Pose DevFarmPoseProvider::makeNavCandidate(const NavPoseState& nav,
                                                                std::uint64_t nowMs) const
{
    Pose out{};
    out.source = "nav";
    out.timestampMs = nav.timestampMs;
    out.xM = nav.xMeters;
    out.yM = nav.yMeters;
    out.yawDeg = normalizeAngleDeg(nav.yawDeg);


    const bool poseFlagsOk = nav.valid && nav.yawValid;
    const bool referenceOk = nav.referenceInitialized || nav.localized || nav.isFresh;
    const bool freshOk = nav.isFresh || isFreshEnough(nav.timestampMs, nowMs, cfg_.freshTimeoutMs);


    out.fresh = freshOk;
    out.valid = poseFlagsOk && referenceOk && freshOk && isFinitePose(out.xM, out.yM, out.yawDeg);
    if (!out.valid) {
        if (!poseFlagsOk) out.rejectReason = "nav_flags_not_ready";
        else if (!referenceOk) out.rejectReason = "nav_reference_not_ready";
        else if (!freshOk) out.rejectReason = "nav_stale";
        else out.rejectReason = "nav_not_finite";
    }
    return out;
}


DevFarmPoseProvider::Pose DevFarmPoseProvider::applyJumpGate(Pose candidate)
{
    if (!candidate.valid) {
        return candidate;
    }


    candidate.hasPrevious = lastAccepted_.valid;


    if (lastAccepted_.valid) {
        const double dx = candidate.xM - lastAccepted_.xM;
        const double dy = candidate.yM - lastAccepted_.yM;
        candidate.deltaMeters = std::sqrt(dx * dx + dy * dy);
        candidate.deltaYawDeg = absAngleDeltaDeg(candidate.yawDeg, lastAccepted_.yawDeg);


        if (candidate.deltaMeters > cfg_.maxDeltaMetersPerUpdate) {
            candidate.valid = false;
            candidate.rejected = true;
            candidate.rejectReason = "pose_jump_xy";
            return candidate;
        }
        if (candidate.deltaYawDeg > cfg_.maxDeltaYawDegPerUpdate) {
            candidate.valid = false;
            candidate.rejected = true;
            candidate.rejectReason = "pose_jump_yaw";
            return candidate;
        }
    }


    lastAccepted_ = candidate;
    return candidate;
}


DevFarmPoseProvider::Pose DevFarmPoseProvider::estimate(const OdomState& odom,
                                                        const NavPoseState& nav,
                                                        std::uint64_t nowMs)
{
    Pose odomCandidate = makeOdomCandidate(odom, nowMs);
    if (odomCandidate.valid) {
        return applyJumpGate(odomCandidate);
    }


    if (cfg_.allowNavFallback) {
        Pose navCandidate = makeNavCandidate(nav, nowMs);
        if (navCandidate.valid) {
            return applyJumpGate(navCandidate);
        }
        navCandidate.valid = false;
        navCandidate.rejected = true;
        navCandidate.source = "wait_pose";
        navCandidate.rejectReason = odomCandidate.rejectReason.empty()
            ? navCandidate.rejectReason
            : (odomCandidate.rejectReason + "+" + navCandidate.rejectReason);
        return navCandidate;
    }


    odomCandidate.valid = false;
    odomCandidate.rejected = true;
    odomCandidate.source = "wait_pose";
    return odomCandidate;
}






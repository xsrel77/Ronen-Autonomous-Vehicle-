#include "navigation/LidarPoseRuntime.h"


#include <algorithm>
#include <cmath>


namespace
{
constexpr double kPi = 3.14159265358979323846;
}


LidarPoseRuntime::LidarPoseRuntime(const Config& cfg)
    : cfg_(cfg)
{
}


void LidarPoseRuntime::reset()
{
    state_ = LidarPoseState{};
    state_.timeoutMs = cfg_.staleTimeoutMs;
}


const LidarPoseState& LidarPoseRuntime::getState() const
{
    return state_;
}


const LidarPoseRuntime::Config& LidarPoseRuntime::config() const
{
    return cfg_;
}


LidarPoseRuntime::Config& LidarPoseRuntime::config()
{
    return cfg_;
}


double LidarPoseRuntime::clamp01(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}


bool LidarPoseRuntime::validDist(double v)
{
    return v > 0.05;
}


bool LidarPoseRuntime::angleInRange(double angleDeg, double minDeg, double maxDeg)
{
    if (minDeg <= maxDeg) {
        return angleDeg >= minDeg && angleDeg <= maxDeg;
    }
    return angleDeg >= minDeg || angleDeg <= maxDeg;
}


double LidarPoseRuntime::computeSectorMinDistance(const std::vector<LidarPoint>& points,
                                                  double minAngleDeg,
                                                  double maxAngleDeg)
{
    double best = -1.0;


    for (const auto& p : points) {
        if (p.dist <= 0.05) {
            continue;
        }


        const double angleDeg = std::atan2(p.y, p.x) * 180.0 / kPi;
        if (!angleInRange(angleDeg, minAngleDeg, maxAngleDeg)) {
            continue;
        }


        if (best < 0.0 || p.dist < best) {
            best = p.dist;
        }
    }


    return best;
}


void LidarPoseRuntime::update(const LidarSnapshot& snapshot,
                              std::uint64_t nowMs)
{
    state_ = LidarPoseState{};
    state_.timeoutMs = cfg_.staleTimeoutMs;
    state_.timestampMs = snapshot.timestampMs > 0 ? snapshot.timestampMs : nowMs;


    state_.scanValid = snapshot.valid;
    state_.valid = snapshot.valid;
    state_.enoughPoints = snapshot.points.size() >= cfg_.minPointCountForConfidence;


    if (!snapshot.valid || snapshot.points.empty()) {
        state_.isFresh = false;
        state_.isStale = true;
        return;
    }


    // Coordinate convention from current LiDAR pipeline:
    // +90° ~= front, 0° ~= right, -90° ~= rear, ±180° ~= left
    state_.frontDistanceM      = computeSectorMinDistance(snapshot.points,  67.5, 112.5);
    state_.frontLeftDistanceM  = computeSectorMinDistance(snapshot.points, 112.5, 157.5);
    state_.leftDistanceM       = computeSectorMinDistance(snapshot.points, 157.5, -157.5);
    state_.rearLeftDistanceM   = computeSectorMinDistance(snapshot.points, -157.5, -112.5);
    state_.rearDistanceM       = computeSectorMinDistance(snapshot.points, -112.5,  -67.5);
    state_.rearRightDistanceM  = computeSectorMinDistance(snapshot.points,  -67.5,  -22.5);
    state_.rightDistanceM      = computeSectorMinDistance(snapshot.points,  -22.5,   22.5);
    state_.frontRightDistanceM = computeSectorMinDistance(snapshot.points,   22.5,   67.5);


    const double sectors[8] = {
        state_.frontDistanceM,
        state_.frontLeftDistanceM,
        state_.leftDistanceM,
        state_.rearLeftDistanceM,
        state_.rearDistanceM,
        state_.rearRightDistanceM,
        state_.rightDistanceM,
        state_.frontRightDistanceM
    };


    int validSectorCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (validDist(sectors[i])) {
            ++validSectorCount;
            if (state_.nearestDistanceM < 0.0 || sectors[i] < state_.nearestDistanceM) {
                state_.nearestDistanceM = sectors[i];
                state_.nearestSectorIndex = i;
            }
        }
    }


    if (validDist(state_.leftDistanceM) && validDist(state_.rightDistanceM)) {
        state_.lateralBalanceM = state_.rightDistanceM - state_.leftDistanceM;
        state_.centerErrorM = state_.lateralBalanceM * cfg_.centerErrorGain;
    }


    if (validDist(state_.frontLeftDistanceM) && validDist(state_.frontRightDistanceM)) {
        state_.frontBalanceM = state_.frontRightDistanceM - state_.frontLeftDistanceM;
        state_.headingHintDeg = state_.frontBalanceM * cfg_.headingHintGainDegPerMeter;
    }


    if (validDist(state_.rearLeftDistanceM) && validDist(state_.rearRightDistanceM)) {
        state_.rearBalanceM = state_.rearRightDistanceM - state_.rearLeftDistanceM;
    }


    state_.frontClearanceM = state_.frontDistanceM;
    state_.rearClearanceM  = state_.rearDistanceM;


    state_.frontLeftBlocked  = validDist(state_.frontLeftDistanceM)  && state_.frontLeftDistanceM  < cfg_.obstacleThresholdM;
    state_.frontRightBlocked = validDist(state_.frontRightDistanceM) && state_.frontRightDistanceM < cfg_.obstacleThresholdM;
    state_.leftBlocked       = validDist(state_.leftDistanceM)       && state_.leftDistanceM       < cfg_.obstacleThresholdM;
    state_.rightBlocked      = validDist(state_.rightDistanceM)      && state_.rightDistanceM      < cfg_.obstacleThresholdM;
    state_.rearLeftBlocked   = validDist(state_.rearLeftDistanceM)   && state_.rearLeftDistanceM   < cfg_.obstacleThresholdM;
    state_.rearRightBlocked  = validDist(state_.rearRightDistanceM)  && state_.rearRightDistanceM  < cfg_.obstacleThresholdM;


    state_.obstacleAhead =
        (validDist(state_.frontDistanceM) && state_.frontDistanceM < cfg_.obstacleThresholdM) ||
        state_.frontLeftBlocked ||
        state_.frontRightBlocked;


    state_.obstacleRear =
        (validDist(state_.rearDistanceM) && state_.rearDistanceM < cfg_.obstacleThresholdM) ||
        state_.rearLeftBlocked ||
        state_.rearRightBlocked;


    double sectorCoverageScore = static_cast<double>(validSectorCount) / 8.0;
    double pointsScore = 0.0;
    if (cfg_.minPointCountForConfidence > 0) {
        pointsScore = std::min(
            1.0,
            static_cast<double>(snapshot.points.size()) /
            static_cast<double>(cfg_.minPointCountForConfidence * 2)
        );
    }


    state_.confidence = clamp01(0.65 * sectorCoverageScore + 0.35 * pointsScore);


    const std::uint64_t ageMs = (nowMs >= state_.timestampMs) ? (nowMs - state_.timestampMs) : 0;
    state_.isFresh = state_.valid && (ageMs <= cfg_.staleTimeoutMs);
    state_.isStale = state_.valid && !state_.isFresh;
}




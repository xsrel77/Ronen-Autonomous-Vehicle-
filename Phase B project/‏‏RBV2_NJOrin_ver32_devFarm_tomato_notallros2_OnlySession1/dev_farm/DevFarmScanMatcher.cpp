#include "dev_farm/DevFarmScanMatcher.h"




#include <algorithm>
#include <cmath>
#include <limits>




namespace {
constexpr double PI_D = 3.14159265358979323846;
}




void DevFarmScanMatcher::reset()
{
    occupiedCells_.clear();
}




std::size_t DevFarmScanMatcher::mapCellsCount() const
{
    return occupiedCells_.size();
}




void DevFarmScanMatcher::addMapPoint(double xM, double yM)
{
    occupiedCells_.insert(makeKey(toCell(xM), toCell(yM)));
}




DevFarmScanMatcher::Result DevFarmScanMatcher::match(const LidarSnapshot& snapshot,
                                                     const OdomState& odom,
                                                     std::uint64_t nowMs) const
{
    if (!odom.valid || !odom.referenceInitialized) {
        Result r{};
        r.mapCells = occupiedCells_.size();
        r.mapReady = r.mapCells >= kMinMapCellsForMatch;
        return r;
    }


    return matchPose(snapshot, odom.xMeters, odom.yMeters, odom.yawDeg, nowMs);
}




DevFarmScanMatcher::Result DevFarmScanMatcher::matchPose(const LidarSnapshot& snapshot,
                                                         double poseXM,
                                                         double poseYM,
                                                         double poseYawDeg,
                                                         std::uint64_t /*nowMs*/) const
{
    Result result{};
    result.mapCells = occupiedCells_.size();
    result.mapReady = result.mapCells >= kMinMapCellsForMatch;




    if (!result.mapReady || !snapshot.valid || snapshot.points.empty()) {
        return result;
    }




    if (!std::isfinite(poseXM) || !std::isfinite(poseYM) || !std::isfinite(poseYawDeg)) {
        return result;
    }




    std::vector<LidarPoint> sampled;
    sampled.reserve(kMaxMatchPoints);




    std::size_t seen = 0;
    for (const auto& p : snapshot.points) {
        if (p.dist < kMinDistanceM || p.dist > kMaxDistanceM) {
            continue;
        }




        if ((seen % kMatchPointStride) == 0U) {
            sampled.push_back(p);
            if (sampled.size() >= kMaxMatchPoints) {
                break;
            }
        }
        ++seen;
    }




    result.usedPoints = sampled.size();
    if (sampled.size() < 35) {
        return result;
    }




    auto scoreCandidate = [&](double dxM, double dyM, double dYawDeg) -> double {
        const double yawDeg = normalizeAngleDeg(poseYawDeg + dYawDeg);
        const double yawRad = yawDeg * PI_D / 180.0;
        const double c = std::cos(yawRad);
        const double s = std::sin(yawRad);




        std::size_t hits = 0;
        for (const auto& p : sampled) {
            const double x = poseXM + dxM + c * p.x - s * p.y;
            const double y = poseYM + dyM + s * p.x + c * p.y;
            if (isCellOccupiedNear(toCell(x), toCell(y))) {
                ++hits;
            }
        }




        return static_cast<double>(hits) / static_cast<double>(sampled.size());
    };




    result.attempted = true;
    result.baseScore = scoreCandidate(0.0, 0.0, 0.0);




    double bestScore = result.baseScore;
    double bestDx = 0.0;
    double bestDy = 0.0;
    double bestYaw = 0.0;




    // Coarse-to-fine search around the current predicted map pose.
    // This intentionally stays small: it fixes odom/yaw drift without allowing a bad scan
    // to teleport the map frame.
    static constexpr double yawCoarse[] = {-5.0, -3.0, -1.5, 0.0, 1.5, 3.0, 5.0};
    static constexpr double xyCoarse[] = {-0.10, -0.05, 0.0, 0.05, 0.10};




    for (double dyaw : yawCoarse) {
        for (double dx : xyCoarse) {
            for (double dy : xyCoarse) {
                const double score = scoreCandidate(dx, dy, dyaw);
                if (score > bestScore) {
                    bestScore = score;
                    bestDx = dx;
                    bestDy = dy;
                    bestYaw = dyaw;
                }
            }
        }
    }




    static constexpr double yawFine[] = {-1.0, -0.5, 0.0, 0.5, 1.0};
    static constexpr double xyFine[] = {-0.025, 0.0, 0.025};




    const double coarseDx = bestDx;
    const double coarseDy = bestDy;
    const double coarseYaw = bestYaw;




    for (double dyaw : yawFine) {
        for (double dx : xyFine) {
            for (double dy : xyFine) {
                const double candDx = coarseDx + dx;
                const double candDy = coarseDy + dy;
                const double candYaw = coarseYaw + dyaw;
                const double score = scoreCandidate(candDx, candDy, candYaw);
                if (score > bestScore) {
                    bestScore = score;
                    bestDx = candDx;
                    bestDy = candDy;
                    bestYaw = candYaw;
                }
            }
        }
    }




    result.score = bestScore;
    result.dxM = bestDx;
    result.dyM = bestDy;
    result.dYawDeg = bestYaw;
    result.improvement = bestScore - result.baseScore;




    const bool hasNonZeroCorrection =
        (std::fabs(bestDx) > 0.0005) ||
        (std::fabs(bestDy) > 0.0005) ||
        (std::fabs(bestYaw) > 0.05);




    result.accepted =
        hasNonZeroCorrection &&
        (bestScore >= kAcceptScore) &&
        (result.improvement >= kAcceptImprovement);




    if (!result.accepted) {
        result.dxM = 0.0;
        result.dyM = 0.0;
        result.dYawDeg = 0.0;
    }




    return result;
}




bool DevFarmScanMatcher::isCellOccupiedNear(int ix, int iy) const
{
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (occupiedCells_.find(makeKey(ix + dx, iy + dy)) != occupiedCells_.end()) {
                return true;
            }
        }
    }
    return false;
}




long long DevFarmScanMatcher::makeKey(int ix, int iy)
{
    const long long x = static_cast<long long>(ix) & 0xffffffffLL;
    const long long y = static_cast<long long>(iy) & 0xffffffffLL;
    return (x << 32) ^ y;
}




int DevFarmScanMatcher::toCell(double meters)
{
    return static_cast<int>(std::llround(meters / kGridResolutionM));
}




double DevFarmScanMatcher::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}






#include "dev_farm/DevFarmOccupancyGridMapper.h"


#include <algorithm>
#include <cmath>
#include <cstdlib>


namespace {
constexpr double PI_D = 3.14159265358979323846;
}


void DevFarmOccupancyGridMapper::reset()
{
    cells_.clear();
    stats_ = Stats{};
}


void DevFarmOccupancyGridMapper::setConfig(const Config& config)
{
    cfg_ = config;
    if (cfg_.resolutionM < 0.01) {
        cfg_.resolutionM = 0.01;
    }
    if (cfg_.beamStride == 0) {
        cfg_.beamStride = 1;
    }
}


const DevFarmOccupancyGridMapper::Config& DevFarmOccupancyGridMapper::config() const
{
    return cfg_;
}


DevFarmOccupancyGridMapper::Stats DevFarmOccupancyGridMapper::stats() const
{
    Stats out = stats_;
    out.totalCells = cells_.size();
    out.occupiedCells = 0;
    out.freeCells = 0;


    for (const auto& kv : cells_) {
        const Cell& c = kv.second;
        if (c.logOdds >= cfg_.occupiedThreshold) {
            out.occupiedCells += 1;
        } else if (c.logOdds <= -cfg_.occupiedThreshold) {
            out.freeCells += 1;
        }
    }


    return out;
}


std::int64_t DevFarmOccupancyGridMapper::packCell(int x, int y)
{
    return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
}


int DevFarmOccupancyGridMapper::unpackX(std::int64_t key)
{
    return static_cast<int>(key >> 32);
}


int DevFarmOccupancyGridMapper::unpackY(std::int64_t key)
{
    return static_cast<int>(static_cast<std::uint32_t>(key & 0xFFFFFFFFULL));
}


int DevFarmOccupancyGridMapper::worldToCell(double value) const
{
    return static_cast<int>(std::floor(value / cfg_.resolutionM));
}


double DevFarmOccupancyGridMapper::cellToWorld(int cell) const
{
    return (static_cast<double>(cell) + 0.5) * cfg_.resolutionM;
}


bool DevFarmOccupancyGridMapper::updateCell(int x,
                                            int y,
                                            double delta,
                                            bool occupied,
                                            std::uint64_t nowMs)
{
    const std::int64_t key = packCell(x, y);
    auto it = cells_.find(key);


    if (it == cells_.end()) {
        if (cells_.size() >= cfg_.maxCells) {
            stats_.maxCellsReached = true;
            return false;
        }
        it = cells_.emplace(key, Cell{}).first;
    }


    Cell& c = it->second;
    c.logOdds = std::max(cfg_.minLogOdds, std::min(cfg_.maxLogOdds, c.logOdds + delta));
    c.lastUpdateMs = nowMs;


    if (occupied) {
        c.occHits += 1;
        stats_.occupiedUpdates += 1;
    } else {
        c.freeHits += 1;
        stats_.freeUpdates += 1;
    }


    return true;
}


bool DevFarmOccupancyGridMapper::isStableOccupiedCell(int x, int y) const
{
    const auto it = cells_.find(packCell(x, y));
    if (it == cells_.end()) {
        return false;
    }


    const Cell& c = it->second;
    return c.logOdds >= cfg_.stableWallLogOdds && c.occHits >= cfg_.stableWallMinHits;
}


bool DevFarmOccupancyGridMapper::findStableEndpointNear(int x, int y, int& outX, int& outY) const
{
    if (!cfg_.wallProtectionEnabled || cfg_.endpointSnapRadiusCells <= 0) {
        return false;
    }


    int bestX = x;
    int bestY = y;
    int bestDist2 = 1000000;


    for (int dy = -cfg_.endpointSnapRadiusCells; dy <= cfg_.endpointSnapRadiusCells; ++dy) {
        for (int dx = -cfg_.endpointSnapRadiusCells; dx <= cfg_.endpointSnapRadiusCells; ++dx) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > cfg_.endpointSnapRadiusCells * cfg_.endpointSnapRadiusCells) {
                continue;
            }


            const int cx = x + dx;
            const int cy = y + dy;
            if (!isStableOccupiedCell(cx, cy)) {
                continue;
            }


            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestX = cx;
                bestY = cy;
            }
        }
    }


    if (bestDist2 == 1000000) {
        return false;
    }


    outX = bestX;
    outY = bestY;
    return true;
}


bool DevFarmOccupancyGridMapper::traceFreeCells(int x0,
                                                int y0,
                                                int x1,
                                                int y1,
                                                int& protectedX,
                                                int& protectedY,
                                                std::uint64_t nowMs)
{
    int dx = std::abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;


    int x = x0;
    int y = y0;
    int step = 0;


    const int maxSteps = std::max(1, dx + (-dy) + 4);


    for (int guard = 0; guard < maxSteps; ++guard) {
        if (x == x1 && y == y1) {
            break;
        }


        const int remain = std::abs(x1 - x) + std::abs(y1 - y);


        if (cfg_.wallProtectionEnabled &&
            step >= cfg_.freeStartSkipCells &&
            remain > cfg_.wallProtectionEndSkipCells &&
            isStableOccupiedCell(x, y)) {
            // We hit a wall that the map already trusts. Stop this ray here so a small
            // odom/yaw error will not clear through the wall or create duplicate walls behind it.
            protectedX = x;
            protectedY = y;
            updateCell(x, y, cfg_.stableWallReinforceLogOdds, true, nowMs);
            stats_.wallProtectionStops += 1;
            return false;
        }


        if (step >= cfg_.freeStartSkipCells && remain > cfg_.freeEndSkipCells) {
            updateCell(x, y, cfg_.freeLogOdds, false, nowMs);
        }


        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }


        ++step;
    }


    return true;
}


void DevFarmOccupancyGridMapper::updateFromSnapshot(const LidarSnapshot& snapshot,
                                                    double robotXM,
                                                    double robotYM,
                                                    double robotYawDeg,
                                                    bool poseValid,
                                                    std::uint64_t nowMs)
{
    if (!snapshot.valid || snapshot.points.empty()) {
        return;
    }


    const double yawRad = (poseValid ? robotYawDeg : 0.0) * PI_D / 180.0;
    const double cosYaw = std::cos(yawRad);
    const double sinYaw = std::sin(yawRad);


    const double originX = poseValid ? robotXM : 0.0;
    const double originY = poseValid ? robotYM : 0.0;
    const int startCellX = worldToCell(originX);
    const int startCellY = worldToCell(originY);


    std::size_t beamIndex = 0;
    for (const auto& p : snapshot.points) {
        stats_.sourceSamples += 1;
        ++beamIndex;


        if ((beamIndex % cfg_.beamStride) != 0U) {
            continue;
        }


        if (p.dist < cfg_.minRangeM || p.dist > cfg_.maxRangeM) {
            continue;
        }


        const double hitX = originX + cosYaw * p.x - sinYaw * p.y;
        const double hitY = originY + sinYaw * p.x + cosYaw * p.y;


        const int endCellX = worldToCell(hitX);
        const int endCellY = worldToCell(hitY);


        int protectedX = endCellX;
        int protectedY = endCellY;
        const bool rayReachedEndpoint =
            traceFreeCells(startCellX, startCellY, endCellX, endCellY, protectedX, protectedY, nowMs);


        int finalEndX = endCellX;
        int finalEndY = endCellY;


        if (!rayReachedEndpoint) {
            finalEndX = protectedX;
            finalEndY = protectedY;
        } else {
            int snapX = endCellX;
            int snapY = endCellY;
            if (findStableEndpointNear(endCellX, endCellY, snapX, snapY)) {
                finalEndX = snapX;
                finalEndY = snapY;
                stats_.endpointSnaps += 1;
            }
        }


        updateCell(finalEndX, finalEndY, cfg_.occupiedLogOdds, true, nowMs);
        stats_.raysIntegrated += 1;


        if (stats_.maxCellsReached) {
            break;
        }
    }
}


void DevFarmOccupancyGridMapper::exportOccupiedPoints(std::vector<DevFarmMapPoint>& out,
                                                      std::size_t maxPoints,
                                                      bool includeAll,
                                                      std::uint64_t timestampMs) const
{
    out.clear();


    const Stats st = stats();
    const std::size_t occupiedCount = st.occupiedCells;
    if (occupiedCount == 0) {
        return;
    }


    const std::size_t limit = includeAll ? occupiedCount : std::min<std::size_t>(occupiedCount, maxPoints);
    const std::size_t stride = includeAll ? 1U : std::max<std::size_t>(1U, occupiedCount / std::max<std::size_t>(1U, maxPoints));


    out.reserve(std::min<std::size_t>(limit, maxPoints == 0 ? limit : maxPoints));


    std::size_t seenOccupied = 0;
    for (const auto& kv : cells_) {
        const Cell& c = kv.second;
        if (c.logOdds < cfg_.occupiedThreshold) {
            continue;
        }


        if (!includeAll && (seenOccupied % stride) != 0U) {
            ++seenOccupied;
            continue;
        }


        DevFarmMapPoint p{};
        const int cx = unpackX(kv.first);
        const int cy = unpackY(kv.first);
        p.xM = cellToWorld(cx);
        p.yM = cellToWorld(cy);
        p.localXM = p.xM;
        p.localYM = p.yM;
        p.distanceM = 2.0; // keeps occupancy points blue/calm in the existing color scheme
        p.angleDeg = normalizeAngleDeg(std::atan2(p.yM, p.xM) * 180.0 / PI_D);
        p.robotXM = 0.0;
        p.robotYM = 0.0;
        p.robotYawDeg = 0.0;
        p.timestampMs = timestampMs;
        p.hits = c.occHits;


        out.push_back(p);
        ++seenOccupied;


        if (!includeAll && out.size() >= maxPoints) {
            break;
        }
    }
}


double DevFarmOccupancyGridMapper::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}






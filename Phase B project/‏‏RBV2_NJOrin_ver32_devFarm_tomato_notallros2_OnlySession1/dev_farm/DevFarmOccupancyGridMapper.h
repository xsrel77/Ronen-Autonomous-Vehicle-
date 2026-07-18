#pragma once


#include "core/LidarTypes.h"
#include "core/SystemState.h"


#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>


class DevFarmOccupancyGridMapper {
public:
    struct Config {
        double resolutionM = 0.05;          // 5cm cells: good balance for 20m x 14m greenhouse
        double minRangeM = 0.18;            // ignore robot body / very near noisy returns
        double maxRangeM = 8.0;             // clamp far noisy rays
        double occupiedLogOdds = 0.85;      // endpoint update
        double freeLogOdds = -0.35;         // ray clearing update
        double minLogOdds = -3.50;
        double maxLogOdds = 4.50;
        double occupiedThreshold = 0.85;    // cells above this are displayed/saved
        std::size_t beamStride = 4;         // use 1 of every 4 LiDAR beams for grid update
        std::size_t maxCells = 1000000;     // safety cap
        int freeStartSkipCells = 2;         // do not clear robot footprint around LiDAR
        int freeEndSkipCells = 2;           // keep a small wall thickness near hit endpoint


        // Protect already-stable walls from being erased or duplicated when yaw/odom has small errors.
        bool wallProtectionEnabled = true;
        double stableWallLogOdds = 1.35;
        std::uint32_t stableWallMinHits = 8;
        int wallProtectionEndSkipCells = 3;
        int endpointSnapRadiusCells = 2;
        double stableWallReinforceLogOdds = 0.20;
    };


    struct Stats {
        std::size_t totalCells = 0;
        std::size_t occupiedCells = 0;
        std::size_t freeCells = 0;
        std::uint64_t sourceSamples = 0;
        std::uint64_t raysIntegrated = 0;
        std::uint64_t occupiedUpdates = 0;
        std::uint64_t freeUpdates = 0;
        std::uint64_t wallProtectionStops = 0;
        std::uint64_t endpointSnaps = 0;
        bool maxCellsReached = false;
    };


    DevFarmOccupancyGridMapper() = default;


    void reset();
    void setConfig(const Config& config);
    const Config& config() const;
    Stats stats() const;


    void updateFromSnapshot(const LidarSnapshot& snapshot,
                            double robotXM,
                            double robotYM,
                            double robotYawDeg,
                            bool poseValid,
                            std::uint64_t nowMs);


    void exportOccupiedPoints(std::vector<DevFarmMapPoint>& out,
                              std::size_t maxPoints,
                              bool includeAll,
                              std::uint64_t timestampMs) const;


private:
    struct Cell {
        double logOdds = 0.0;
        std::uint32_t occHits = 0;
        std::uint32_t freeHits = 0;
        std::uint64_t lastUpdateMs = 0;
    };


    static std::int64_t packCell(int x, int y);
    static int unpackX(std::int64_t key);
    static int unpackY(std::int64_t key);


    int worldToCell(double value) const;
    double cellToWorld(int cell) const;


    bool updateCell(int x, int y, double delta, bool occupied, std::uint64_t nowMs);
    bool isStableOccupiedCell(int x, int y) const;
    bool findStableEndpointNear(int x, int y, int& outX, int& outY) const;
    bool traceFreeCells(int x0, int y0, int x1, int y1, int& protectedX, int& protectedY, std::uint64_t nowMs);
    static double normalizeAngleDeg(double angleDeg);


private:
    Config cfg_{};
    std::unordered_map<std::int64_t, Cell> cells_{};
    Stats stats_{};
};






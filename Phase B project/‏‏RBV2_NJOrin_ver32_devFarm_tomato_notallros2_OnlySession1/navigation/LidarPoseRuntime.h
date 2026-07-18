#pragma once


#include <cstdint>
#include <vector>


#include "core/SystemState.h"


class LidarPoseRuntime
{
public:
    struct Config
    {
        double obstacleThresholdM = 0.50;
        double headingHintGainDegPerMeter = 35.0;
        double centerErrorGain = 0.50;


        std::size_t minPointCountForConfidence = 40;
        std::uint64_t staleTimeoutMs = 300;
    };


public:
    LidarPoseRuntime() = default;
    explicit LidarPoseRuntime(const Config& cfg);


    void reset();


    void update(const LidarSnapshot& snapshot,
                std::uint64_t nowMs);


    const LidarPoseState& getState() const;
    const Config& config() const;
    Config& config();


private:
    static bool angleInRange(double angleDeg, double minDeg, double maxDeg);
    static double computeSectorMinDistance(const std::vector<LidarPoint>& points,
                                           double minAngleDeg,
                                           double maxAngleDeg);
    static double clamp01(double v);
    static bool validDist(double v);


private:
    Config cfg_{};
    LidarPoseState state_{};
};




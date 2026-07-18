#pragma once


#include <cstdint>


#include "core/SystemState.h"


class LidarCorrectionHintsRuntime
{
public:
    struct Config
    {
        double forwardClearanceMinM = 0.45;
        double reverseClearanceMinM = 0.35;


        double centerDeadbandM = 0.08;
        double headingDeadbandDeg = 4.0;


        double reverseSuggestFrontM = 0.32;
        double reverseSuggestCenterErrorM = 0.18;


        double steerStrengthCenterScaleM = 0.35;
        double steerStrengthHeadingScaleDeg = 18.0;


        std::uint64_t staleTimeoutMs = 300;
    };


public:
    LidarCorrectionHintsRuntime() = default;
    explicit LidarCorrectionHintsRuntime(const Config& cfg);


    void reset();


    void update(const LidarPoseState& lidarPose,
                const NavPoseState& nav,
                std::uint64_t nowMs);


    const LidarCorrectionHintsState& getState() const;
    const Config& config() const;
    Config& config();


private:
    static double clamp01(double v);
    static double absMax(double a, double b);


private:
    Config cfg_{};
    LidarCorrectionHintsState state_{};
};




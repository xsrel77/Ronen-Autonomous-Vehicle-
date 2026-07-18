#pragma once


#include <cstdint>


#include "core/SystemState.h"


class OdomRuntime
{
public:
    struct Config
    {
        double estimatedMaxLinearSpeedMps = 0.45;


        // RBV2 ver29 fix4:
        // The robot has no wheel encoders, so odom is estimated from drive command.
        // Real-world calibration on 2026-06-28: 1.60m physical drive produced
        // about 0.84m estimated odom before scaling. For ver30 map overlay,
        // keep a moderate estimated scale: approximate map progress, not precision navigation.
        double linearDistanceScale = 2.7;


        // Vehicle geometry (Ackermann approximation)
        double wheelBaseMeters = 0.32;
        double maxSteeringAngleDeg = 40.0;


        // Slight gain to strengthen lateral / yaw contribution from the kinematic model.
        double ackermannYawRateGain = 1.15;

        // Ver31 map-overlay stabilization:
        // The robot has no wheel encoders, and turns looked too "drifty" on the
        // R2 map overlay. Keep linear progress separate from turn strength.
        // 1.0 = old yaw behavior, lower values reduce turn arc exaggeration.
        double turnYawScale = 0.55;

        // Smooth short steering spikes from the PS4 stick before applying the
        // Ackermann yaw estimate. 0.0 = frozen, 1.0 = no smoothing.
        double steeringSmoothingAlpha = 0.22;


        // IMU yaw extraction
        double yawAxisFromGy = 1.00;
        double yawAxisFromGz = 0.20;
        double yawAxisSign = 1.00;


        // IMU deadbands
        double gyroYawRateDeadbandDegPs = 3.0;
        double gyroStationaryHoldDegPs = 1.2;


        // Command thresholds
        double minForwardCmdForMotion = 8.0;
        double minSteeringCmdForTurn = 8.0;


        // Blend between kinematic Ackermann yaw-rate and IMU yaw-rate
        // 0.0 = command/kinematic only
        // 1.0 = IMU only
        double imuYawBlendWeight = 0.40;


        // Timing
        double maxAcceptedDtSec = 0.25;
        double fixedIntegrationDtSec = 0.02;


        std::uint64_t staleTimeoutMs = 300;
    };


public:
    OdomRuntime() = default;
    explicit OdomRuntime(const Config& cfg);


    void reset();


    void update(const M5ImuState& imu,
                const DriveState& drive,
                std::uint64_t nowMs);


    const OdomState& getState() const;
    const Config& config() const;
    Config& config();


private:
    static double normalizeAngleDeg(double angleDeg);
    static double clampUnit(double v);
    static double applyDeadband(double value, double deadbandAbs);


private:
    Config cfg_{};
    OdomState state_{};
    std::uint64_t lastUpdateMs_ = 0;
    double filteredSteeringCmd_ = 0.0;
};




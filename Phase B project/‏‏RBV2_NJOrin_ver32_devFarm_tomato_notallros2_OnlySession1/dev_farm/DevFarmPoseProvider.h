#pragma once


#include "core/SystemState.h"


#include <cstdint>
#include <string>


class DevFarmPoseProvider {
public:
    struct Config {
        std::uint64_t freshTimeoutMs = 700;
        double maxDeltaMetersPerUpdate = 0.85;
        double maxDeltaYawDegPerUpdate = 18.0;
        bool allowNavFallback = true;
    };


    struct Pose {
        bool valid = false;
        bool fresh = false;
        bool rejected = false;
        bool hasPrevious = false;


        std::string source;
        std::string rejectReason;


        double xM = 0.0;
        double yM = 0.0;
        double yawDeg = 0.0;
        double deltaMeters = 0.0;
        double deltaYawDeg = 0.0;


        std::uint64_t timestampMs = 0;
    };


public:
    DevFarmPoseProvider() = default;


    void reset();
    void setConfig(const Config& config);
    const Config& config() const;


    Pose estimate(const OdomState& odom,
                  const NavPoseState& nav,
                  std::uint64_t nowMs);


    const Pose& lastAcceptedPose() const;


private:
    static bool isFinitePose(double xM, double yM, double yawDeg);
    static bool isFreshEnough(std::uint64_t sourceTimestampMs,
                              std::uint64_t nowMs,
                              std::uint64_t timeoutMs);
    static double normalizeAngleDeg(double angleDeg);
    static double absAngleDeltaDeg(double aDeg, double bDeg);


    Pose makeOdomCandidate(const OdomState& odom,
                           std::uint64_t nowMs) const;
    Pose makeNavCandidate(const NavPoseState& nav,
                          std::uint64_t nowMs) const;
    Pose applyJumpGate(Pose candidate);


private:
    Config cfg_{};
    Pose lastAccepted_{};
};






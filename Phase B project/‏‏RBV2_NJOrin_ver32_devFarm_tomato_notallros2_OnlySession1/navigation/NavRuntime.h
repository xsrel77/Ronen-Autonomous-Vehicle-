#pragma once


#include <array>
#include <cstdint>
#include "core/SystemState.h"


struct NavDebugSnapshot
{
    std::uint64_t timestampMs = 0;


    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;


    double gxRaw = 0.0;
    double gyRaw = 0.0;
    double gzRaw = 0.0;


    double yawAxisRaw = 0.0;
    double yawAxisMedian = 0.0;


    double accelNorm = 0.0;
    double jerk = 0.0;


    // backward-compatible names for existing CSV/debug code
    double gzMedian = 0.0;
    double gyroBiasZ = 0.0;


    double gyroBiasYaw = 0.0;


    double gxFiltered = 0.0;
    double gyFiltered = 0.0;
    double gzFiltered = 0.0;
    double yawRateFiltered = 0.0;


    double dtSec = 0.0;
    double yawBefore = 0.0;
    double yawAfter = 0.0;


    double steeringCmd = 0.0;
    double forwardCmd = 0.0;


    bool imuUsable = false;
    bool biasReady = false;
    bool stationary = false;
    bool shockDetected = false;
    bool crossAxisTooHigh = false;
    bool steeringActive = false;
    bool signMismatch = false;
    bool straightMotionSuppressed = false;
    bool integrated = false;
    bool zeroSnapped = false;


    // extra context for local-nav debugging
    int fbStepCmd = 0;
    int lrStepCmd = 0;
    int selectedFbSpeed = 0;
    int selectedLrSpeed = 0;
};


class NavRuntime
{
public:
    NavRuntime() = default;


    void reset();


    void updateFromImu(const M5ImuState& imu,
                       bool lidarFresh,
                       double steeringCmd,
                       double forwardCmd,
                       std::uint64_t nowMs);


    const NavPoseState& getState() const;
    const NavDebugSnapshot& getDebug() const;


private:
    static double normalizeAngleDeg(double angleDeg);
    static bool accelNormLooksLikeGravity(double norm);
    static double medianOfWindow(const std::array<double, 5>& values, int count);


private:
    NavPoseState state_{};
    NavDebugSnapshot debug_{};


    bool yawIntegratorInitialized_ = false;
    bool gyroBiasReady_ = false;
    bool rateFilterInitialized_ = false;
    bool accelHistoryInitialized_ = false;


    double yawDegAccum_ = 0.0;
    double gyroBiasYawDegPs_ = 0.0;
    double gyroBiasAccum_ = 0.0;
    int gyroBiasSampleCount_ = 0;


    double yawRateFilteredDegPs_ = 0.0;
    double gxFilteredDegPs_ = 0.0;
    double gyFilteredDegPs_ = 0.0;
    double gzFilteredDegPs_ = 0.0;


    double prevAx_ = 0.0;
    double prevAy_ = 0.0;
    double prevAz_ = 0.0;


    std::uint64_t lastYawUpdateMs_ = 0;


    std::array<double, 5> yawMedianWindow_{};
    int yawMedianCount_ = 0;
    int yawMedianIndex_ = 0;


    // yaw axis mapping:
    // לפי הלוגים שלך gy דומיננטי יותר מ-gz
    static constexpr double kYawAxisFromGy = 1.00;
    static constexpr double kYawAxisFromGz = 0.20;
    static constexpr double kYawAxisSign   = 1.00;   // אם הכיוון הפוך, שנה ל--1.00


    static constexpr int kGyroBiasTargetSamples = 80;
    static constexpr double kYawDeadbandDegPs = 0.50;
    static constexpr double kGyroCutoffHz = 4.0;


    static constexpr double kStationaryGyroXYDegPs = 1.6;
    static constexpr double kStationaryYawDegPs    = 0.8;


    static constexpr double kZeroSnapDeg = 0.8;
    static constexpr double kZeroSnapRateDegPs = 0.35;


    static constexpr double kShockJerkThreshold = 8.0;
    static constexpr double kShockAccelDeltaThreshold = 0.10;


    static constexpr double kSteeringActiveThreshold = 22.0;
    static constexpr double kDriveActiveThreshold = 18.0;
    static constexpr double kSignCheckMinRateDegPs = 2.5;
    static constexpr double kFreeYawRateWithoutSteerDegPs = 8.5;
    static constexpr double kCrossAxisHighDegPs = 6.0;
    static constexpr double kSmallYawRateDegPs = 2.5;
};




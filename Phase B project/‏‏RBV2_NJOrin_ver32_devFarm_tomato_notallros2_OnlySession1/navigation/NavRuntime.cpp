#include "navigation/NavRuntime.h"


#include <algorithm>
#include <cmath>
#include <iostream>


namespace
{
constexpr double kPi = 3.14159265358979323846;


static int signOf(double v, double eps = 1.0e-6)
{
    if (v > eps) return +1;
    if (v < -eps) return -1;
    return 0;
}
}


void NavRuntime::reset()
{
    state_ = NavPoseState{};
    debug_ = NavDebugSnapshot{};


    yawIntegratorInitialized_ = false;
    gyroBiasReady_ = false;
    rateFilterInitialized_ = false;
    accelHistoryInitialized_ = false;


    yawDegAccum_ = 0.0;
    gyroBiasYawDegPs_ = 0.0;
    gyroBiasAccum_ = 0.0;
    gyroBiasSampleCount_ = 0;


    yawRateFilteredDegPs_ = 0.0;
    gxFilteredDegPs_ = 0.0;
    gyFilteredDegPs_ = 0.0;
    gzFilteredDegPs_ = 0.0;


    prevAx_ = 0.0;
    prevAy_ = 0.0;
    prevAz_ = 0.0;


    lastYawUpdateMs_ = 0;


    yawMedianWindow_.fill(0.0);
    yawMedianCount_ = 0;
    yawMedianIndex_ = 0;
}


double NavRuntime::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}


bool NavRuntime::accelNormLooksLikeGravity(double norm)
{
    const bool looksLike1g   = std::fabs(norm - 1.0)  <= 0.20;
    const bool looksLike9p81 = std::fabs(norm - 9.81) <= 1.50;
    return looksLike1g || looksLike9p81;
}


double NavRuntime::medianOfWindow(const std::array<double, 5>& values, int count)
{
    if (count <= 0) {
        return 0.0;
    }


    std::array<double, 5> tmp{};
    for (int i = 0; i < count; ++i) {
        tmp[i] = values[i];
    }


    std::sort(tmp.begin(), tmp.begin() + count);
    return tmp[count / 2];
}


void NavRuntime::updateFromImu(const M5ImuState& imu,
                               bool lidarFresh,
                               double steeringCmd,
                               double forwardCmd,
                               std::uint64_t nowMs)
{
    debug_ = NavDebugSnapshot{};
    debug_.timestampMs = nowMs;
    debug_.ax = imu.ax;
    debug_.ay = imu.ay;
    debug_.az = imu.az;
    debug_.gxRaw = imu.gx;
    debug_.gyRaw = imu.gy;
    debug_.gzRaw = imu.gz;
    debug_.steeringCmd = steeringCmd;
    debug_.forwardCmd = forwardCmd;


    state_.timestampMs = nowMs;
    state_.slamActive = false;
    state_.mapReady = false;
    state_.localized = false;
    state_.valid = false;
    state_.trackingLost = false;
    state_.xMeters = 0.0;
    state_.yMeters = 0.0;
    state_.linearVelocityMps = 0.0;


    if (!imu.enabled || !imu.hwOk) {
        reset();
        state_.timestampMs = nowMs;
        state_.isFresh = lidarFresh;
        state_.isStale = !state_.isFresh;
        return;
    }


    if (!imu.valid || !imu.isFresh) {
        state_.yawDeg = yawDegAccum_;
        state_.yawValid = gyroBiasReady_;
        state_.angularVelocityDegPs = yawRateFilteredDegPs_;
        state_.timestampMs = nowMs;
        state_.isFresh = lidarFresh;
        state_.isStale = !state_.isFresh;
        return;
    }


    debug_.imuUsable = true;


    const double gxRaw = imu.gx;
    const double gyRaw = imu.gy;
    const double gzRaw = imu.gz;


    const bool steeringActive = std::fabs(steeringCmd) >= kSteeringActiveThreshold;
    debug_.steeringActive = steeringActive;


    const bool movingForward = (forwardCmd >= kDriveActiveThreshold);
    const bool movingReverse = (forwardCmd <= -kDriveActiveThreshold);


    // projected yaw axis: gy dominant, gz secondary
    const double yawAxisRaw =
        kYawAxisSign * ((kYawAxisFromGy * gyRaw) + (kYawAxisFromGz * gzRaw));


    debug_.yawAxisRaw = yawAxisRaw;


    if (!yawIntegratorInitialized_) {
        yawIntegratorInitialized_ = true;
        lastYawUpdateMs_ = nowMs;
        yawDegAccum_ = 0.0;
        gyroBiasAccum_ = 0.0;
        gyroBiasSampleCount_ = 0;
        gyroBiasYawDegPs_ = 0.0;
        gyroBiasReady_ = false;


        state_.yawDeg = 0.0;
        state_.yawValid = false;
        state_.angularVelocityDegPs = 0.0;
        state_.isFresh = true;
        state_.isStale = false;
        return;
    }


    if (nowMs <= lastYawUpdateMs_) {
        state_.yawDeg = yawDegAccum_;
        state_.yawValid = gyroBiasReady_;
        state_.angularVelocityDegPs = yawRateFilteredDegPs_;
        state_.isFresh = true;
        state_.isStale = false;
        return;
    }


    const double dtSec = static_cast<double>(nowMs - lastYawUpdateMs_) / 1000.0;
    lastYawUpdateMs_ = nowMs;
    debug_.dtSec = dtSec;


    yawMedianWindow_[yawMedianIndex_] = yawAxisRaw;
    yawMedianIndex_ = (yawMedianIndex_ + 1) % static_cast<int>(yawMedianWindow_.size());
    if (yawMedianCount_ < static_cast<int>(yawMedianWindow_.size())) {
        ++yawMedianCount_;
    }
    const double yawAxisMedian = medianOfWindow(yawMedianWindow_, yawMedianCount_);


    debug_.yawAxisMedian = yawAxisMedian;
    debug_.gzMedian = yawAxisMedian;


    if (!gyroBiasReady_) {
        gyroBiasAccum_ += yawAxisMedian;
        ++gyroBiasSampleCount_;


        if (gyroBiasSampleCount_ >= kGyroBiasTargetSamples) {
            gyroBiasYawDegPs_ =
                gyroBiasAccum_ / static_cast<double>(gyroBiasSampleCount_);
            gyroBiasReady_ = true;


            std::cout << "[nav] yaw-axis gyro bias estimated: "
                      << gyroBiasYawDegPs_ << " deg/s\n";
        }


        debug_.gyroBiasYaw = gyroBiasYawDegPs_;
        debug_.gyroBiasZ = gyroBiasYawDegPs_;
        debug_.biasReady = gyroBiasReady_;


        state_.yawDeg = yawDegAccum_;
        state_.yawValid = false;
        state_.angularVelocityDegPs = 0.0;
        state_.isFresh = true;
        state_.isStale = false;
        return;
    }


    debug_.biasReady = true;
    debug_.gyroBiasYaw = gyroBiasYawDegPs_;
    debug_.gyroBiasZ = gyroBiasYawDegPs_;


    const double tau = 1.0 / (2.0 * kPi * kGyroCutoffHz);
    const double alpha = dtSec / (tau + dtSec);


    const double yawRateCorrected = yawAxisMedian - gyroBiasYawDegPs_;


    if (!rateFilterInitialized_) {
        yawRateFilteredDegPs_ = yawRateCorrected;
        gxFilteredDegPs_ = gxRaw;
        gyFilteredDegPs_ = gyRaw;
        gzFilteredDegPs_ = gzRaw;
        rateFilterInitialized_ = true;
    } else {
        yawRateFilteredDegPs_ += alpha * (yawRateCorrected - yawRateFilteredDegPs_);
        gxFilteredDegPs_ += alpha * (gxRaw - gxFilteredDegPs_);
        gyFilteredDegPs_ += alpha * (gyRaw - gyFilteredDegPs_);
        gzFilteredDegPs_ += alpha * (gzRaw - gzFilteredDegPs_);
    }


    if (std::fabs(yawRateFilteredDegPs_) < kYawDeadbandDegPs) {
        yawRateFilteredDegPs_ = 0.0;
    }


    debug_.gxFiltered = gxFilteredDegPs_;
    debug_.gyFiltered = gyFilteredDegPs_;
    debug_.gzFiltered = gzFilteredDegPs_;
    debug_.yawRateFiltered = yawRateFilteredDegPs_;


    const double accelNorm =
        std::sqrt(imu.ax * imu.ax + imu.ay * imu.ay + imu.az * imu.az);
    debug_.accelNorm = accelNorm;


    double jerk = 0.0;
    if (accelHistoryInitialized_ && dtSec > 1.0e-6) {
        const double dax = imu.ax - prevAx_;
        const double day = imu.ay - prevAy_;
        const double daz = imu.az - prevAz_;
        jerk = std::sqrt(dax * dax + day * day + daz * daz) / dtSec;
    }
    debug_.jerk = jerk;


    const double axDelta = accelHistoryInitialized_ ? std::fabs(imu.ax - prevAx_) : 0.0;
    const double ayDelta = accelHistoryInitialized_ ? std::fabs(imu.ay - prevAy_) : 0.0;
    const double azDelta = accelHistoryInitialized_ ? std::fabs(imu.az - prevAz_) : 0.0;


    prevAx_ = imu.ax;
    prevAy_ = imu.ay;
    prevAz_ = imu.az;
    accelHistoryInitialized_ = true;


    const bool gravityOk = accelNormLooksLikeGravity(accelNorm);
    const bool gyroQuiet =
        (std::fabs(gxFilteredDegPs_) <= kStationaryGyroXYDegPs) &&
        (std::fabs(gzFilteredDegPs_) <= kStationaryGyroXYDegPs) &&
        (std::fabs(yawRateFilteredDegPs_) <= kStationaryYawDegPs);


    const bool stationary = gravityOk && gyroQuiet;
    debug_.stationary = stationary;


    const bool shockDetected =
        (jerk > kShockJerkThreshold) ||
        (axDelta > kShockAccelDeltaThreshold) ||
        (ayDelta > kShockAccelDeltaThreshold) ||
        (azDelta > kShockAccelDeltaThreshold);
    debug_.shockDetected = shockDetected;


    const bool crossAxisTooHigh =
        (std::fabs(gxFilteredDegPs_) > kCrossAxisHighDegPs) &&
        (std::fabs(yawRateFilteredDegPs_) < kSmallYawRateDegPs);
    debug_.crossAxisTooHigh = crossAxisTooHigh;


    const bool straightMotionSuppressed =
        (!steeringActive) &&
        (std::fabs(yawRateFilteredDegPs_) < kFreeYawRateWithoutSteerDegPs);
    debug_.straightMotionSuppressed = straightMotionSuppressed;


    bool signMismatch = false;
    if (steeringActive &&
        (movingForward || movingReverse) &&
        (std::fabs(yawRateFilteredDegPs_) >= kSignCheckMinRateDegPs)) {


        int expectedTurnSign = signOf(steeringCmd);


        // ברוורס סימן ה-yaw הצפוי מתהפך
        if (movingReverse) {
            expectedTurnSign = -expectedTurnSign;
        }


        signMismatch = (signOf(yawRateFilteredDegPs_) != expectedTurnSign);
    }
    debug_.signMismatch = signMismatch;


    if (stationary) {
        const double biasAlpha = std::min(0.04, dtSec * 0.8);
        gyroBiasYawDegPs_ =
            (1.0 - biasAlpha) * gyroBiasYawDegPs_ + biasAlpha * yawAxisMedian;


        debug_.gyroBiasYaw = gyroBiasYawDegPs_;
        debug_.gyroBiasZ = gyroBiasYawDegPs_;


        if (std::fabs(yawDegAccum_) < kZeroSnapDeg &&
            std::fabs(yawRateFilteredDegPs_) < kZeroSnapRateDegPs) {
            yawDegAccum_ = 0.0;
            debug_.zeroSnapped = true;
        }
    }


    debug_.yawBefore = yawDegAccum_;


    const bool allowIntegration =
        !stationary &&
        !shockDetected &&
        !crossAxisTooHigh &&
        !straightMotionSuppressed &&
        !signMismatch;


    if (allowIntegration) {
        yawDegAccum_ += yawRateFilteredDegPs_ * dtSec;
        yawDegAccum_ = normalizeAngleDeg(yawDegAccum_);
        debug_.integrated = true;
    } else {
        debug_.integrated = false;
    }


    debug_.yawAfter = yawDegAccum_;


    state_.yawDeg = yawDegAccum_;
    state_.yawValid = true;
    state_.angularVelocityDegPs = yawRateFilteredDegPs_;
    state_.isFresh = true;
    state_.isStale = false;
}


const NavPoseState& NavRuntime::getState() const
{
    return state_;
}


const NavDebugSnapshot& NavRuntime::getDebug() const
{
    return debug_;
}




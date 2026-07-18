#include "navigation/LidarCorrectionHintsRuntime.h"


#include <algorithm>
#include <cmath>


LidarCorrectionHintsRuntime::LidarCorrectionHintsRuntime(const Config& cfg)
    : cfg_(cfg)
{
}


void LidarCorrectionHintsRuntime::reset()
{
    state_ = LidarCorrectionHintsState{};
}


const LidarCorrectionHintsState& LidarCorrectionHintsRuntime::getState() const
{
    return state_;
}


const LidarCorrectionHintsRuntime::Config& LidarCorrectionHintsRuntime::config() const
{
    return cfg_;
}


LidarCorrectionHintsRuntime::Config& LidarCorrectionHintsRuntime::config()
{
    return cfg_;
}


double LidarCorrectionHintsRuntime::clamp01(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}


double LidarCorrectionHintsRuntime::absMax(double a, double b)
{
    return std::max(std::fabs(a), std::fabs(b));
}


void LidarCorrectionHintsRuntime::update(const LidarPoseState& lidarPose,
                                         const NavPoseState& nav,
                                         std::uint64_t nowMs)
{
    state_ = LidarCorrectionHintsState{};
    state_.timestampMs = (lidarPose.timestampMs > 0) ? lidarPose.timestampMs : nowMs;


    if (!lidarPose.valid) {
        state_.isStale = true;
        return;
    }


    state_.valid = true;
    state_.isFresh = lidarPose.isFresh;
    state_.isStale = lidarPose.isStale;


    state_.centerErrorM = lidarPose.centerErrorM;
    state_.headingHintDeg = lidarPose.headingHintDeg;
    state_.frontClearanceM = lidarPose.frontClearanceM;
    state_.rearClearanceM = lidarPose.rearClearanceM;


    state_.forwardClearanceOk =
        (lidarPose.frontClearanceM < 0.0) ? false :
        (lidarPose.frontClearanceM >= cfg_.forwardClearanceMinM);


    state_.reverseClearanceOk =
        (lidarPose.rearClearanceM < 0.0) ? false :
        (lidarPose.rearClearanceM >= cfg_.reverseClearanceMinM);


    state_.corridorCentered =
        (std::fabs(lidarPose.centerErrorM) <= cfg_.centerDeadbandM) &&
        (std::fabs(lidarPose.headingHintDeg) <= cfg_.headingDeadbandDeg);


    const double combinedBias =
        lidarPose.centerErrorM +
        (lidarPose.headingHintDeg / 30.0);


    if (std::fabs(combinedBias) > cfg_.centerDeadbandM ||
        std::fabs(lidarPose.headingHintDeg) > cfg_.headingDeadbandDeg) {


        state_.steerCorrectionSuggested = true;
        state_.suggestedSteerSign = (combinedBias >= 0.0) ? +1 : -1;


        const double centerTerm =
            std::fabs(lidarPose.centerErrorM) / std::max(0.01, cfg_.steerStrengthCenterScaleM);
        const double headingTerm =
            std::fabs(lidarPose.headingHintDeg) / std::max(0.1, cfg_.steerStrengthHeadingScaleDeg);


        state_.suggestedSteerStrength = clamp01(std::max(centerTerm, headingTerm));
    }


    const bool frontTight =
        (lidarPose.frontClearanceM > 0.0) &&
        (lidarPose.frontClearanceM <= cfg_.reverseSuggestFrontM);


    const bool strongCenterBias =
        std::fabs(lidarPose.centerErrorM) >= cfg_.reverseSuggestCenterErrorM;


    const bool frontCornersBlocked =
        lidarPose.frontLeftBlocked && lidarPose.frontRightBlocked;


    if (frontTight && (strongCenterBias || frontCornersBlocked)) {
        state_.reverseCorrectionSuggested = true;
    }


    double leftRearSpace = -1.0;
    double rightRearSpace = -1.0;


    if (lidarPose.rearLeftDistanceM > 0.0) {
        leftRearSpace = lidarPose.rearLeftDistanceM;
    }
    if (lidarPose.rearRightDistanceM > 0.0) {
        rightRearSpace = lidarPose.rearRightDistanceM;
    }


    if (leftRearSpace > 0.0 || rightRearSpace > 0.0) {
        if (leftRearSpace > rightRearSpace) {
            state_.preferredReverseSide = -1;
            state_.reversePreferenceStrength =
                clamp01((leftRearSpace - std::max(0.0, rightRearSpace)) / 0.50);
        } else if (rightRearSpace > leftRearSpace) {
            state_.preferredReverseSide = +1;
            state_.reversePreferenceStrength =
                clamp01((rightRearSpace - std::max(0.0, leftRearSpace)) / 0.50);
        }
    }


    if (!nav.localAutoEnabled || !nav.localAutoActive) {
        if (!state_.reverseCorrectionSuggested) {
            state_.preferredReverseSide = 0;
            state_.reversePreferenceStrength = 0.0;
        }
    }
}




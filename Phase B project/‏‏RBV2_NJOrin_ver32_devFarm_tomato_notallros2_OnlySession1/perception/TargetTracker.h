#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perception/ObjectDetector.h"

class ServoCamera;

class TargetTracker {
public:
    struct TargetPoint {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Config {
        float minConfidence = 0.50f;

        bool personOnly = true;
        std::string personLabel = "person";

        float targetXRatio = 0.50f;
        float targetYRatio = 0.33f;

        int deadbandX = 25;
        int deadbandY = 20;

        int jitterThresholdPx = 12;

        float kpPan  = 0.025f;
        float kpTilt = 0.025f;

        int maxStepPan  = 5;
        int maxStepTilt = 5;

        bool invertPan = false;
        bool invertTilt = false;

        bool usePanLimits = false;
        bool useTiltLimits = false;
        int minPan = 0;
        int maxPan = 180;
        int minTilt = 0;
        int maxTilt = 180;

        bool autoCenterOnLost = false;

        int minUpdateIntervalMs = 0;
    };

public:
    TargetTracker(ServoCamera& camera);
    TargetTracker(ServoCamera& camera, const Config& cfg);

    void start();
    void stop();
    void toggle();
    bool isEnabled() const;

    void reset();
    void centerCamera();
    void manualNudgeCamera(int panDeltaDeg, int tiltDeltaDeg);
    void manualCenterCamera();
    std::pair<int,int> cameraAngles() const;

    const Config& config() const;
    Config& config();

    void update(const std::vector<ObjectDetector::Detection>& detections,
                const ObjectDetector::Frame& frame);

    void updateSingle(const ObjectDetector::Detection& detection,
                      const ObjectDetector::Frame& frame);

    bool updateFromDetector(const ObjectDetector& detector);

    void onTargetLost();

    bool hasTarget() const;
    std::optional<ObjectDetector::Detection> currentTarget() const;
    std::optional<TargetPoint> lastTargetPoint() const;

private:
    std::optional<ObjectDetector::Detection>
    selectBestTarget(const std::vector<ObjectDetector::Detection>& detections) const;

    bool isTrackable(const ObjectDetector::Detection& det) const;
    TargetPoint computeTargetPoint(const ObjectDetector::Detection& det) const;

    void trackPoint(const TargetPoint& pt, const ObjectDetector::Frame& frame);
    void applyPanTiltDelta(int panDelta, int tiltDelta);
    int clampi(int v, int lo, int hi) const;

private:
    ServoCamera& camera_;
    Config cfg_;

    bool enabled_ = false;
    bool havePrevTarget_ = false;
    bool haveCurrentTarget_ = false;

    TargetPoint prevTargetPoint_{};
    TargetPoint lastTargetPoint_{};

    std::optional<ObjectDetector::Detection> currentTarget_;
    uint64_t lastUpdateTimestampMs_ = 0;
};

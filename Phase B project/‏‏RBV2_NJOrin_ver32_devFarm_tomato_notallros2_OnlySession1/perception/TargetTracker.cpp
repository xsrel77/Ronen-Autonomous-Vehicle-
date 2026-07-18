#include "perception/TargetTracker.h"
#include "hardware/ServoCamera.h"

#include <cmath>

TargetTracker::TargetTracker(ServoCamera& camera)
    : camera_(camera), cfg_{} {}

TargetTracker::TargetTracker(ServoCamera& camera, const Config& cfg)
    : camera_(camera), cfg_(cfg) {}

void TargetTracker::start() {
    enabled_ = true;
}

void TargetTracker::stop() {
    enabled_ = false;
    haveCurrentTarget_ = false;
    currentTarget_.reset();
}

void TargetTracker::toggle() {
    if (enabled_) stop();
    else start();
}

bool TargetTracker::isEnabled() const {
    return enabled_;
}

void TargetTracker::reset() {
    havePrevTarget_ = false;
    haveCurrentTarget_ = false;
    currentTarget_.reset();
    lastUpdateTimestampMs_ = 0;
}

void TargetTracker::centerCamera() {
    camera_.center();
}

void TargetTracker::manualNudgeCamera(int panDeltaDeg, int tiltDeltaDeg) {
    applyPanTiltDelta(panDeltaDeg, tiltDeltaDeg);
}

void TargetTracker::manualCenterCamera() {
    camera_.center();
}

std::pair<int,int> TargetTracker::cameraAngles() const {
    return camera_.getAngles();
}

const TargetTracker::Config& TargetTracker::config() const {
    return cfg_;
}

TargetTracker::Config& TargetTracker::config() {
    return cfg_;
}

bool TargetTracker::isTrackable(const ObjectDetector::Detection& det) const {
    if (!det.valid) return false;
    if (det.confidence < cfg_.minConfidence) return false;
    if (det.w <= 0.0f || det.h <= 0.0f) return false;

    if (cfg_.personOnly) {
        if (det.label != cfg_.personLabel) return false;
    }

    return true;
}

std::optional<ObjectDetector::Detection>
TargetTracker::selectBestTarget(const std::vector<ObjectDetector::Detection>& detections) const {
    float bestScore = -1.0f;
    std::optional<ObjectDetector::Detection> best;

    for (const auto& det : detections) {
        if (!isTrackable(det)) continue;

        if (det.confidence > bestScore) {
            bestScore = det.confidence;
            best = det;
        }
    }

    return best;
}

TargetTracker::TargetPoint
TargetTracker::computeTargetPoint(const ObjectDetector::Detection& det) const {
    TargetPoint pt;
    pt.x = det.x + det.w * cfg_.targetXRatio;
    pt.y = det.y + det.h * cfg_.targetYRatio;
    return pt;
}

int TargetTracker::clampi(int v, int lo, int hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void TargetTracker::applyPanTiltDelta(int panDelta, int tiltDelta) {
    if (panDelta == 0 && tiltDelta == 0) {
        return;
    }

    auto ang = camera_.getAngles();

    int pan  = ang.first  + panDelta;
    int tilt = ang.second + tiltDelta;

    if (cfg_.usePanLimits) {
        pan = clampi(pan, cfg_.minPan, cfg_.maxPan);
    } else {
        pan = clampi(pan, 0, 180);
    }

    if (cfg_.useTiltLimits) {
        tilt = clampi(tilt, cfg_.minTilt, cfg_.maxTilt);
    } else {
        tilt = clampi(tilt, 0, 180);
    }

    camera_.setAngles(pan, tilt);
}

void TargetTracker::trackPoint(const TargetPoint& pt, const ObjectDetector::Frame& frame) {
    if (frame.width <= 0 || frame.height <= 0) return;

    if (cfg_.minUpdateIntervalMs > 0) {
        if (lastUpdateTimestampMs_ != 0 &&
            frame.timestampMs > lastUpdateTimestampMs_ &&
            (frame.timestampMs - lastUpdateTimestampMs_) < static_cast<uint64_t>(cfg_.minUpdateIntervalMs)) {
            return;
        }
    }

    if (havePrevTarget_) {
        const float dxPrev = std::fabs(pt.x - prevTargetPoint_.x);
        const float dyPrev = std::fabs(pt.y - prevTargetPoint_.y);
        if (dxPrev < cfg_.jitterThresholdPx && dyPrev < cfg_.jitterThresholdPx) {
            return;
        }
    }

    const float cx = frame.width  * 0.5f;
    const float cy = frame.height * 0.5f;

    const float errX = pt.x - cx;
    const float errY = pt.y - cy;

    int panDelta = 0;
    int tiltDelta = 0;

    if (std::fabs(errX) > static_cast<float>(cfg_.deadbandX)) {
        panDelta = static_cast<int>(std::round(errX * cfg_.kpPan));
        panDelta = clampi(panDelta, -cfg_.maxStepPan, cfg_.maxStepPan);
        if (cfg_.invertPan) panDelta = -panDelta;
    }

    if (std::fabs(errY) > static_cast<float>(cfg_.deadbandY)) {
        tiltDelta = static_cast<int>(std::round(errY * cfg_.kpTilt));
        tiltDelta = clampi(tiltDelta, -cfg_.maxStepTilt, cfg_.maxStepTilt);
        if (cfg_.invertTilt) tiltDelta = -tiltDelta;
    }

    applyPanTiltDelta(panDelta, tiltDelta);

    prevTargetPoint_ = pt;
    lastTargetPoint_ = pt;
    havePrevTarget_ = true;
    lastUpdateTimestampMs_ = frame.timestampMs;
}

void TargetTracker::update(const std::vector<ObjectDetector::Detection>& detections,
                           const ObjectDetector::Frame& frame) {
    if (!enabled_) return;

    auto best = selectBestTarget(detections);
    if (!best.has_value()) {
        onTargetLost();
        return;
    }

    updateSingle(*best, frame);
}

void TargetTracker::updateSingle(const ObjectDetector::Detection& detection,
                                 const ObjectDetector::Frame& frame) {
    if (!enabled_) return;
    if (!isTrackable(detection)) {
        onTargetLost();
        return;
    }

    currentTarget_ = detection;
    haveCurrentTarget_ = true;

    const TargetPoint pt = computeTargetPoint(detection);
    trackPoint(pt, frame);
}

bool TargetTracker::updateFromDetector(const ObjectDetector& detector) {
    if (!enabled_) return false;

    ObjectDetector::Snapshot snap;
    if (!detector.getLatestSnapshot(snap)) {
        onTargetLost();
        return false;
    }

    update(snap.detections, snap.frame);
    return true;
}

void TargetTracker::onTargetLost() {
    haveCurrentTarget_ = false;
    currentTarget_.reset();

    if (cfg_.autoCenterOnLost) {
        camera_.center();
    }
}

bool TargetTracker::hasTarget() const {
    return haveCurrentTarget_;
}

std::optional<ObjectDetector::Detection> TargetTracker::currentTarget() const {
    return currentTarget_;
}

std::optional<TargetTracker::TargetPoint> TargetTracker::lastTargetPoint() const {
    if (!havePrevTarget_) return std::nullopt;
    return lastTargetPoint_;
}

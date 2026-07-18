#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include "core/PerceptionTypes.h"

/*
 * Lightweight toClient video/image recorder for RBV2_NJOrin_ver24.
 *
 * This class is intentionally separate from dev_farm/DevFarmVideoRecorder.
 * The dev_farm recorder is a different debug mode. This recorder is only for
 * TO_CLIENT_JSON sessions and is designed to keep files small enough for the
 * Next.js client export.
 */
class ToClientVideoRecorder
{
public:
    struct Config
    {
        double targetFps = 10.0;
        int maxWidth = 1280;
        int maxHeight = 720;
        int h264BitrateKbps = 2500;
        std::uint64_t maxBytes = 1024ULL * 1024ULL * 1024ULL;
        int jpegQuality = 82;
    };

    struct Status
    {
        bool active = false;
        bool writerOpen = false;
        bool usingGStreamer = false;
        bool maxSizeReached = false;
        std::string path;
        std::string backend;
        std::uint64_t framesWritten = 0;
        std::uint64_t bytesWritten = 0;
        std::uint64_t startedAtMs = 0;
        std::uint64_t stoppedAtMs = 0;
        std::string stopReason;
    };

    ToClientVideoRecorder() = default;
    ~ToClientVideoRecorder();

    bool start(const std::string& mp4Path,
               std::uint64_t startedAtMs,
               const Config& cfg);
    void stop(const std::string& reason, std::uint64_t stoppedAtMs);

    bool isActive() const;
    Status getStatus() const;

    bool updateFromSnapshot(const DetectionSnapshot& snapshot,
                            std::uint64_t nowMs);

    bool writeAnnotatedJpeg(const std::string& jpgPath,
                            const DetectionSnapshot& snapshot,
                            int jpegQuality = 82) const;
    bool writeRawJpeg(const std::string& jpgPath,
                      const DetectionSnapshot& snapshot,
                      int jpegQuality = 82) const;

    static bool snapshotToBgr(const DetectionSnapshot& snapshot, cv::Mat& outBgr);
    static cv::Mat makeAnnotatedFrame(const cv::Mat& bgr,
                                      const std::vector<Detection>& detections);

private:
    bool openWriterIfNeeded(const cv::Size& sourceSize);
    static cv::Size fitSize(const cv::Size& src, int maxW, int maxH);
    static void drawDetections(cv::Mat& img,
                               const std::vector<Detection>& detections);
    static std::string escapeGstPath(const std::string& path);
    void refreshBytesWritten();

private:
    Config cfg_{};
    Status st_{};
    cv::VideoWriter writer_{};
    cv::Size writerSize_{};
    std::uint64_t lastWriteMs_ = 0;
};

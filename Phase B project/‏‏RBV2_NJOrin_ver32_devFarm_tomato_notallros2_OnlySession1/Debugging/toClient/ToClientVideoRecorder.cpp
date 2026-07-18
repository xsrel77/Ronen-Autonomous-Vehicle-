#include "Debugging/toClient/ToClientVideoRecorder.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

ToClientVideoRecorder::~ToClientVideoRecorder()
{
    stop("destructor", 0);
}

bool ToClientVideoRecorder::start(const std::string& mp4Path,
                                  std::uint64_t startedAtMs,
                                  const Config& cfg)
{
    stop("restart", startedAtMs);

    cfg_ = cfg;
    st_ = Status{};
    st_.active = true;
    st_.path = mp4Path;
    st_.startedAtMs = startedAtMs;
    st_.backend = "pending_first_frame";
    lastWriteMs_ = 0;
    writerSize_ = cv::Size{};

    try {
        fs::create_directories(fs::path(mp4Path).parent_path());
    } catch (const std::exception& e) {
        std::cerr << "[toClient][video] failed to create video directory: " << e.what() << "\n";
        st_.active = false;
        st_.stopReason = "mkdir_failed";
        return false;
    }

    return true;
}

void ToClientVideoRecorder::stop(const std::string& reason, std::uint64_t stoppedAtMs)
{
    if (writer_.isOpened()) {
        writer_.release();
    }

    if (st_.active || st_.writerOpen) {
        refreshBytesWritten();
        st_.stoppedAtMs = stoppedAtMs;
        st_.stopReason = reason;
    }

    st_.active = false;
    st_.writerOpen = false;
}

bool ToClientVideoRecorder::isActive() const
{
    return st_.active;
}

ToClientVideoRecorder::Status ToClientVideoRecorder::getStatus() const
{
    return st_;
}

cv::Size ToClientVideoRecorder::fitSize(const cv::Size& src, int maxW, int maxH)
{
    if (src.width <= 0 || src.height <= 0) {
        return cv::Size{};
    }

    double scale = 1.0;
    if (maxW > 0 && src.width > maxW) {
        scale = std::min(scale, static_cast<double>(maxW) / static_cast<double>(src.width));
    }
    if (maxH > 0 && src.height > maxH) {
        scale = std::min(scale, static_cast<double>(maxH) / static_cast<double>(src.height));
    }

    int w = static_cast<int>(std::round(src.width * scale));
    int h = static_cast<int>(std::round(src.height * scale));

    // Most encoders prefer even dimensions.
    if (w % 2 != 0) --w;
    if (h % 2 != 0) --h;

    w = std::max(w, 2);
    h = std::max(h, 2);
    return cv::Size(w, h);
}

std::string ToClientVideoRecorder::escapeGstPath(const std::string& path)
{
    std::string out;
    out.reserve(path.size() + 8);
    for (char ch : path) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

bool ToClientVideoRecorder::openWriterIfNeeded(const cv::Size& sourceSize)
{
    if (!st_.active) {
        return false;
    }
    if (writer_.isOpened()) {
        return true;
    }

    writerSize_ = fitSize(sourceSize, cfg_.maxWidth, cfg_.maxHeight);
    if (writerSize_.width <= 0 || writerSize_.height <= 0) {
        st_.stopReason = "invalid_frame_size";
        return false;
    }

    // Preferred path on Jetson: hardware H.264 encoder through GStreamer.
    // If OpenCV was built without GStreamer, or if the pipeline is unavailable,
    // fallback to mp4v. The fallback is still much smaller than MJPG/AVI because
    // we downsample FPS and cap resolution.
    const int bitrateBps = std::max(300, cfg_.h264BitrateKbps) * 1000;
    std::ostringstream gst;
    gst << "appsrc ! videoconvert ! video/x-raw,format=I420 ! "
        << "nvvidconv ! video/x-raw(memory:NVMM),format=NV12 ! "
        << "nvv4l2h264enc bitrate=" << bitrateBps
        << " insert-sps-pps=true iframeinterval=30 ! "
        << "h264parse ! qtmux ! filesink location=\"" << escapeGstPath(st_.path) << "\"";

    if (writer_.open(gst.str(), cv::CAP_GSTREAMER, 0, cfg_.targetFps, writerSize_, true)) {
        st_.writerOpen = true;
        st_.usingGStreamer = true;
        st_.backend = "gstreamer_nvv4l2h264enc";
        std::cout << "[toClient][video] recording H.264: " << st_.path
                  << " fps=" << cfg_.targetFps
                  << " size=" << writerSize_.width << "x" << writerSize_.height
                  << " bitrate=" << cfg_.h264BitrateKbps << "kbps\n";
        return true;
    }

    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    if (writer_.open(st_.path, fourcc, cfg_.targetFps, writerSize_, true)) {
        st_.writerOpen = true;
        st_.usingGStreamer = false;
        st_.backend = "opencv_mp4v_fallback";
        std::cout << "[toClient][video] GStreamer H.264 unavailable, fallback MP4V: "
                  << st_.path << "\n";
        return true;
    }

    std::cerr << "[toClient][video] failed to open any video writer: " << st_.path << "\n";
    st_.active = false;
    st_.writerOpen = false;
    st_.stopReason = "open_writer_failed";
    return false;
}

bool ToClientVideoRecorder::snapshotToBgr(const DetectionSnapshot& snapshot, cv::Mat& outBgr)
{
    outBgr.release();

    if (!snapshot.valid || snapshot.frameBytes.empty() ||
        snapshot.frame.width <= 0 || snapshot.frame.height <= 0 ||
        snapshot.frame.channels <= 0 || snapshot.frame.strideBytes <= 0) {
        return false;
    }

    const int type = (snapshot.frame.channels == 1) ? CV_8UC1 :
                     (snapshot.frame.channels == 3) ? CV_8UC3 :
                     (snapshot.frame.channels == 4) ? CV_8UC4 : -1;
    if (type < 0) {
        return false;
    }

    const std::size_t required = static_cast<std::size_t>(snapshot.frame.height) *
                                 static_cast<std::size_t>(snapshot.frame.strideBytes);
    if (snapshot.frameBytes.size() < required) {
        return false;
    }

    cv::Mat wrapped(snapshot.frame.height,
                    snapshot.frame.width,
                    type,
                    const_cast<std::uint8_t*>(snapshot.frameBytes.data()),
                    static_cast<std::size_t>(snapshot.frame.strideBytes));

    if (snapshot.frame.channels == 1) {
        cv::cvtColor(wrapped, outBgr, cv::COLOR_GRAY2BGR);
    } else if (snapshot.frame.channels == 4) {
        cv::cvtColor(wrapped, outBgr, cv::COLOR_BGRA2BGR);
    } else {
        outBgr = wrapped.clone();
    }

    return !outBgr.empty();
}

static cv::Scalar colorForDetection(const Detection& d)
{
    if (d.heuristic || d.sourceType == "heuristic" || d.policyStatus == "heuristic") {
        return cv::Scalar(0, 255, 255);      // Ver32 Heuristics: yellow in BGR
    }
    if (d.policyStatus == "noise") {
        return cv::Scalar(145, 145, 145);    // Ver32 Noise: gray
    }
    if (!d.valid || d.weak) {
        return cv::Scalar(255, 220, 0);      // Weak: cyan-ish in BGR
    }

    switch (d.classId) {
        case 0: return cv::Scalar(180, 0, 180);   // eripe bunch / ripe bunch - purple
        case 1: return cv::Scalar(0, 0, 255);     // ripe - red
        case 2: return cv::Scalar(0, 180, 0);     // unripe - green
        case 3: return cv::Scalar(0, 140, 255);   // unripe bunch - orange
        default: return cv::Scalar(255, 255, 255);
    }
}

void ToClientVideoRecorder::drawDetections(cv::Mat& img,
                                            const std::vector<Detection>& detections)
{
    for (const auto& d : detections) {
        if (d.displaySuppressed) {
            continue;
        }
        if (d.w <= 1.0f || d.h <= 1.0f) {
            continue;
        }

        const cv::Scalar color = colorForDetection(d);
        const bool isHeuristic = d.heuristic || d.sourceType == "heuristic" || d.policyStatus == "heuristic";
        const int thickness = isHeuristic ? 3 : ((!d.valid || d.weak) ? 1 : ((d.classId == 0 || d.classId == 3) ? 3 : 2));

        const int x = std::max(0, static_cast<int>(std::round(d.x)));
        const int y = std::max(0, static_cast<int>(std::round(d.y)));
        const int w = std::max(1, static_cast<int>(std::round(d.w)));
        const int h = std::max(1, static_cast<int>(std::round(d.h)));

        cv::Rect r(x, y, w, h);
        r &= cv::Rect(0, 0, img.cols, img.rows);
        if (r.width <= 0 || r.height <= 0) {
            continue;
        }

        cv::rectangle(img, r, color, thickness);

        std::ostringstream label;
        label.setf(std::ios::fixed);
        label.precision(2);
        if (isHeuristic) {
            label << (d.label.empty() ? std::string("HEURISTIC bunch") : d.label);
            if (d.childCount > 0) label << " c" << d.childCount;
            if (d.anchorBunchConfidence > 0.0f) label << " a" << d.anchorBunchConfidence;
        } else {
            if (d.policyStatus == "review") label << "REVIEW ";
            if (d.policyStatus == "color_corrected") label << "CORR ";
            if (d.policyStatus == "noise") label << "NOISE ";
            label << (d.label.empty() ? std::string("class_") + std::to_string(d.classId) : d.label)
                  << " " << d.confidence;
        }
        if (d.trackStableBest && d.currentConfidence > 0.0f && std::fabs(d.currentConfidence - d.confidence) > 0.01f) {
            label << " best";
        }
        if (d.weak || !d.valid) {
            label << " weak";
        }
        if (d.roiPass) {
            label << " ROI";
        }
        if (d.trackId >= 0) {
            label << " #" << d.trackId;
        }
        if (d.trackAge > 0) {
            label << " hold";
        }

        int baseLine = 0;
        const cv::Size textSize = cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, 0.48, 1, &baseLine);
        const int textX = r.x;
        const int textY = std::max(textSize.height + 3, r.y - 4);
        cv::Rect bg(textX, textY - textSize.height - 3, textSize.width + 6, textSize.height + baseLine + 5);
        bg &= cv::Rect(0, 0, img.cols, img.rows);
        if (bg.width > 0 && bg.height > 0) {
            cv::rectangle(img, bg, color, cv::FILLED);
        }
        cv::putText(img, label.str(), cv::Point(textX + 3, textY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.48, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    }
}

cv::Mat ToClientVideoRecorder::makeAnnotatedFrame(const cv::Mat& bgr,
                                                  const std::vector<Detection>& detections)
{
    cv::Mat annotated = bgr.clone();
    drawDetections(annotated, detections);
    return annotated;
}

bool ToClientVideoRecorder::updateFromSnapshot(const DetectionSnapshot& snapshot,
                                               std::uint64_t nowMs)
{
    if (!st_.active) {
        return false;
    }

    const std::uint64_t framePeriodMs = cfg_.targetFps > 0.0
        ? static_cast<std::uint64_t>(std::round(1000.0 / cfg_.targetFps))
        : 100ULL;

    if (lastWriteMs_ != 0 && nowMs > lastWriteMs_ && (nowMs - lastWriteMs_) < framePeriodMs) {
        return true;
    }

    cv::Mat bgr;
    if (!snapshotToBgr(snapshot, bgr)) {
        return false;
    }

    if (!openWriterIfNeeded(bgr.size())) {
        return false;
    }

    cv::Mat annotated = makeAnnotatedFrame(bgr, snapshot.detections);
    if (annotated.size() != writerSize_) {
        cv::resize(annotated, annotated, writerSize_);
    }

    writer_.write(annotated);
    ++st_.framesWritten;
    lastWriteMs_ = nowMs;

    if ((st_.framesWritten % 10) == 0) {
        refreshBytesWritten();
        if (cfg_.maxBytes > 0 && st_.bytesWritten >= cfg_.maxBytes) {
            st_.maxSizeReached = true;
            stop("max_size_reached", nowMs);
        }
    }

    return true;
}

bool ToClientVideoRecorder::writeAnnotatedJpeg(const std::string& jpgPath,
                                               const DetectionSnapshot& snapshot,
                                               int jpegQuality) const
{
    cv::Mat bgr;
    if (!snapshotToBgr(snapshot, bgr)) {
        return false;
    }

    cv::Mat annotated = makeAnnotatedFrame(bgr, snapshot.detections);

    try {
        fs::create_directories(fs::path(jpgPath).parent_path());
    } catch (const std::exception& e) {
        std::cerr << "[toClient][image] mkdir failed: " << e.what() << "\n";
        return false;
    }

    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(std::max(35, std::min(100, jpegQuality)));

    return cv::imwrite(jpgPath, annotated, params);
}

bool ToClientVideoRecorder::writeRawJpeg(const std::string& jpgPath,
                                         const DetectionSnapshot& snapshot,
                                         int jpegQuality) const
{
    cv::Mat bgr;
    if (!snapshotToBgr(snapshot, bgr)) {
        return false;
    }

    try {
        fs::create_directories(fs::path(jpgPath).parent_path());
    } catch (const std::exception& e) {
        std::cerr << "[toClient][image] raw mkdir failed: " << e.what() << "\n";
        return false;
    }

    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(std::max(35, std::min(100, jpegQuality)));
    return cv::imwrite(jpgPath, bgr, params);
}

void ToClientVideoRecorder::refreshBytesWritten()
{
    if (st_.path.empty()) {
        return;
    }

    try {
        if (fs::exists(st_.path)) {
            st_.bytesWritten = static_cast<std::uint64_t>(fs::file_size(st_.path));
        }
    } catch (...) {
        // Keep previous value. This is only status/debug metadata.
    }
}

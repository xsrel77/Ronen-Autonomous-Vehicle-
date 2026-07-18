#include "dev_farm/DevFarmVideoRecorder.h"


#include <filesystem>
#include <iostream>


namespace fs = std::filesystem;


DevFarmVideoRecorder::~DevFarmVideoRecorder()
{
    stop("destructor", 0);
}


bool DevFarmVideoRecorder::start(const std::string& outputPath,
                                 std::uint64_t maxBytes,
                                 double fps,
                                 std::uint64_t nowMs)
{
    stop("restart", nowMs);


    if (outputPath.empty()) {
        std::cerr << "[devFarm][video] empty output path\n";
        return false;
    }


    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    if (ec) {
        std::cerr << "[devFarm][video] failed to create folder: "
                  << fs::path(outputPath).parent_path().string()
                  << " error=" << ec.message() << "\n";
        return false;
    }


    status_ = Status{};
    status_.recording = true;
    status_.writerOpen = false;
    status_.outputPath = outputPath;
    status_.maxBytes = maxBytes;
    status_.fps = (fps > 1.0) ? fps : 30.0;
    status_.startedAtMs = nowMs;
    status_.stopReason.clear();


    std::cout << "[devFarm][video] recording armed: " << status_.outputPath
              << " maxBytes=" << status_.maxBytes << "\n";


    return true;
}


void DevFarmVideoRecorder::stop(const std::string& reason, std::uint64_t nowMs)
{
    if (writer_.isOpened()) {
        writer_.release();
    }


    if (status_.recording || status_.writerOpen) {
        refreshFileSize();
        std::cout << "[devFarm][video] recording stopped. reason=" << reason
                  << " frames=" << status_.framesWritten
                  << " bytes=" << status_.currentBytes
                  << " file=" << status_.outputPath << "\n";
    }


    status_.recording = false;
    status_.writerOpen = false;
    status_.stopReason = reason;
    status_.stoppedAtMs = nowMs;
}


bool DevFarmVideoRecorder::isRecording() const
{
    return status_.recording;
}


DevFarmVideoRecorder::Status DevFarmVideoRecorder::getStatus() const
{
    return status_;
}


bool DevFarmVideoRecorder::openWriterFromSnapshot(const ObjectDetector::Snapshot& snapshot)
{
    if (!status_.recording) {
        return false;
    }


    if (snapshot.frame.width <= 0 ||
        snapshot.frame.height <= 0 ||
        snapshot.frame.strideBytes <= 0) {
        return false;
    }


    if (snapshot.frame.channels != 3) {
        std::cerr << "[devFarm][video] unsupported frame channels="
                  << snapshot.frame.channels << " expected 3\n";
        stop("unsupported_frame_format", status_.startedAtMs);
        return false;
    }


    const int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    const cv::Size size(snapshot.frame.width, snapshot.frame.height);


    if (!writer_.open(status_.outputPath, fourcc, status_.fps, size, true)) {
        std::cerr << "[devFarm][video] failed to open VideoWriter: "
                  << status_.outputPath << "\n";
        stop("writer_open_failed", status_.startedAtMs);
        return false;
    }


    status_.writerOpen = true;
    status_.width = snapshot.frame.width;
    status_.height = snapshot.frame.height;


    std::cout << "[devFarm][video] VideoWriter opened "
              << status_.width << "x" << status_.height
              << " fps=" << status_.fps << "\n";


    return true;
}


void DevFarmVideoRecorder::refreshFileSize()
{
    if (status_.outputPath.empty()) {
        status_.currentBytes = 0;
        return;
    }


    std::error_code ec;
    const auto bytes = fs::file_size(status_.outputPath, ec);
    if (!ec) {
        status_.currentBytes = static_cast<std::uint64_t>(bytes);
    }
}


void DevFarmVideoRecorder::updateFromSnapshot(const ObjectDetector::Snapshot& snapshot,
                                              std::uint64_t nowMs)
{
    if (!status_.recording) {
        return;
    }


    if (!snapshot.valid || snapshot.frameBytes.empty() || snapshot.frame.data == nullptr) {
        return;
    }


    if (snapshot.frame.timestampMs > 0 &&
        snapshot.frame.timestampMs == status_.lastFrameTimestampMs) {
        return;
    }


    if (!writer_.isOpened()) {
        if (!openWriterFromSnapshot(snapshot)) {
            return;
        }
    }


    cv::Mat frame(snapshot.frame.height,
                  snapshot.frame.width,
                  CV_8UC3,
                  const_cast<std::uint8_t*>(snapshot.frame.data),
                  static_cast<size_t>(snapshot.frame.strideBytes));


    writer_.write(frame);


    status_.framesWritten += 1;
    status_.lastFrameTimestampMs = snapshot.frame.timestampMs;


    refreshFileSize();


    if (status_.maxBytes > 0 && status_.currentBytes >= status_.maxBytes) {
        status_.maxSizeReached = true;
        stop("max_size_reached", nowMs);
    }
}






#pragma once


#include "perception/ObjectDetector.h"


#include <cstdint>
#include <string>


#include <opencv2/opencv.hpp>


class DevFarmVideoRecorder {
public:
    struct Status {
        bool recording = false;
        bool writerOpen = false;
        bool maxSizeReached = false;


        std::string outputPath;
        std::string stopReason;


        std::uint64_t maxBytes = 0;
        std::uint64_t currentBytes = 0;
        std::uint64_t framesWritten = 0;
        std::uint64_t lastFrameTimestampMs = 0;
        std::uint64_t startedAtMs = 0;
        std::uint64_t stoppedAtMs = 0;


        int width = 0;
        int height = 0;
        double fps = 0.0;
    };


public:
    DevFarmVideoRecorder() = default;
    ~DevFarmVideoRecorder();


    bool start(const std::string& outputPath,
               std::uint64_t maxBytes,
               double fps,
               std::uint64_t nowMs);


    void stop(const std::string& reason, std::uint64_t nowMs);


    bool isRecording() const;
    Status getStatus() const;


    void updateFromSnapshot(const ObjectDetector::Snapshot& snapshot,
                            std::uint64_t nowMs);


private:
    bool openWriterFromSnapshot(const ObjectDetector::Snapshot& snapshot);
    void refreshFileSize();


private:
    Status status_{};
    cv::VideoWriter writer_{};
};






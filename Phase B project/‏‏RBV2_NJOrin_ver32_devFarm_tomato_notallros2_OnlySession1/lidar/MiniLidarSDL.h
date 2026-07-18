#pragma once

#include "core/LidarTypes.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


class MiniLidarSDL {
public:
    using LidarPoint = ::LidarPoint;
    using Snapshot = ::LidarSnapshot;


    explicit MiniLidarSDL(std::string port = "/dev/ttyUSB0");
    ~MiniLidarSDL();


    MiniLidarSDL(const MiniLidarSDL&) = delete;
    MiniLidarSDL& operator=(const MiniLidarSDL&) = delete;


    bool start();
    void stop();
    void toggle();
    bool isRunning() const;


    void getLatestPoints(std::vector<LidarPoint>& out) const;
    double maxRangeMeters() const;


    std::string getLastError() const;


private:
    void readerLoop();


    int openSerial(const char* port);
    bool writeAll(int fd, const unsigned char* data, size_t n);
    bool readExact(int fd, unsigned char* buf, size_t n);
    bool findHeader(int fd);


    void setLastError(const std::string& err);
    void cleanupFinishedWorkerIfNeeded();


private:
    std::string port_;


    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};


    std::thread worker_;
    mutable std::mutex stateMutex_;
    mutable std::mutex pointsMutex_;
    mutable std::mutex errorMutex_;


    int fd_ = -1;


    std::vector<LidarPoint> points_;
    std::string lastError_;
};




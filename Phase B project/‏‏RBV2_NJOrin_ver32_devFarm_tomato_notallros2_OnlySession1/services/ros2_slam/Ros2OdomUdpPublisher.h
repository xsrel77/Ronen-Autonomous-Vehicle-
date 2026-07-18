#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "core/SystemState.h"

class Ros2OdomUdpPublisher
{
public:
    struct Config
    {
        std::string host = "127.0.0.1";
        int port = 29129;
        std::uint64_t minPublishPeriodMs = 50; // 20 Hz max from caller
        std::uint64_t keepAlivePeriodMs = 50;  // keep TF alive during blocking map save

        // Fix3: for debugging and for SLAM stability, always publish the latest
        // estimated pose to ROS2 even if OdomRuntime marked it not-fresh for a tick.
        // The bridge will still receive the original input flags in the payload.
        bool forceRosValidFreshFlags = true;
        bool consoleDebug = true;
        std::uint64_t debugPrintPeriodMs = 1000;
    };

    struct Stats
    {
        bool running = false;
        bool lastPublishOk = false;
        bool lastInputValid = false;
        bool lastInputFresh = false;

        std::uint64_t publishCalls = 0;
        std::uint64_t packetsSent = 0;
        std::uint64_t keepAlivePacketsSent = 0;
        std::uint64_t skippedThrottle = 0;
        std::uint64_t sendErrors = 0;
        std::uint64_t lastPublishCallMs = 0;
        std::uint64_t lastSentMs = 0;
        std::uint64_t lastDebugPrintMs = 0;

        double lastX = 0.0;
        double lastY = 0.0;
        double lastYawDeg = 0.0;
        double lastLinearMps = 0.0;
        double lastAngularDegPs = 0.0;

        bool sessionOriginSet = false;
        double sessionOriginX = 0.0;
        double sessionOriginY = 0.0;
        double sessionDistanceM = 0.0;
    };

public:
    Ros2OdomUdpPublisher() = default;
    explicit Ros2OdomUdpPublisher(const Config& cfg);
    ~Ros2OdomUdpPublisher();

    Ros2OdomUdpPublisher(const Ros2OdomUdpPublisher&) = delete;
    Ros2OdomUdpPublisher& operator=(const Ros2OdomUdpPublisher&) = delete;

    bool start();
    void stop();
    bool isRunning() const;

    bool publish(const OdomState& odom, std::uint64_t nowMs);

    Stats stats() const;
    const std::string& lastError() const;
    const Config& config() const;
    Config& config();

private:
    bool sendPayloadLocked(const std::string& payload, bool keepAlive, std::uint64_t nowMs);
    void maybePrintDebugLocked(std::uint64_t nowMs, const OdomState& odom, bool sentNow);
    void keepAliveLoop();
    static std::uint64_t wallClockMs();

private:
    Config cfg_{};
    int socketFd_ = -1;
    std::string lastError_{};
    std::uint64_t lastPublishMs_ = 0;

    mutable std::mutex mutex_{};
    std::string lastPayload_{};
    std::thread keepAliveThread_{};
    std::atomic<bool> keepAliveStop_{false};
    Stats stats_{};
};

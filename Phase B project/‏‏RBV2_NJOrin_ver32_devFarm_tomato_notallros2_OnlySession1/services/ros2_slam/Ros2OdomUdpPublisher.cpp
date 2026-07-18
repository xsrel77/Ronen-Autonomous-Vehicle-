#include "services/ros2_slam/Ros2OdomUdpPublisher.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

Ros2OdomUdpPublisher::Ros2OdomUdpPublisher(const Config& cfg)
    : cfg_(cfg)
{
}

Ros2OdomUdpPublisher::~Ros2OdomUdpPublisher()
{
    stop();
}

std::uint64_t Ros2OdomUdpPublisher::wallClockMs()
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool Ros2OdomUdpPublisher::start()
{
    if (socketFd_ >= 0) {
        return true;
    }

    socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        lastError_ = std::string("socket failed: ") + std::strerror(errno);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastPayload_.clear();
        lastPublishMs_ = 0;
        stats_ = Stats{};
        stats_.running = true;
    }

    keepAliveStop_.store(false);
    keepAliveThread_ = std::thread(&Ros2OdomUdpPublisher::keepAliveLoop, this);

    lastError_.clear();
    if (cfg_.consoleDebug) {
        std::cout << "[ros2_odom_udp] publisher START host=" << cfg_.host
                  << " port=" << cfg_.port
                  << " force_valid_fresh=" << (cfg_.forceRosValidFreshFlags ? "YES" : "NO")
                  << "\n";
    }
    return true;
}

void Ros2OdomUdpPublisher::stop()
{
    keepAliveStop_.store(true);
    if (keepAliveThread_.joinable()) {
        keepAliveThread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
    lastPayload_.clear();
    lastPublishMs_ = 0;
    stats_.running = false;
}

bool Ros2OdomUdpPublisher::isRunning() const
{
    return socketFd_ >= 0;
}

bool Ros2OdomUdpPublisher::sendPayloadLocked(const std::string& payload,
                                             bool keepAlive,
                                             std::uint64_t nowMsValue)
{
    if (socketFd_ < 0 || payload.empty()) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
    if (::inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr) != 1) {
        lastError_ = "invalid UDP host: " + cfg_.host;
        stats_.sendErrors++;
        stats_.lastPublishOk = false;
        return false;
    }

    const ssize_t sent = ::sendto(socketFd_,
                                  payload.data(),
                                  payload.size(),
                                  0,
                                  reinterpret_cast<sockaddr*>(&addr),
                                  sizeof(addr));
    if (sent < 0) {
        lastError_ = std::string("sendto failed: ") + std::strerror(errno);
        stats_.sendErrors++;
        stats_.lastPublishOk = false;
        return false;
    }

    stats_.packetsSent++;
    if (keepAlive) {
        stats_.keepAlivePacketsSent++;
    }
    stats_.lastSentMs = nowMsValue;
    stats_.lastPublishOk = true;

    lastError_.clear();
    return true;
}

void Ros2OdomUdpPublisher::maybePrintDebugLocked(std::uint64_t nowMsValue,
                                                 const OdomState& odom,
                                                 bool sentNow)
{
    if (!cfg_.consoleDebug) {
        return;
    }

    const std::uint64_t period = cfg_.debugPrintPeriodMs > 0 ? cfg_.debugPrintPeriodMs : 1000;
    if (stats_.lastDebugPrintMs != 0 &&
        (nowMsValue - stats_.lastDebugPrintMs) < period) {
        return;
    }
    stats_.lastDebugPrintMs = nowMsValue;

    std::cout << std::fixed << std::setprecision(3)
              << "[ros2_odom_udp] calls=" << stats_.publishCalls
              << " sent=" << stats_.packetsSent
              << " keepalive=" << stats_.keepAlivePacketsSent
              << " throttle=" << stats_.skippedThrottle
              << " errors=" << stats_.sendErrors
              << " last_sent_now=" << (sentNow ? "YES" : "NO")
              << " input(valid=" << (odom.valid ? 1 : 0)
              << ",fresh=" << (odom.isFresh ? 1 : 0)
              << ",yaw=" << (odom.yawValid ? 1 : 0) << ")"
              << " x=" << odom.xMeters
              << " y=" << odom.yMeters
              << " dist=" << stats_.sessionDistanceM
              << " yaw=" << odom.yawDeg
              << " v=" << odom.linearVelocityMps
              << " w=" << odom.angularVelocityDegPs
              << " fcmd=" << odom.forwardCommand
              << " scmd=" << odom.steeringCommand
              << "\n";
}

void Ros2OdomUdpPublisher::keepAliveLoop()
{
    const auto period = std::chrono::milliseconds(
        cfg_.keepAlivePeriodMs > 0 ? cfg_.keepAlivePeriodMs : 50);

    while (!keepAliveStop_.load()) {
        std::this_thread::sleep_for(period);

        std::lock_guard<std::mutex> lock(mutex_);
        if (socketFd_ >= 0 && !lastPayload_.empty()) {
            // Keep /odom and odom->base_link TF alive even when the main joystick
            // loop is temporarily blocked while map_saver_cli saves the map.
            sendPayloadLocked(lastPayload_, true, wallClockMs());
        }
    }
}

bool Ros2OdomUdpPublisher::publish(const OdomState& odom, std::uint64_t nowMsValue)
{
    if (socketFd_ < 0) {
        return false;
    }

    const int rosValid = cfg_.forceRosValidFreshFlags ? 1 : (odom.valid ? 1 : 0);
    const int rosFresh = cfg_.forceRosValidFreshFlags ? 1 : (odom.isFresh ? 1 : 0);
    const int rosYawValid = odom.yawValid ? 1 : 0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6)
       << nowMsValue << ','
       << odom.xMeters << ','
       << odom.yMeters << ','
       << odom.yawDeg << ','
       << odom.linearVelocityMps << ','
       << odom.angularVelocityDegPs << ','
       << rosValid << ','
       << rosYawValid << ','
       << rosFresh << ','
       // Original flags are appended for bridge diagnostics. Older bridge logic
       // uses only the first 9 fields, so this remains backward-compatible.
       << (odom.valid ? 1 : 0) << ','
       << (odom.isFresh ? 1 : 0) << ','
       << odom.forwardCommand << ','
       << odom.steeringCommand << '\n';

    const std::string payload = ss.str();

    std::lock_guard<std::mutex> lock(mutex_);
    lastPayload_ = payload;

    stats_.running = socketFd_ >= 0;
    stats_.publishCalls++;
    stats_.lastPublishCallMs = nowMsValue;
    stats_.lastInputValid = odom.valid;
    stats_.lastInputFresh = odom.isFresh;
    stats_.lastX = odom.xMeters;
    stats_.lastY = odom.yMeters;
    stats_.lastYawDeg = odom.yawDeg;
    stats_.lastLinearMps = odom.linearVelocityMps;
    stats_.lastAngularDegPs = odom.angularVelocityDegPs;
    if (!stats_.sessionOriginSet) {
        stats_.sessionOriginSet = true;
        stats_.sessionOriginX = odom.xMeters;
        stats_.sessionOriginY = odom.yMeters;
    }
    stats_.sessionDistanceM = std::hypot(odom.xMeters - stats_.sessionOriginX,
                                        odom.yMeters - stats_.sessionOriginY);

    if (lastPublishMs_ != 0 &&
        (nowMsValue - lastPublishMs_) < cfg_.minPublishPeriodMs) {
        stats_.skippedThrottle++;
        maybePrintDebugLocked(nowMsValue, odom, false);
        return true;
    }

    const bool ok = sendPayloadLocked(payload, false, nowMsValue);
    if (ok) {
        lastPublishMs_ = nowMsValue;
    }
    maybePrintDebugLocked(nowMsValue, odom, ok);
    return ok;
}

Ros2OdomUdpPublisher::Stats Ros2OdomUdpPublisher::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

const std::string& Ros2OdomUdpPublisher::lastError() const
{
    return lastError_;
}

const Ros2OdomUdpPublisher::Config& Ros2OdomUdpPublisher::config() const
{
    return cfg_;
}

Ros2OdomUdpPublisher::Config& Ros2OdomUdpPublisher::config()
{
    return cfg_;
}

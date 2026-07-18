#include "lidar/MiniLidarSDL.h"


#include <fcntl.h>
#include <termios.h>
#include <unistd.h>


#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>


namespace {
constexpr speed_t BAUDRATE = B230400;
constexpr double MAX_RANGE_M      = 3.0;
constexpr double ANGLE_OFFSET_DEG = 90.0;
constexpr double PI = 3.14159265358979323846;


constexpr uint8_t STOP_CMD[2] = {0xA5, 0x65};
constexpr uint8_t SCAN_CMD[2] = {0xA5, 0x60};
}


MiniLidarSDL::MiniLidarSDL(std::string port)
    : port_(std::move(port)) {
    points_.reserve(8000);
}


MiniLidarSDL::~MiniLidarSDL() {
    stop();
    cleanupFinishedWorkerIfNeeded();
}


void MiniLidarSDL::setLastError(const std::string& err) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = err;
}


std::string MiniLidarSDL::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}


void MiniLidarSDL::cleanupFinishedWorkerIfNeeded() {
    if (worker_.joinable() && !running_.load()) {
        worker_.join();
    }
}


bool MiniLidarSDL::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);


    // אם thread קודם נגמר ונשאר joinable, לנקות אותו לפני start חדש
    if (worker_.joinable() && !running_.load()) {
        worker_.join();
    }


    if (running_) {
        return false;
    }


    {
        std::lock_guard<std::mutex> pointsLock(pointsMutex_);
        points_.clear();
    }


    setLastError("");


    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&MiniLidarSDL::readerLoop, this);


    std::cout << "[MiniLidarSDL] start() on port: " << port_ << "\n";
    return true;
}


void MiniLidarSDL::stop() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stopRequested_ = true;
    }


    if (worker_.joinable()) {
        worker_.join();
    }


    running_ = false;


    {
        std::lock_guard<std::mutex> pointsLock(pointsMutex_);
        points_.clear();
    }
}


void MiniLidarSDL::toggle() {
    if (isRunning()) {
        stop();
    } else {
        start();
    }
}


bool MiniLidarSDL::isRunning() const {
    return running_;
}


void MiniLidarSDL::getLatestPoints(std::vector<LidarPoint>& out) const {
    std::lock_guard<std::mutex> lock(pointsMutex_);
    out = points_;
}


double MiniLidarSDL::maxRangeMeters() const {
    return MAX_RANGE_M;
}


int MiniLidarSDL::openSerial(const char* port) {
    int fd = ::open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return -1;
    }


    termios tio{};
    if (tcgetattr(fd, &tio) != 0) {
        perror("tcgetattr");
        ::close(fd);
        return -1;
    }


    cfmakeraw(&tio);
    cfsetispeed(&tio, BAUDRATE);
    cfsetospeed(&tio, BAUDRATE);


    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;


    // יותר סלחני מהגרסה הקודמת
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 5; // 0.5 sec


    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        ::close(fd);
        return -1;
    }


    tcflush(fd, TCIOFLUSH);
    return fd;
}


bool MiniLidarSDL::writeAll(int fd, const unsigned char* data, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(fd, data + off, n - off);
        if (w <= 0) {
            return false;
        }
        off += static_cast<size_t>(w);
    }
    return true;
}


bool MiniLidarSDL::readExact(int fd, unsigned char* buf, size_t n) {
    size_t off = 0;


    while (off < n) {
        if (stopRequested_) {
            return false;
        }


        ssize_t r = ::read(fd, buf + off, n - off);


        if (r < 0) {
            return false;
        }


        if (r == 0) {
            // timeout - לא כשל מיידי, רק ננסה שוב
            continue;
        }


        off += static_cast<size_t>(r);
    }


    return true;
}


bool MiniLidarSDL::findHeader(int fd) {
    uint8_t b = 0;
    int timeoutCount = 0;


    while (!stopRequested_) {
        ssize_t r = ::read(fd, &b, 1);


        if (r < 0) {
            return false;
        }


        if (r == 0) {
            // timeout רגעי - ניתן כמה נסיונות לפני שנכריז על כשל
            ++timeoutCount;
            if (timeoutCount > 20) {
                return false;
            }
            continue;
        }


        timeoutCount = 0;


        if (b == 0xAA) {
            uint8_t b2 = 0;
            r = ::read(fd, &b2, 1);


            if (r < 0) {
                return false;
            }


            if (r == 0) {
                continue;
            }


            if (b2 == 0x55) {
                return true;
            }
        }
    }


    return false;
}


void MiniLidarSDL::readerLoop() {
    fd_ = openSerial(port_.c_str());
    if (fd_ < 0) {
        setLastError("Failed to open serial port");
        std::cerr << "[MiniLidarSDL] Failed to open serial port: " << port_ << "\n";
        running_ = false;
        return;
    }


    writeAll(fd_, STOP_CMD, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    tcflush(fd_, TCIOFLUSH);


    if (!writeAll(fd_, SCAN_CMD, 2)) {
        setLastError("Failed to send SCAN command");
        std::cerr << "[MiniLidarSDL] Failed to send SCAN command\n";
        ::close(fd_);
        fd_ = -1;
        running_ = false;
        return;
    }


    std::this_thread::sleep_for(std::chrono::milliseconds(180));


    int packetCount = 0;
    int consecutiveHeaderFails = 0;


    while (!stopRequested_) {
        if (!findHeader(fd_)) {
            if (stopRequested_) {
                break;
            }


            ++consecutiveHeaderFails;
            if (consecutiveHeaderFails <= 3) {
                std::cerr << "[MiniLidarSDL] Header timeout, retrying...\n";
                continue;
            }


            setLastError("Lost LiDAR header");
            std::cerr << "[MiniLidarSDL] Lost LiDAR header\n";
            break;
        }


        consecutiveHeaderFails = 0;


        uint8_t hdr[1 + 1 + 2 + 2 + 2];
        if (!readExact(fd_, hdr, sizeof(hdr))) {
            if (!stopRequested_) {
                setLastError("Failed to read packet header");
                std::cerr << "[MiniLidarSDL] Failed to read packet header\n";
            }
            break;
        }


        const uint8_t  LSN = hdr[1];
        const uint16_t FSA = static_cast<uint16_t>(hdr[2] | (hdr[3] << 8));
        const uint16_t LSA = static_cast<uint16_t>(hdr[4] | (hdr[5] << 8));


        if (LSN == 0) {
            continue;
        }


        std::vector<uint8_t> sampleBytes(LSN * 3);
        if (!readExact(fd_, sampleBytes.data(), sampleBytes.size())) {
            if (!stopRequested_) {
                setLastError("Failed to read sample bytes");
                std::cerr << "[MiniLidarSDL] Failed to read sample bytes\n";
            }
            break;
        }


        const double angleStart = (FSA >> 1) / 64.0;
        const double angleEnd   = (LSA >> 1) / 64.0;


        double diff = angleEnd - angleStart;
        if (diff < 0.0) {
            diff += 360.0;
        }


        const double step = (LSN > 1) ? (diff / (LSN - 1)) : 0.0;


        {
            std::lock_guard<std::mutex> lock(pointsMutex_);


            for (int i = 0; i < LSN; ++i) {
                const uint8_t s1 = sampleBytes[i * 3 + 1];
                const uint8_t s2 = sampleBytes[i * 3 + 2];


                const uint16_t distMm =
                    static_cast<uint16_t>((static_cast<uint16_t>(s2) << 6) |
                                          (static_cast<uint16_t>(s1) >> 2));


                if (distMm == 0) {
                    continue;
                }


                const double distM = distMm / 1000.0;
                const double angleDeg = angleStart + step * i;
                const double angleRad = (angleDeg + ANGLE_OFFSET_DEG) * PI / 180.0;


                const double y = distM * std::cos(angleRad);
                const double x = distM * std::sin(angleRad);


                points_.push_back(LidarPoint{x, y, distM});
            }


            ++packetCount;
            if (packetCount % 2 == 0 && points_.size() > 8000) {
                points_.erase(points_.begin(), points_.begin() + points_.size() / 2);
            }
        }
    }


    if (fd_ >= 0) {
        writeAll(fd_, STOP_CMD, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ::close(fd_);
        fd_ = -1;
    }


    running_ = false;
    stopRequested_ = false;
}




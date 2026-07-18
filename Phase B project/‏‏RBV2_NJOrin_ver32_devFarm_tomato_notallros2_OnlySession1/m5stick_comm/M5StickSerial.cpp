#include "m5stick_comm/M5StickSerial.h"


#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>


#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>


#include <nlohmann/json.hpp>


namespace m5stick {


using json = nlohmann::json;


namespace {


speed_t toSpeedConstant(int baudrate) {
    switch (baudrate) {
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}


bool configurePort(int fd, int baudrate) {
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        return false;
    }


    cfsetispeed(&tty, toSpeedConstant(baudrate));
    cfsetospeed(&tty, toSpeedConstant(baudrate));


    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;


    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;


    return tcsetattr(fd, TCSANOW, &tty) == 0;
}


std::uint64_t monotonicNowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000ULL;
}


}  // namespace


M5StickSerial::M5StickSerial(std::string device_path, int baudrate)
    : device_path_(std::move(device_path)), baudrate_(baudrate) {}


M5StickSerial::~M5StickSerial() {
    stop();
}


bool M5StickSerial::start() {
    if (running_) {
        return true;
    }


    if (!openPort()) {
        return false;
    }


    running_ = true;
    reader_thread_ = std::thread(&M5StickSerial::readerLoop, this);
    return true;
}


void M5StickSerial::stop() {
    if (!running_) {
        closePort();
        return;
    }


    running_ = false;


    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }


    closePort();
}


bool M5StickSerial::isRunning() const {
    return running_;
}


bool M5StickSerial::openPort() {
    fd_ = ::open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        std::cerr << "[M5StickSerial] open failed: " << std::strerror(errno) << "\n";
        return false;
    }


    if (!configurePort(fd_, baudrate_)) {
        std::cerr << "[M5StickSerial] configure failed\n";
        closePort();
        return false;
    }


    tcflush(fd_, TCIOFLUSH);
    return true;
}


void M5StickSerial::closePort() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}


bool M5StickSerial::writeLine(const std::string& line) {
    if (fd_ < 0) {
        return false;
    }


    const std::string full = line + "\n";
    const ssize_t written = ::write(fd_, full.c_str(), full.size());
    return written == static_cast<ssize_t>(full.size());
}


bool M5StickSerial::sendGetStatus() {
    json j;
    j["cmd"] = "get_status";
    return writeLine(j.dump());
}


bool M5StickSerial::sendSetSensor(const std::string& sensor, bool enabled) {
    json j;
    j["cmd"] = "set_sensor";
    j["sensor"] = sensor;
    j["enabled"] = enabled;
    return writeLine(j.dump());
}


bool M5StickSerial::sendSetAll(bool enabled) {
    json j;
    j["cmd"] = "set_all";
    j["enabled"] = enabled;
    return writeLine(j.dump());
}


std::optional<BootMessage> M5StickSerial::getLastBoot() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_boot_;
}


std::optional<StatusMessage> M5StickSerial::getLastStatus() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_status_;
}


std::optional<TelemetryMessage> M5StickSerial::getLastTelemetry() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_telemetry_;
}


std::optional<AckMessage> M5StickSerial::getLastAck() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_ack_;
}


std::uint64_t M5StickSerial::getLastBootRxTimeMs() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_boot_rx_ms_;
}


std::uint64_t M5StickSerial::getLastStatusRxTimeMs() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_status_rx_ms_;
}


std::uint64_t M5StickSerial::getLastTelemetryRxTimeMs() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_telemetry_rx_ms_;
}


std::uint64_t M5StickSerial::getLastAckRxTimeMs() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_ack_rx_ms_;
}


void M5StickSerial::readerLoop() {
    std::string line;
    char ch = '\0';


    while (running_) {
        const ssize_t n = ::read(fd_, &ch, 1);
        if (n <= 0) {
            usleep(2000);
            continue;
        }


        if (ch == '\r') {
            continue;
        }


        if (ch == '\n') {
            if (!line.empty()) {
                parseAndStoreMessage(line);
                line.clear();
            }
            continue;
        }


        line.push_back(ch);
        if (line.size() > 4096) {
            line.clear();
        }
    }
}


bool M5StickSerial::parseAndStoreMessage(const std::string& line) {
    json j;
    try {
        j = json::parse(line);
    } catch (...) {
        return false;
    }


    const std::string type = j.value("type", "");
    const std::uint64_t rxNow = monotonicNowMs();


    std::lock_guard<std::mutex> lock(data_mutex_);


    if (type == "boot") {
        BootMessage msg;
        msg.ok = j.value("ok", false);
        msg.imu_hw_ok = j.value("imu_hw_ok", false);
        msg.env_hw_ok = j.value("env_hw_ok", false);
        last_boot_ = msg;
        last_boot_rx_ms_ = rxNow;
        return true;
    }


    if (type == "status") {
        StatusMessage msg;
        msg.imu_enabled = j.value("imu_enabled", false);
        msg.env_enabled = j.value("env_enabled", false);
        msg.imu_hw_ok = j.value("imu_hw_ok", false);
        msg.env_hw_ok = j.value("env_hw_ok", false);
        msg.telemetry_period_ms = j.value("telemetry_period_ms", 0);
        last_status_ = msg;
        last_status_rx_ms_ = rxNow;
        return true;
    }


    if (type == "ack") {
        AckMessage msg;
        msg.cmd = j.value("cmd", "");
        msg.ok = j.value("ok", false);
        msg.sensor = j.value("sensor", "");
        msg.enabled = j.value("enabled", false);
        msg.reason = j.value("reason", "");
        last_ack_ = msg;
        last_ack_rx_ms_ = rxNow;
        return true;
    }


    if (type == "telemetry") {
        TelemetryMessage msg;
        msg.ts_ms = j.value("ts_ms", 0ULL);


        if (j.contains("imu") && !j["imu"].is_null()) {
            ImuData imu;
            imu.enabled = j["imu"].value("enabled", false);
            imu.ok = j["imu"].value("ok", false);
            imu.ax = j["imu"].value("ax", 0.0);
            imu.ay = j["imu"].value("ay", 0.0);
            imu.az = j["imu"].value("az", 0.0);
            imu.gx = j["imu"].value("gx", 0.0);
            imu.gy = j["imu"].value("gy", 0.0);
            imu.gz = j["imu"].value("gz", 0.0);
            msg.imu = imu;
        }


        if (j.contains("env") && !j["env"].is_null()) {
            EnvData env;
            env.enabled = j["env"].value("enabled", false);
            env.ok = j["env"].value("ok", false);
            env.temp_c = j["env"].value("temp_c", 0.0);
            env.humidity_pct = j["env"].value("humidity_pct", 0.0);
            env.pressure_hpa = j["env"].value("pressure_hpa", 0.0);
            env.gas_kohm = j["env"].value("gas_kohm", 0.0);
            msg.env = env;
        }


        last_telemetry_ = msg;
        last_telemetry_rx_ms_ = rxNow;
        return true;
    }


    return false;
}
}  // namespace m5stick




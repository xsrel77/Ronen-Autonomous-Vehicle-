#pragma once


#include "core/M5StickProtocol.h"


#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>


namespace m5stick {


class M5StickSerial {
public:
    explicit M5StickSerial(std::string device_path = "/dev/ttyACM0", int baudrate = 115200);
    ~M5StickSerial();


    bool start();
    void stop();


    bool isRunning() const;


    bool sendGetStatus();
    bool sendSetSensor(const std::string& sensor, bool enabled);
    bool sendSetAll(bool enabled);


    std::optional<BootMessage> getLastBoot() const;
    std::optional<StatusMessage> getLastStatus() const;
    std::optional<TelemetryMessage> getLastTelemetry() const;
    std::optional<AckMessage> getLastAck() const;


    std::uint64_t getLastBootRxTimeMs() const;
    std::uint64_t getLastStatusRxTimeMs() const;
    std::uint64_t getLastTelemetryRxTimeMs() const;
    std::uint64_t getLastAckRxTimeMs() const;


private:
    bool openPort();
    void closePort();
    void readerLoop();


    bool writeLine(const std::string& line);
    bool parseAndStoreMessage(const std::string& line);


private:
    std::string device_path_;
    int baudrate_;
    int fd_{-1};


    std::atomic<bool> running_{false};
    std::thread reader_thread_;


    mutable std::mutex data_mutex_;
    std::optional<BootMessage> last_boot_;
    std::optional<StatusMessage> last_status_;
    std::optional<TelemetryMessage> last_telemetry_;
    std::optional<AckMessage> last_ack_;


    std::uint64_t last_boot_rx_ms_{0};
    std::uint64_t last_status_rx_ms_{0};
    std::uint64_t last_telemetry_rx_ms_{0};
    std::uint64_t last_ack_rx_ms_{0};
};


}  // namespace m5stick




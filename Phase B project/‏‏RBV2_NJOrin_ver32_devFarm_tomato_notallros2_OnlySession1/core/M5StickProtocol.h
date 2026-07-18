#pragma once


#include <cstdint>
#include <optional>
#include <string>


namespace m5stick {


struct ImuData {
    bool enabled{false};
    bool ok{false};
    double ax{0.0};
    double ay{0.0};
    double az{0.0};
    double gx{0.0};
    double gy{0.0};
    double gz{0.0};
};


struct EnvData {
    bool enabled{false};
    bool ok{false};
    double temp_c{0.0};
    double humidity_pct{0.0};
    double pressure_hpa{0.0};
    double gas_kohm{0.0};
};


struct BootMessage {
    bool ok{false};
    bool imu_hw_ok{false};
    bool env_hw_ok{false};
};


struct StatusMessage {
    bool imu_enabled{false};
    bool env_enabled{false};
    bool imu_hw_ok{false};
    bool env_hw_ok{false};
    int telemetry_period_ms{0};
};


struct AckMessage {
    std::string cmd;
    bool ok{false};
    std::string sensor;
    bool enabled{false};
    std::string reason;
};


struct TelemetryMessage {
    std::uint64_t ts_ms{0};
    std::optional<ImuData> imu;
    std::optional<EnvData> env;
};


enum class MessageType {
    Unknown,
    Boot,
    Status,
    Ack,
    Telemetry
};


struct ParsedMessage {
    MessageType type{MessageType::Unknown};
    std::optional<BootMessage> boot;
    std::optional<StatusMessage> status;
    std::optional<AckMessage> ack;
    std::optional<TelemetryMessage> telemetry;
    std::string raw_json;
};


}  // namespace m5stick




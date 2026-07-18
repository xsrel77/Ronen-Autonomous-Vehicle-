#pragma once

#include <cstdint>
#include <string>

#include "core/SystemState.h"

class Ros2SlamManager
{
public:
    struct Config
    {
        std::string baseDir = "maps/ros2_slam";
        std::string startScript = "services/ros2_slam/scripts/start_ros2_mapping.sh";
        std::string saveScript = "services/ros2_slam/scripts/save_ros2_map.sh";
        std::uint64_t startStopDebounceMs = 800;
        std::uint64_t loadDebounceMs = 500;
    };

public:
    Ros2SlamManager() = default;
    explicit Ros2SlamManager(const Config& cfg);
    ~Ros2SlamManager();

    Ros2SlamManager(const Ros2SlamManager&) = delete;
    Ros2SlamManager& operator=(const Ros2SlamManager&) = delete;

    bool startMapping(std::uint64_t nowMs);
    bool stopMapping(const std::string& reason, std::uint64_t nowMs);
    bool toggleMapping(std::uint64_t nowMs);

    bool loadLatestMap(std::uint64_t nowMs);
    void update(std::uint64_t nowMs);

    bool isMappingActive() const;
    const Ros2SlamState& state() const;

    const Config& config() const;
    Config& config();

private:
    static std::string makeTimestampString();
    static bool ensureDir(const std::string& path);
    static bool fileExists(const std::string& path);
    static int runShellCommand(const std::string& command);
    static std::string shellQuote(const std::string& value);

    void setError(const std::string& errorText, std::uint64_t nowMs);
    void writeLatestMapFile(std::uint64_t nowMs);
    void resetProcessState();

private:
    Config cfg_{};
    Ros2SlamState state_{};

    int mappingPid_ = -1;
    std::uint64_t lastToggleMs_ = 0;
    std::uint64_t lastLoadMs_ = 0;
};

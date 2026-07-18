#include "services/ros2_slam/Ros2SlamManager.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace
{
std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}
}

Ros2SlamManager::Ros2SlamManager(const Config& cfg)
    : cfg_(cfg)
{
}

Ros2SlamManager::~Ros2SlamManager()
{
    if (isMappingActive()) {
        stopMapping("manager_destructor", state_.timestampMs);
    }
}

std::string Ros2SlamManager::makeTimestampString()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

bool Ros2SlamManager::ensureDir(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    std::string partial;
    if (path.front() == '/') {
        partial = "/";
    }

    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (item.empty()) {
            continue;
        }
        if (!partial.empty() && partial.back() != '/') {
            partial += "/";
        }
        partial += item;

        struct stat st{};
        if (::stat(partial.c_str(), &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                return false;
            }
            continue;
        }

        if (::mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }

    return true;
}

bool Ros2SlamManager::fileExists(const std::string& path)
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string Ros2SlamManager::shellQuote(const std::string& value)
{
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

int Ros2SlamManager::runShellCommand(const std::string& command)
{
    return std::system(command.c_str());
}

void Ros2SlamManager::setError(const std::string& errorText, std::uint64_t nowMs)
{
    state_.lastError = errorText;
    state_.statusText = "ERROR: " + errorText;
    state_.timestampMs = nowMs;
    std::cout << "[ros2_slam] " << state_.statusText << "\n";
}

void Ros2SlamManager::resetProcessState()
{
    mappingPid_ = -1;
    state_.mappingActive = false;
    state_.lidarLockedByRos2 = false;
    state_.slamToolboxActive = false;
    state_.odomBridgeActive = false;
}

bool Ros2SlamManager::startMapping(std::uint64_t nowMs)
{
    if ((nowMs - lastToggleMs_) < cfg_.startStopDebounceMs) {
        return false;
    }
    lastToggleMs_ = nowMs;

    if (isMappingActive()) {
        state_.statusText = "ROS2 mapping already active";
        state_.timestampMs = nowMs;
        return true;
    }

    if (!ensureDir(cfg_.baseDir)) {
        setError("failed to create base dir: " + cfg_.baseDir, nowMs);
        return false;
    }

    const std::string sessionName = "session_" + makeTimestampString();
    const std::string sessionDir = joinPath(cfg_.baseDir, sessionName);
    if (!ensureDir(sessionDir)) {
        setError("failed to create session dir: " + sessionDir, nowMs);
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        setError(std::string("fork failed: ") + std::strerror(errno), nowMs);
        return false;
    }

    if (pid == 0) {
        // Put the launch process and its children in a separate process group,
        // so stopMapping can terminate only the ROS2 mapping stack it started.
        ::setpgid(0, 0);
        ::execl("/bin/bash",
                "bash",
                cfg_.startScript.c_str(),
                sessionDir.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }

    mappingPid_ = static_cast<int>(pid);
    state_ = Ros2SlamState{};
    state_.mappingActive = true;
    state_.lidarLockedByRos2 = true;
    state_.slamToolboxActive = true;
    state_.odomBridgeActive = true;
    state_.statusText = "ROS2 mapping active";
    state_.activeSessionDir = sessionDir;
    state_.startedAtMs = nowMs;
    state_.timestampMs = nowMs;

    std::cout << "[ros2_slam] R1 START mapping session: " << sessionDir << "\n";
    return true;
}

bool Ros2SlamManager::stopMapping(const std::string& reason, std::uint64_t nowMs)
{
    if (!isMappingActive()) {
        state_.statusText = "ROS2 mapping already stopped";
        state_.timestampMs = nowMs;
        return true;
    }

    state_.saveRequested = true;
    state_.timestampMs = nowMs;

    const std::string saveCommand = "/bin/bash " + shellQuote(cfg_.saveScript) + " " +
                                    shellQuote(state_.activeSessionDir);
    const int saveRc = runShellCommand(saveCommand);
    state_.lastSaveOk = (saveRc == 0) &&
                        fileExists(joinPath(state_.activeSessionDir, "map.yaml")) &&
                        fileExists(joinPath(state_.activeSessionDir, "map.pgm"));

    if (state_.lastSaveOk) {
        state_.latestMapYaml = joinPath(state_.activeSessionDir, "map.yaml");
        state_.latestMapPgm = joinPath(state_.activeSessionDir, "map.pgm");
        writeLatestMapFile(nowMs);
    } else {
        std::cout << "[ros2_slam] map save failed or map files missing. rc=" << saveRc << "\n";
    }

    if (mappingPid_ > 0) {
        // The child is the process-group leader.
        ::kill(-mappingPid_, SIGTERM);
        for (int i = 0; i < 20; ++i) {
            int status = 0;
            const pid_t r = ::waitpid(mappingPid_, &status, WNOHANG);
            if (r == mappingPid_) {
                mappingPid_ = -1;
                break;
            }
            usleep(100 * 1000);
        }
        if (mappingPid_ > 0) {
            ::kill(-mappingPid_, SIGKILL);
            int status = 0;
            ::waitpid(mappingPid_, &status, 0);
            mappingPid_ = -1;
        }
    }

    state_.mappingActive = false;
    state_.lidarLockedByRos2 = false;
    state_.slamToolboxActive = false;
    state_.odomBridgeActive = false;
    state_.stoppedAtMs = nowMs;
    state_.timestampMs = nowMs;
    state_.statusText = state_.lastSaveOk ? "ROS2 mapping stopped, map saved"
                                          : "ROS2 mapping stopped, map save failed";
    if (!state_.lastSaveOk) {
        state_.lastError = "map save failed on stop reason=" + reason;
    }

    std::cout << "[ros2_slam] R1 STOP mapping. reason=" << reason
              << " save_ok=" << (state_.lastSaveOk ? "YES" : "NO") << "\n";
    return state_.lastSaveOk;
}

bool Ros2SlamManager::toggleMapping(std::uint64_t nowMs)
{
    if (isMappingActive()) {
        return stopMapping("manual_R1_toggle", nowMs);
    }
    return startMapping(nowMs);
}

bool Ros2SlamManager::loadLatestMap(std::uint64_t nowMs)
{
    if ((nowMs - lastLoadMs_) < cfg_.loadDebounceMs) {
        return false;
    }
    lastLoadMs_ = nowMs;

    if (isMappingActive()) {
        state_.statusText = "R2 ignored: stop mapping before loading latest map";
        state_.timestampMs = nowMs;
        std::cout << "[ros2_slam] " << state_.statusText << "\n";
        return false;
    }

    const std::string latestPath = joinPath(cfg_.baseDir, "latest_map.json");
    std::ifstream in(latestPath);
    if (!in) {
        setError("latest map file not found: " + latestPath, nowMs);
        state_.latestMapLoaded = false;
        state_.latestMapValid = false;
        return false;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& ex) {
        setError(std::string("failed to parse latest_map.json: ") + ex.what(), nowMs);
        state_.latestMapLoaded = false;
        state_.latestMapValid = false;
        return false;
    }

    state_.latestMapYaml = j.value("map_yaml", "");
    state_.latestMapPgm = j.value("map_pgm", "");
    state_.activeSessionDir = j.value("session_dir", "");
    state_.latestMapLoaded = true;
    state_.latestMapValid = fileExists(state_.latestMapYaml) && fileExists(state_.latestMapPgm);
    state_.timestampMs = nowMs;

    if (state_.latestMapValid) {
        state_.statusText = "Latest ROS2 map loaded";
        state_.lastError.clear();
        std::cout << "[ros2_slam] R2 LOAD latest map OK: "
                  << state_.latestMapYaml << "\n";
    } else {
        setError("latest map paths are missing files", nowMs);
    }

    return state_.latestMapValid;
}

void Ros2SlamManager::update(std::uint64_t nowMs)
{
    if (mappingPid_ <= 0) {
        state_.timestampMs = nowMs;
        return;
    }

    int status = 0;
    const pid_t r = ::waitpid(mappingPid_, &status, WNOHANG);
    if (r == mappingPid_) {
        mappingPid_ = -1;
        state_.mappingActive = false;
        state_.lidarLockedByRos2 = false;
        state_.slamToolboxActive = false;
        state_.odomBridgeActive = false;
        state_.stoppedAtMs = nowMs;
        state_.timestampMs = nowMs;
        state_.statusText = "ROS2 mapping process exited";
        state_.lastError = "mapping process exited unexpectedly";
        std::cout << "[ros2_slam] mapping process exited unexpectedly\n";
        return;
    }

    state_.timestampMs = nowMs;
}

bool Ros2SlamManager::isMappingActive() const
{
    return mappingPid_ > 0 && state_.mappingActive;
}

const Ros2SlamState& Ros2SlamManager::state() const
{
    return state_;
}

const Ros2SlamManager::Config& Ros2SlamManager::config() const
{
    return cfg_;
}

Ros2SlamManager::Config& Ros2SlamManager::config()
{
    return cfg_;
}

void Ros2SlamManager::writeLatestMapFile(std::uint64_t nowMs)
{
    const std::string latestPath = joinPath(cfg_.baseDir, "latest_map.json");

    nlohmann::json j;
    j["valid"] = true;
    j["created_at_unix_ms"] = nowMs;
    j["session_dir"] = state_.activeSessionDir;
    j["map_yaml"] = state_.latestMapYaml;
    j["map_pgm"] = state_.latestMapPgm;
    j["source"] = "slam_toolbox";
    j["navigation_enabled"] = false;
    j["odom_source"] = "estimated_cmd_imu_no_encoders";

    std::ofstream out(latestPath);
    if (out) {
        out << j.dump(2) << "\n";
    }
}

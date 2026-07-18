#pragma once


#include <cstdint>
#include <fstream>
#include <string>


#include "core/SystemState.h"


class NavLidarDebugLogger
{
public:
    NavLidarDebugLogger() = default;
    ~NavLidarDebugLogger();


    bool start(const std::string& path);
    void stop();


    bool isEnabled() const;


    void log(const NavPoseState& nav,
             const OdomState& odom,
             const LidarPoseState& lidar,
             const LidarCorrectionHintsState& hints,
             const NavGuardState& navGuard,
             const DriveState& drive);


private:
    static const char* bool01(bool v);
    void writeHeader();


private:
    std::ofstream out_{};
    bool enabled_ = false;
};




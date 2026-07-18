#pragma once


#include <fstream>
#include <string>


#include "navigation/NavRuntime.h"
#include "core/SystemState.h"


class NavOdomDebugLogger
{
public:
    NavOdomDebugLogger() = default;
    ~NavOdomDebugLogger();


    bool start(const std::string& csvPath);
    void stop();


    bool isEnabled() const;


    void log(const NavDebugSnapshot& dbg,
             const NavPoseState& nav,
             const OdomState& odom,
             const DriveState& drive);


private:
    std::ofstream file_;
    bool enabled_ = false;
};




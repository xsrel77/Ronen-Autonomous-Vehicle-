#pragma once


#include <cstdint>
#include <fstream>
#include <string>


#include "navigation/NavRuntime.h"


class YawDebugLogger
{
public:
    YawDebugLogger() = default;
    ~YawDebugLogger();


    bool start(const std::string& csvPath);
    void stop();


    bool isEnabled() const;


    void log(const NavDebugSnapshot& dbg, const NavPoseState& state);


private:
    std::ofstream file_;
    bool enabled_ = false;
};




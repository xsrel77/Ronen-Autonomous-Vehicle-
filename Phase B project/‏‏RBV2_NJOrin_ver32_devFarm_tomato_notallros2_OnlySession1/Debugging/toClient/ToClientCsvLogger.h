#pragma once


#include <fstream>
#include <string>


#include "core/SystemState.h"


class ToClientJsonLogger
{
public:
    ToClientJsonLogger() = default;
    ~ToClientJsonLogger();


    bool start(const std::string& jsonlPath,
               const std::string& latestJsonPath = "");
    void stop();


    bool isEnabled() const;


    void log(const RobotState& state);


private:
    static const char* boolText(bool v);
    static std::string jsonText(const std::string& value);


    static const char* robotModeName(RobotMode mode);
    static const char* driveCommandTypeName(DriveCommandType type);
    static const char* navPoseSourceName(NavPoseSource source);
    static const char* navMotionStateName(NavMotionState state);
    static const char* navTurnStateName(NavTurnState state);
    static const char* odomPoseSourceName(OdomPoseSource source);


    static const Detection* findBestDetection(const DetectionSnapshot& snapshot);


    static void writeJsonObject(std::ostream& os,
                                const RobotState& state,
                                bool pretty);


private:
    std::ofstream file_{};
    std::string latestJsonPath_{};
    bool enabled_ = false;
};






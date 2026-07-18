#pragma once


#include <cstdint>
#include <string>


/*
 * Shared drive-related types for control / GUI / logger / future ROS interfaces.
 */


enum class DriveCommandType
{
    None = 0,
    Stop,
    Forward,
    Backward,
    Left,
    Right,
    ForwardLeft,
    ForwardRight,
    BackwardLeft,
    BackwardRight
};


struct DriveCommand
{
    DriveCommandType type = DriveCommandType::None;


    float forwardSpeed = 0.0f;
    float steeringSpeed = 0.0f;


    std::string source;                 // e.g. "joystick", "tracker", "autonomy"
    std::uint64_t timestampMs = 0;
};


struct DriveState
{
    float currentForwardSpeed = 0.0f;
    float currentSteeringSpeed = 0.0f;


    DriveCommand lastCommand{};
    bool emergencyStop = false;


    bool isFresh = false;
    std::uint64_t timeoutMs = 250;
    std::uint64_t lastUpdateTimeMs = 0;
};




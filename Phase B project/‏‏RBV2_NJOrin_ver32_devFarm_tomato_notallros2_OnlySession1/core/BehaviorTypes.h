#pragma once




#include <cstdint>
#include <string>




enum class RobotMode {
    Idle,
    ManualDrive,
    LidarOnly,
    ManualDriveWithLidar,
    Search,
    SearchWithLidar,
    TrackLock,
    TrackLockWithLidar,
    EmergencyStop
};




struct BehaviorDecision {
    RobotMode mode = RobotMode::Idle;




    bool warningActive = false;
    bool obstacleClose = false;
    bool obstacleFront = false;
    bool obstacleLeft  = false;
    bool obstacleRight = false;




    bool targetLost = false;
    bool emergencyStop = false;




    bool m5stickWarning = false;
    bool m5stickDisconnected = false;
    bool imuOffline = false;
    bool envOffline = false;


    bool navWarning = false;
    bool slamActive = false;
    bool slamLost = false;
    bool mapReady = false;
    bool poseValid = false;




    std::string statusText;
    std::uint64_t timestampMs = 0;
};




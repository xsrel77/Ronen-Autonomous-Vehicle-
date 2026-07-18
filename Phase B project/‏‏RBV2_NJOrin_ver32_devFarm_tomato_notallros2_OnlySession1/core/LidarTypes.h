#pragma once


#include <cstdint>
#include <vector>


/*
 * Shared LiDAR types for lidar module / GUI / logger / future ROS interfaces.
 */


struct LidarPoint
{
    double x = 0.0;
    double y = 0.0;
    double dist = 0.0;
};


struct LidarSnapshot
{
    std::vector<LidarPoint> points;


    bool valid = false;


    bool isFresh = false;
    std::uint64_t timeoutMs = 300;
    std::uint64_t timestampMs = 0;
};


struct LidarSummary
{
    double frontMinMeters = -1.0;
    double leftMinMeters  = -1.0;
    double rightMinMeters = -1.0;
    double rearMinMeters  = -1.0;


    bool frontObstacleClose = false;
    bool leftObstacleClose  = false;
    bool rightObstacleClose = false;


    bool obstacleClose = false;
    bool valid = false;


    bool isFresh = false;
    std::uint64_t timeoutMs = 300;
    std::uint64_t timestampMs = 0;
};


struct LidarPoseState
{
    // status
    bool valid = false;
    bool isFresh = false;
    bool isStale = false;


    bool scanValid = false;
    bool enoughPoints = false;


    double confidence = 0.0;      // 0..1
    std::uint64_t timestampMs = 0;
    std::uint64_t timeoutMs = 300;


    // 8 sectors in meters
    // 0=FRONT, 1=FRONT_LEFT, 2=LEFT, 3=REAR_LEFT,
    // 4=REAR, 5=REAR_RIGHT, 6=RIGHT, 7=FRONT_RIGHT
    double frontDistanceM      = -1.0;
    double frontLeftDistanceM  = -1.0;
    double leftDistanceM       = -1.0;
    double rearLeftDistanceM   = -1.0;
    double rearDistanceM       = -1.0;
    double rearRightDistanceM  = -1.0;
    double rightDistanceM      = -1.0;
    double frontRightDistanceM = -1.0;


    // derived metrics
    double lateralBalanceM = 0.0;   // right - left
    double frontBalanceM   = 0.0;   // frontRight - frontLeft
    double rearBalanceM    = 0.0;   // rearRight - rearLeft


    double centerErrorM    = 0.0;
    double headingHintDeg  = 0.0;


    double frontClearanceM = -1.0;
    double rearClearanceM  = -1.0;


    double nearestDistanceM = -1.0;
    int    nearestSectorIndex = -1; // 0..7


    // logical flags
    bool obstacleAhead = false;
    bool obstacleRear = false;


    bool frontLeftBlocked = false;
    bool frontRightBlocked = false;
    bool leftBlocked = false;
    bool rightBlocked = false;
    bool rearLeftBlocked = false;
    bool rearRightBlocked = false;
};




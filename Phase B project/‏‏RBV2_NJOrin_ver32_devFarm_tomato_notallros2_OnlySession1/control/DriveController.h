#pragma once


#include <cstdint>


#include "core/RobotTypes.h"


class MotorDCfb;
class MotorDCLR;


class DriveController {
public:
    struct Config {
        int speedMin = 0;
        int speedMax = 255;


        // FB behavior
        bool fbUseMicro = false;


        // legacy micro values, kept only if re-enabled
        int fbDutyPercent = 21;
        int fbPeriodMs    = 20;


        // release behavior
        bool fbSoftStopEnabled = false;
        int  fbRampStep   = 16;
        int  fbRampTickMs = 20;


        // if true, FB ignores selected speed and always drives at 255
        bool fbForceMaxWhileMoving = false;


        // minimum useful FB power while moving.
        // set to 0 to fully respect selected speed.
        int  fbMinDrivePower = 0;


        // guard against instant forward<->reverse chatter
        int  fbReverseNeutralHoldMs = 90;


        // ============================================
        // LR steering behavior (DC steering motor)
        // ============================================
        // short strong pulse at steering start to overcome static friction
        int lrKickPower = 255;


        // default holding power after the kick phase
        int lrHoldPower = 160;


        // how long the kick stays active
        int lrKickDurationMs = 90;
    };


    struct State {
        int fbSpeed = 180;
        int lrSpeed = 160;


        bool fbOn = false;
        uint64_t fbNextToggleMs = 0;


        int currentFbSigned = 0;
        uint64_t fbNextRampMs = 0;
        uint64_t fbReverseNeutralUntilMs = 0;


        int lastLrCmd = 0;
        int currentLrSigned = 0;
        uint64_t lrKickUntilMs = 0;


        bool emergencyStop = false;
    };


public:
    using SharedState = ::DriveState;


public:
    DriveController(MotorDCfb& rearDrive, MotorDCfb& frontDrive, MotorDCLR& steering);
    DriveController(MotorDCfb& rearDrive, MotorDCfb& frontDrive, MotorDCLR& steering, const Config& cfg);


    void stopAll();
    void step(int fbCmd, int lrCmd, uint64_t nowMs);


    int clampSpeed(int v) const;


    void setFbSpeed(int v);
    void setLrSpeed(int v);
    void adjustFbSpeed(int delta);
    void adjustLrSpeed(int delta);
    void resetSpeeds();


    int getFbSpeed() const;
    int getLrSpeed() const;


    const Config& config() const;
    Config& config();


    const State& state() const;
    State& state();


    SharedState getDriveState() const;


    void setEmergencyStop(bool enabled, uint64_t nowMs = 0);
    bool isEmergencyStopActive() const;


private:
    static int clampi(int v, int lo, int hi);
    void syncSharedState(uint64_t nowMs, DriveCommandType cmdType, float forwardSpeed, float steeringSpeed);


private:
    MotorDCfb& rear_;
    MotorDCfb& front_;
    MotorDCLR& steering_;
    Config cfg_;
    State st_;
    SharedState driveState_{};
};




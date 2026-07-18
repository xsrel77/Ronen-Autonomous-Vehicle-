#include "control/DriveController.h"
#include "hardware/MotorDCfb.h"
#include "hardware/MotorDCLR.h"


namespace {
DriveCommandType commandTypeFromInputs(int fbCmd, int lrCmd) {
    if (fbCmd > 1) fbCmd = 1;
    if (fbCmd < -1) fbCmd = -1;
    if (lrCmd > 1) lrCmd = 1;
    if (lrCmd < -1) lrCmd = -1;


    if (fbCmd == 0 && lrCmd == 0) return DriveCommandType::Stop;
    if (fbCmd > 0 && lrCmd == 0)  return DriveCommandType::Forward;
    if (fbCmd < 0 && lrCmd == 0)  return DriveCommandType::Backward;
    if (fbCmd == 0 && lrCmd < 0)  return DriveCommandType::Left;
    if (fbCmd == 0 && lrCmd > 0)  return DriveCommandType::Right;
    if (fbCmd > 0 && lrCmd < 0)   return DriveCommandType::ForwardLeft;
    if (fbCmd > 0 && lrCmd > 0)   return DriveCommandType::ForwardRight;
    if (fbCmd < 0 && lrCmd < 0)   return DriveCommandType::BackwardLeft;
    if (fbCmd < 0 && lrCmd > 0)   return DriveCommandType::BackwardRight;


    return DriveCommandType::None;
}


int signOfInt(int v) {
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
}
}


int DriveController::clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}


DriveController::DriveController(MotorDCfb& rearDrive, MotorDCfb& frontDrive, MotorDCLR& steering)
    : rear_(rearDrive), front_(frontDrive), steering_(steering), cfg_{}, st_{}, driveState_{} {}


DriveController::DriveController(MotorDCfb& rearDrive, MotorDCfb& frontDrive, MotorDCLR& steering, const Config& cfg)
    : rear_(rearDrive), front_(frontDrive), steering_(steering), cfg_(cfg), st_{}, driveState_{} {
    st_.lrSpeed = clampi(cfg_.lrHoldPower, cfg_.speedMin, cfg_.speedMax);
}


void DriveController::syncSharedState(uint64_t nowMs, DriveCommandType cmdType, float forwardSpeed, float steeringSpeed) {
    driveState_.currentForwardSpeed = forwardSpeed;
    driveState_.currentSteeringSpeed = steeringSpeed;
    driveState_.lastCommand.type = cmdType;
    driveState_.lastCommand.forwardSpeed = forwardSpeed;
    driveState_.lastCommand.steeringSpeed = steeringSpeed;
    driveState_.lastCommand.source = "drive_controller";
    driveState_.lastUpdateTimeMs = nowMs;
    driveState_.lastCommand.timestampMs = nowMs;
    driveState_.emergencyStop = st_.emergencyStop;
}


void DriveController::stopAll() {
    rear_.stop();
    front_.stop();
    steering_.stop();


    st_.fbOn = false;
    st_.fbNextToggleMs = 0;
    st_.currentFbSigned = 0;
    st_.fbNextRampMs = 0;
    st_.fbReverseNeutralUntilMs = 0;


    st_.lastLrCmd = 0;
    st_.currentLrSigned = 0;
    st_.lrKickUntilMs = 0;


    syncSharedState(0, DriveCommandType::Stop, 0.0f, 0.0f);
}


int DriveController::clampSpeed(int v) const {
    return clampi(v, cfg_.speedMin, cfg_.speedMax);
}


void DriveController::setFbSpeed(int v) {
    st_.fbSpeed = clampSpeed(v);
}


void DriveController::setLrSpeed(int v) {
    st_.lrSpeed = clampSpeed(v);
}


void DriveController::adjustFbSpeed(int delta) {
    setFbSpeed(st_.fbSpeed + delta);
}


void DriveController::adjustLrSpeed(int delta) {
    setLrSpeed(st_.lrSpeed + delta);
}


void DriveController::resetSpeeds() {
    st_.fbSpeed = 40;
    st_.lrSpeed = clampi(cfg_.lrHoldPower, cfg_.speedMin, cfg_.speedMax);
}


int DriveController::getFbSpeed() const {
    return st_.fbSpeed;
}


int DriveController::getLrSpeed() const {
    return st_.lrSpeed;
}


const DriveController::Config& DriveController::config() const {
    return cfg_;
}


DriveController::Config& DriveController::config() {
    return cfg_;
}


const DriveController::State& DriveController::state() const {
    return st_;
}


DriveController::State& DriveController::state() {
    return st_;
}


DriveController::SharedState DriveController::getDriveState() const {
    return driveState_;
}


void DriveController::setEmergencyStop(bool enabled, uint64_t nowMs) {
    st_.emergencyStop = enabled;


    rear_.stop();
    front_.stop();
    steering_.stop();


    st_.fbOn = false;
    st_.fbNextToggleMs = 0;
    st_.currentFbSigned = 0;
    st_.fbNextRampMs = 0;
    st_.fbReverseNeutralUntilMs = 0;


    st_.lastLrCmd = 0;
    st_.currentLrSigned = 0;
    st_.lrKickUntilMs = 0;


    syncSharedState(nowMs, DriveCommandType::Stop, 0.0f, 0.0f);
}


bool DriveController::isEmergencyStopActive() const {
    return st_.emergencyStop;
}


void DriveController::step(int fbCmd, int lrCmd, uint64_t nowMs) {
    if (st_.emergencyStop) {
        rear_.stop();
        front_.stop();
        steering_.stop();


        st_.fbOn = false;
        st_.fbNextToggleMs = 0;
        st_.currentFbSigned = 0;
        st_.fbNextRampMs = 0;
        st_.fbReverseNeutralUntilMs = 0;


        st_.lastLrCmd = 0;
        st_.currentLrSigned = 0;
        st_.lrKickUntilMs = 0;


        syncSharedState(nowMs, DriveCommandType::Stop, 0.0f, 0.0f);
        return;
    }


    if (fbCmd > 1) fbCmd = 1;
    if (fbCmd < -1) fbCmd = -1;
    if (lrCmd > 1) lrCmd = 1;
    if (lrCmd < -1) lrCmd = -1;


    st_.fbSpeed = clampSpeed(st_.fbSpeed);
    st_.lrSpeed = clampSpeed(st_.lrSpeed);


    const int requestedFbSign = signOfInt(fbCmd);
    const int currentFbSign = signOfInt(st_.currentFbSigned);


    // hard guard: never reverse drive direction instantly.
    if (requestedFbSign != 0 &&
        currentFbSign != 0 &&
        requestedFbSign != currentFbSign) {


        rear_.stop();
        front_.stop();


        st_.fbOn = false;
        st_.fbNextToggleMs = 0;
        st_.currentFbSigned = 0;
        st_.fbNextRampMs = 0;
        st_.fbReverseNeutralUntilMs =
            nowMs + static_cast<uint64_t>(clampi(cfg_.fbReverseNeutralHoldMs, 0, 1000));


        fbCmd = 0;
    }


    if (st_.fbReverseNeutralUntilMs != 0) {
        if (nowMs < st_.fbReverseNeutralUntilMs) {
            fbCmd = 0;
        } else {
            st_.fbReverseNeutralUntilMs = 0;
        }
    }


    // ===============================================
    // Dual drive FB: rear + front motors together
    // ===============================================
    if (!cfg_.fbUseMicro) {
        if (fbCmd != 0 && st_.fbSpeed > 0) {
            int target = cfg_.fbForceMaxWhileMoving ? cfg_.speedMax : st_.fbSpeed;


            if (cfg_.fbMinDrivePower > 0 && target < cfg_.fbMinDrivePower) {
                target = cfg_.fbMinDrivePower;
            }


            target = clampi(target, 0, cfg_.speedMax);


            st_.currentFbSigned = (fbCmd > 0) ? target : -target;


            rear_.applySigned(st_.currentFbSigned);
            front_.applySigned(st_.currentFbSigned);


            st_.fbNextRampMs = nowMs + static_cast<uint64_t>(cfg_.fbRampTickMs);
        } else {
            if (!cfg_.fbSoftStopEnabled) {
                st_.currentFbSigned = 0;
                rear_.stop();
                front_.stop();
            } else {
                if (st_.currentFbSigned != 0 && nowMs >= st_.fbNextRampMs) {
                    const int step = clampi(cfg_.fbRampStep, 1, 255);


                    if (st_.currentFbSigned > 0) {
                        st_.currentFbSigned -= step;
                        if (st_.currentFbSigned < 0) st_.currentFbSigned = 0;
                    } else {
                        st_.currentFbSigned += step;
                        if (st_.currentFbSigned > 0) st_.currentFbSigned = 0;
                    }


                    if (st_.currentFbSigned == 0) {
                        rear_.stop();
                        front_.stop();
                    } else {
                        rear_.applySigned(st_.currentFbSigned);
                        front_.applySigned(st_.currentFbSigned);
                    }


                    st_.fbNextRampMs = nowMs + static_cast<uint64_t>(cfg_.fbRampTickMs);
                }
            }
        }
    } else {
        // legacy micro mode for both drive motors together
        const int period = clampi(cfg_.fbPeriodMs, 1, 2000);
        const int duty   = clampi(cfg_.fbDutyPercent, 1, 100);
        const int onMs   = clampi((period * duty) / 100, 1, period);
        const int offMs  = period - onMs;


        if (fbCmd == 0 || st_.fbSpeed == 0) {
            if (st_.fbOn) {
                rear_.stop();
                front_.stop();
                st_.fbOn = false;
            }
            st_.fbNextToggleMs = nowMs;
            st_.currentFbSigned = 0;
        } else {
            if (nowMs >= st_.fbNextToggleMs) {
                if (!st_.fbOn) {
                    int target = cfg_.fbForceMaxWhileMoving ? cfg_.speedMax : st_.fbSpeed;


                    if (cfg_.fbMinDrivePower > 0 && target < cfg_.fbMinDrivePower) {
                        target = cfg_.fbMinDrivePower;
                    }


                    target = clampi(target, 0, cfg_.speedMax);


                    const int signedSpeed = (fbCmd > 0) ? target : -target;
                    rear_.applySigned(signedSpeed);
                    front_.applySigned(signedSpeed);
                    st_.currentFbSigned = signedSpeed;


                    st_.fbOn = true;
                    st_.fbNextToggleMs = nowMs + static_cast<uint64_t>(onMs);
                } else {
                    rear_.stop();
                    front_.stop();


                    st_.fbOn = false;
                    st_.fbNextToggleMs = nowMs + static_cast<uint64_t>(offMs);
                    st_.currentFbSigned = 0;
                }
            }
        }
    }


    // ============================================
    // Steering: front wheels left/right
    // DC motor with kick + hold behavior
    // ============================================
    const int requestedLrSign = signOfInt(lrCmd);
    const int currentLrSign = signOfInt(st_.currentLrSigned);


    if (requestedLrSign == 0 || st_.lrSpeed == 0) {
        if (st_.currentLrSigned != 0 || st_.lastLrCmd != 0) {
            steering_.stop();
        }
        st_.lastLrCmd = 0;
        st_.currentLrSigned = 0;
        st_.lrKickUntilMs = 0;
    } else {
        const int holdPower = clampi(st_.lrSpeed, 0, cfg_.speedMax);
        const int kickPower = clampi(cfg_.lrKickPower, 0, cfg_.speedMax);
        const int kickDurationMs = clampi(cfg_.lrKickDurationMs, 0, 1000);


        const bool directionChanged =
            (requestedLrSign != currentLrSign);


        if (directionChanged) {
            st_.lrKickUntilMs = nowMs + static_cast<uint64_t>(kickDurationMs);
        }


        const bool kickActive = (nowMs < st_.lrKickUntilMs);
        int activePower = holdPower;


        if (kickActive && kickPower > activePower) {
            activePower = kickPower;
        }


        const int desiredLrSigned = (requestedLrSign > 0) ? activePower : -activePower;


        if (desiredLrSigned != st_.currentLrSigned) {
            steering_.applySigned(desiredLrSigned);
        }


        st_.currentLrSigned = desiredLrSigned;
        st_.lastLrCmd = lrCmd;
    }


    const float sharedForward = static_cast<float>(st_.currentFbSigned);
    const float sharedSteering = static_cast<float>(st_.currentLrSigned);


    syncSharedState(nowMs, commandTypeFromInputs(fbCmd, lrCmd), sharedForward, sharedSteering);
}




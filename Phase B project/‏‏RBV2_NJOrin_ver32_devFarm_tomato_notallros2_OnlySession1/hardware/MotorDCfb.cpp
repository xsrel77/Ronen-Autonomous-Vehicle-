#include "hardware/MotorDCfb.h"
#include "hardware/RaspbotBoard.h"


#include <unistd.h>
#include <cstdlib>


int MotorDCfb::clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


MotorDCfb::MotorDCfb(RaspbotBoard& board, uint8_t channel, bool inverted)
    : board_(board), ch_(channel), inverted_(inverted) {}


bool MotorDCfb::motorWrite(uint8_t dir, uint8_t speed) {
    return board_.setMotor(
        ch_,
        (dir == DIR_FORWARD)
            ? RaspbotBoard::MotorDirection::Forward
            : RaspbotBoard::MotorDirection::Backward,
        speed
    );
}


void MotorDCfb::stop() {
    motorWrite(DIR_FORWARD, 0);
}


void MotorDCfb::applySigned(int speedSigned) {
    int s = clampi(speedSigned, -255, 255);
    if (s == 0) {
        stop();
        return;
    }


    const bool wantForward = (s > 0);
    uint8_t dir;


    if (inverted_) {
        dir = wantForward ? DIR_BACKWARD : DIR_FORWARD;
    } else {
        dir = wantForward ? DIR_FORWARD : DIR_BACKWARD;
    }


    const uint8_t mag = static_cast<uint8_t>(std::abs(s));
    motorWrite(dir, mag);
}


void MotorDCfb::motorMicro(uint8_t dir, uint8_t speed,
                           int dutyPercent, int periodMs, int durationMs) {
    dutyPercent = clampi(dutyPercent, 1, 100);
    periodMs    = clampi(periodMs, 20, 2000);
    durationMs  = clampi(durationMs, 50, 20000);


    int onMs  = (periodMs * dutyPercent) / 100;
    int offMs = periodMs - onMs;
    if (onMs < 1) onMs = 1;
    if (offMs < 0) offMs = 0;


    int elapsed = 0;
    while (elapsed < durationMs) {
        motorWrite(dir, speed);
        usleep((useconds_t)onMs * 1000);


        motorWrite(DIR_FORWARD, 0);
        usleep((useconds_t)offMs * 1000);


        elapsed += periodMs;
    }


    motorWrite(DIR_FORWARD, 0);
}


void MotorDCfb::drive(int speedSigned, bool micro,
                      int dutyPercent, int periodMs, int durationMs) {
    int s = clampi(speedSigned, -255, 255);
    if (s == 0) {
        stop();
        return;
    }


    const bool wantForward = (s > 0);
    uint8_t dir;


    if (inverted_) {
        dir = wantForward ? DIR_BACKWARD : DIR_FORWARD;
    } else {
        dir = wantForward ? DIR_FORWARD : DIR_BACKWARD;
    }


    const uint8_t mag = static_cast<uint8_t>(std::abs(s));


    if (!micro) {
        motorWrite(dir, mag);
        usleep((useconds_t)durationMs * 1000);
        stop();
    } else {
        motorMicro(dir, mag, dutyPercent, periodMs, durationMs);
    }
}


void MotorDCfb::testOnce(int speed, bool forward, bool micro,
                         int dutyPercent, int periodMs, int durationMs) {
    speed = clampi(speed, 0, 255);
    const int signedSpeed = forward ? speed : -speed;
    drive(signedSpeed, micro, dutyPercent, periodMs, durationMs);
}




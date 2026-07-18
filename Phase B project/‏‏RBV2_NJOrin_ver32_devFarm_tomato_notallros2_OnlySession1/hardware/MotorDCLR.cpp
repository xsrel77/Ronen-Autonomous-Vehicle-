#include "hardware/MotorDCLR.h"
#include "hardware/RaspbotBoard.h"


#include <unistd.h>
#include <cstdlib>


int MotorDCLR::clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


MotorDCLR::MotorDCLR(RaspbotBoard& board, uint8_t channel)
    : board_(board), ch_(channel) {}


bool MotorDCLR::motorWrite(uint8_t dir, uint8_t speed) {
    return board_.setMotor(
        ch_,
        (dir == DIR_FORWARD)
            ? RaspbotBoard::MotorDirection::Forward
            : RaspbotBoard::MotorDirection::Backward,
        speed
    );
}


void MotorDCLR::stop() {
    motorWrite(DIR_FORWARD, 0);
}


void MotorDCLR::applySigned(int speedSigned) {
    int s = clampi(speedSigned, -255, 255);
    if (s == 0) {
        stop();
        return;
    }


    // Channel 2 mapping: + = RIGHT, - = LEFT
    const bool wantRight = (s > 0);
    const uint8_t dir = wantRight ? DIR_FORWARD : DIR_BACKWARD;
    const uint8_t mag = static_cast<uint8_t>(std::abs(s));


    motorWrite(dir, mag);
}


void MotorDCLR::motorMicro(uint8_t dir, uint8_t speed,
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


void MotorDCLR::steer(int speedSigned, bool micro,
                      int dutyPercent, int periodMs, int durationMs) {
    int s = clampi(speedSigned, -255, 255);
    if (s == 0) {
        stop();
        return;
    }


    const bool wantRight = (s > 0);
    const uint8_t dir = wantRight ? DIR_FORWARD : DIR_BACKWARD;
    const uint8_t mag = static_cast<uint8_t>(std::abs(s));


    if (!micro) {
        motorWrite(dir, mag);
        usleep((useconds_t)durationMs * 1000);
        stop();
    } else {
        motorMicro(dir, mag, dutyPercent, periodMs, durationMs);
    }
}


void MotorDCLR::testOnce(int speed, bool right, bool micro,
                         int dutyPercent, int periodMs, int durationMs) {
    speed = clampi(speed, 0, 255);
    const int signedSpeed = right ? speed : -speed;
    steer(signedSpeed, micro, dutyPercent, periodMs, durationMs);
}




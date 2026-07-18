#pragma once


#include <cstdint>


class RaspbotBoard;


class MotorDCLR {
public:
    explicit MotorDCLR(RaspbotBoard& board, uint8_t channel = 2);


    void stop();


    // immediate non-blocking command:
    // +speed => RIGHT, -speed => LEFT, 0 => stop
    void applySigned(int speedSigned);


    // blocking helper
    void steer(int speedSigned, bool micro,
               int dutyPercent, int periodMs, int durationMs);


    void testOnce(int speed, bool right, bool micro,
                  int dutyPercent, int periodMs, int durationMs);


private:
    static int clampi(int v, int lo, int hi);


    bool motorWrite(uint8_t dir, uint8_t speed);
    void motorMicro(uint8_t dir, uint8_t speed, int dutyPercent, int periodMs, int durationMs);


private:
    RaspbotBoard& board_;
    uint8_t ch_;


    static constexpr uint8_t DIR_FORWARD  = 0;
    static constexpr uint8_t DIR_BACKWARD = 1;
};




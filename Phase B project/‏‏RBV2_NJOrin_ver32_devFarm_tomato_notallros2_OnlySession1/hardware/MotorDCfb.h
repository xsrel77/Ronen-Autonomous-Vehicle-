#pragma once


#include <cstdint>


class RaspbotBoard;


class MotorDCfb {
public:
    // channel = motor channel on Raspbot board
    // inverted=true keeps the old "forward is physically reversed" behavior
    explicit MotorDCfb(RaspbotBoard& board, uint8_t channel = 1, bool inverted = true);


    void stop();


    // immediate non-blocking command:
    // +speed => forward, -speed => backward, 0 => stop
    void applySigned(int speedSigned);


    // blocking helper
    void drive(int speedSigned, bool micro,
               int dutyPercent, int periodMs, int durationMs);


    void testOnce(int speed, bool forward, bool micro,
                  int dutyPercent, int periodMs, int durationMs);


    uint8_t channel() const { return ch_; }
    bool inverted() const { return inverted_; }
    void setInverted(bool v) { inverted_ = v; }


private:
    static int clampi(int v, int lo, int hi);


    bool motorWrite(uint8_t dir, uint8_t speed);
    void motorMicro(uint8_t dir, uint8_t speed, int dutyPercent, int periodMs, int durationMs);


private:
    RaspbotBoard& board_;
    uint8_t ch_;
    bool inverted_;


    static constexpr uint8_t DIR_FORWARD  = 0;
    static constexpr uint8_t DIR_BACKWARD = 1;
};




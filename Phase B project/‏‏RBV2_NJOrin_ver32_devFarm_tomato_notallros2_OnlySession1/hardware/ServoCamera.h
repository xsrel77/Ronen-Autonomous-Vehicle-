#pragma once


#include <cstdint>
#include <utility>


class RaspbotBoard;


class ServoCamera {
public:
    explicit ServoCamera(RaspbotBoard& board,
                         uint8_t panServoNum = 1,
                         uint8_t tiltServoNum = 2,
                         int centerPan = 90,
                         int centerTilt = 90);


    void center();
    void setAngles(int panDeg, int tiltDeg);
    std::pair<int,int> getAngles() const;
    void setCenter(int centerPan, int centerTilt);


    void panLeft(int stepDeg = 4);
    void panRight(int stepDeg = 4);
    void tiltUp(int stepDeg = 4);
    void tiltDown(int stepDeg = 4);


    void nudgePan(int deltaDeg);
    void nudgeTilt(int deltaDeg);


    void runMovementTest(int deltaDeg = 10, int delayMs = 600);
    void runInteractive();


private:
    static int clamp(int v, int lo, int hi);
    void apply();


    RaspbotBoard& board_;
    uint8_t panServo_;
    uint8_t tiltServo_;
    int panDeg_;
    int tiltDeg_;
    int centerPan_;
    int centerTilt_;
};




#include "hardware/ServoCamera.h"
#include "hardware/RaspbotBoard.h"


#include <iostream>
#include <unistd.h>


int ServoCamera::clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


ServoCamera::ServoCamera(RaspbotBoard& board,
                         uint8_t panServoNum,
                         uint8_t tiltServoNum,
                         int centerPan,
                         int centerTilt)
    : board_(board),
      panServo_(panServoNum),
      tiltServo_(tiltServoNum),
      panDeg_(clamp(centerPan, 0, 180)),
      tiltDeg_(clamp(centerTilt, 0, 180)),
      centerPan_(centerPan),
      centerTilt_(centerTilt) {
    apply();
}


void ServoCamera::apply() {
    board_.setServo(panServo_,  static_cast<uint8_t>(clamp(panDeg_,  0, 180)));
    board_.setServo(tiltServo_, static_cast<uint8_t>(clamp(tiltDeg_, 0, 180)));
}


void ServoCamera::center() {
    panDeg_  = clamp(centerPan_,  0, 180);
    tiltDeg_ = clamp(centerTilt_, 0, 180);
    apply();
}


void ServoCamera::setAngles(int panDeg, int tiltDeg) {
    panDeg_  = clamp(panDeg,  0, 180);
    tiltDeg_ = clamp(tiltDeg, 0, 180);
    apply();
}


std::pair<int,int> ServoCamera::getAngles() const {
    return { panDeg_, tiltDeg_ };
}


void ServoCamera::setCenter(int centerPan, int centerTilt) {
    centerPan_  = centerPan;
    centerTilt_ = centerTilt;
}


void ServoCamera::nudgePan(int deltaDeg) {
    panDeg_ = clamp(panDeg_ + deltaDeg, 0, 180);
    apply();
}


void ServoCamera::nudgeTilt(int deltaDeg) {
    tiltDeg_ = clamp(tiltDeg_ + deltaDeg, 0, 180);
    apply();
}


void ServoCamera::panLeft(int stepDeg) {
    nudgePan(-stepDeg);
}


void ServoCamera::panRight(int stepDeg) {
    nudgePan(+stepDeg);
}


void ServoCamera::tiltUp(int stepDeg) {
    nudgeTilt(-stepDeg);
}


void ServoCamera::tiltDown(int stepDeg) {
    nudgeTilt(+stepDeg);
}


void ServoCamera::runMovementTest(int deltaDeg, int delayMs) {
    std::cout << "\n[cam] movement test start\n";
    center();
    usleep((useconds_t)delayMs * 1000);


    std::cout << "[cam] pan right\n";
    nudgePan(+deltaDeg);
    usleep((useconds_t)delayMs * 1000);


    std::cout << "[cam] pan left\n";
    nudgePan(-2 * deltaDeg);
    usleep((useconds_t)delayMs * 1000);


    std::cout << "[cam] center pan\n";
    center();
    usleep((useconds_t)delayMs * 1000);


    std::cout << "[cam] tilt down\n";
    nudgeTilt(+deltaDeg);
    usleep((useconds_t)delayMs * 1000);


    std::cout << "[cam] tilt up\n";
    nudgeTilt(-2 * deltaDeg);
    usleep((useconds_t)delayMs * 1000);


    std::cout << "[cam] center all\n";
    center();
    std::cout << "[cam] movement test done\n";
}


void ServoCamera::runInteractive() {
    std::cout << "\n[cam] Interactive camera calibration\n"
              << "Controls:\n"
              << "  w - tilt up\n"
              << "  s - tilt down\n"
              << "  a - pan left\n"
              << "  d - pan right\n"
              << "  c - center\n"
              << "  p - print angles\n"
              << "  q - quit\n";


    bool done = false;
    while (!done) {
        std::cout << "\n[cam] choose: ";
        char ch = 0;
        std::cin >> ch;
        if (!std::cin) return;


        switch (ch) {
            case 'w': case 'W': tiltUp(4); break;
            case 's': case 'S': tiltDown(4); break;
            case 'a': case 'A': panLeft(4); break;
            case 'd': case 'D': panRight(4); break;
            case 'c': case 'C': center(); break;
            case 'p': case 'P': {
                auto ang = getAngles();
                std::cout << "[cam] pan=" << ang.first << " tilt=" << ang.second << "\n";
                break;
            }
            case 'q': case 'Q':
                done = true;
                break;
            default:
                std::cout << "[cam] unknown key\n";
                break;
        }
    }
}




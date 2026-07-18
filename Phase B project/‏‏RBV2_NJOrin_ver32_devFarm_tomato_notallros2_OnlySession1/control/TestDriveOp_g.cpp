#include "control/TestDriveOp_g.h"
#include "control/DriveController.h"


#include <iostream>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <dirent.h>
#include <string>
#include <ctime>
#include <cerrno>


static uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}


static std::string find_keyboard_event_device() {
    const char* bypath = "/dev/input/by-path";
    DIR* d = opendir(bypath);
    if (d) {
        dirent* de;
        while ((de = readdir(d)) != nullptr) {
            std::string name = de->d_name;
            if (name.find("-event-kbd") != std::string::npos) {
                closedir(d);
                return std::string(bypath) + "/" + name;
            }
        }
        closedir(d);
    }
    return "";
}


static int open_input_device_nonblock(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) perror(("open input " + path).c_str());
    return fd;
}


void RunTestDriveOp_g(DriveController& drive) {
    const int SPEED_STEP = 5;


    const std::string kbd = find_keyboard_event_device();
    if (kbd.empty()) {
        std::cout << "\n[TestDriveOp_g] Could not auto-detect keyboard device.\n"
                  << "Run:\n  ls -l /dev/input/by-path/*event-kbd\n"
                  << "and run this program with sudo.\n";
        return;
    }


    const int kfd = open_input_device_nonblock(kbd);
    if (kfd < 0) {
        std::cout << "[TestDriveOp_g] Failed to open: " << kbd << "\n";
        return;
    }


    drive.stopAll();
    drive.resetSpeeds();


    std::cout << "\n=== Drive Mode (g) ===\n"
              << "Keyboard device: " << kbd << "\n"
              << "Hold keys (simultaneous supported):\n"
              << "  W = forward  (FB micro)\n"
              << "  S = backward (FB micro)\n"
              << "  A = left     (LR raw)\n"
              << "  D = right    (LR raw)\n"
              << "  SPACE = stop both\n"
              << "  Q = exit\n\n"
              << "Speed tuning:\n"
              << "  1/2 -> CH1(FB) -/+\n"
              << "  3/4 -> CH2(LR) -/+\n"
              << "  R   -> reset speeds\n\n"
              << "[speed] CH1(FB)=" << drive.getFbSpeed()
              << "  CH2(LR)=" << drive.getLrSpeed() << "\n\n";


    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
    bool running = true;


    while (running) {
        input_event ev{};


        while (true) {
            const ssize_t r = ::read(kfd, &ev, sizeof(ev));
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("[TestDriveOp_g] read(evdev)");
                running = false;
                break;
            }
            if (r != static_cast<ssize_t>(sizeof(ev))) break;


            if (ev.type == EV_KEY) {
                const bool down = (ev.value == 1);
                const bool up   = (ev.value == 0);


                switch (ev.code) {
                    case KEY_W: if (down) w = true; if (up) w = false; break;
                    case KEY_S: if (down) s = true; if (up) s = false; break;
                    case KEY_A: if (down) a = true; if (up) a = false; break;
                    case KEY_D: if (down) d = true; if (up) d = false; break;


                    case KEY_SPACE:
                        if (down) {
                            w = a = s = d = false;
                            drive.stopAll();
                        }
                        break;


                    case KEY_Q:
                        if (down) running = false;
                        break;


                    case KEY_1:
                        if (down) {
                            drive.adjustFbSpeed(-SPEED_STEP);
                            std::cout << "[speed] CH1(FB)=" << drive.getFbSpeed()
                                      << "  CH2(LR)=" << drive.getLrSpeed() << "\n";
                        }
                        break;


                    case KEY_2:
                        if (down) {
                            drive.adjustFbSpeed(+SPEED_STEP);
                            std::cout << "[speed] CH1(FB)=" << drive.getFbSpeed()
                                      << "  CH2(LR)=" << drive.getLrSpeed() << "\n";
                        }
                        break;


                    case KEY_3:
                        if (down) {
                            drive.adjustLrSpeed(-SPEED_STEP);
                            std::cout << "[speed] CH1(FB)=" << drive.getFbSpeed()
                                      << "  CH2(LR)=" << drive.getLrSpeed() << "\n";
                        }
                        break;


                    case KEY_4:
                        if (down) {
                            drive.adjustLrSpeed(+SPEED_STEP);
                            std::cout << "[speed] CH1(FB)=" << drive.getFbSpeed()
                                      << "  CH2(LR)=" << drive.getLrSpeed() << "\n";
                        }
                        break;


                    case KEY_R:
                        if (down) {
                            drive.resetSpeeds();
                            std::cout << "[speed] CH1(FB)=" << drive.getFbSpeed()
                                      << "  CH2(LR)=" << drive.getLrSpeed() << "\n";
                        }
                        break;


                    default:
                        break;
                }
            }
        }


        int fbCmd = 0;
        if (w && !s) fbCmd = +1;
        else if (s && !w) fbCmd = -1;


        int lrCmd = 0;
        if (d && !a) lrCmd = +1;
        else if (a && !d) lrCmd = -1;


        drive.step(fbCmd, lrCmd, now_ms());


        usleep(5000);
    }


    drive.stopAll();
    ::close(kfd);
    std::cout << "[TestDriveOp_g] exit.\n";
}




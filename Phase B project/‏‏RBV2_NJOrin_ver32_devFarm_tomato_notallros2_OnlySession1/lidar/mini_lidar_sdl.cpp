#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>


#include <SDL2/SDL.h>


// ================= הגדרות ליידאר ===================
static constexpr speed_t BAUDRATE = B230400;


static constexpr double MAX_RANGE_M   = 3.0;   // רדיוס מקסימלי שנצייר בחלון
static constexpr double MIN_VALID_M   = 0.05;  // מתחת לזה נתעלם
static constexpr double NEAR_MAX_M    = 0.50;  // עד כאן נחשב "קרוב"
static constexpr double MID_MAX_M     = 1.50;  // עד כאן "בינוני"


static constexpr double ANGLE_OFFSET_DEG = 90.0; // סיבוב כך שהחזית תהיה למעלה


// ================= הגדרות חלון =====================
static constexpr int WIN_W = 800;
static constexpr int WIN_H = 800;


static constexpr double PI = 3.14159265358979323846;


struct Point {
    double x;
    double y;
    double dist;
};


int openSerial(const char* port) {
    int fd = ::open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return -1;
    }


    termios tio{};
    if (tcgetattr(fd, &tio) != 0) {
        perror("tcgetattr");
        ::close(fd);
        return -1;
    }


    cfmakeraw(&tio);
    cfsetispeed(&tio, BAUDRATE);
    cfsetospeed(&tio, BAUDRATE);


    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;


    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 5; // 0.5 שניות timeout


    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        ::close(fd);
        return -1;
    }


    tcflush(fd, TCIOFLUSH);
    return fd;
}


bool writeAll(int fd, const uint8_t* data, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(fd, data + off, n - off);
        if (w <= 0) return false;
        off += static_cast<size_t>(w);
    }
    return true;
}


bool readExact(int fd, uint8_t* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = ::read(fd, buf + off, n - off);
        if (r <= 0) return false;
        off += static_cast<size_t>(r);
    }
    return true;
}


bool findHeader(int fd) {
    uint8_t b = 0;
    while (true) {
        ssize_t r = ::read(fd, &b, 1);
        if (r <= 0) return false;


        if (b == 0xAA) {
            uint8_t b2 = 0;
            r = ::read(fd, &b2, 1);
            if (r <= 0) return false;
            if (b2 == 0x55) return true;
        }
    }
}


void renderMap(SDL_Renderer* ren, const std::vector<Point>& pts) {
    const double cx = WIN_W / 2.0;
    const double cy = WIN_H / 2.0;


    const double scale = (std::min(WIN_W, WIN_H) / 2.0 - 20.0) / MAX_RANGE_M;


    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);


    SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
    SDL_RenderDrawLine(ren, (int)cx, 0, (int)cx, WIN_H);
    SDL_RenderDrawLine(ren, 0, (int)cy, WIN_W, (int)cy);


    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    for (int r = -3; r <= 3; ++r) {
        for (int c = -3; c <= 3; ++c) {
            SDL_RenderDrawPoint(ren,
                                (int)std::lround(cx + c),
                                (int)std::lround(cy + r));
        }
    }


    for (const auto& p : pts) {
        if (p.dist < MIN_VALID_M || p.dist > MAX_RANGE_M) continue;


        int px = (int)std::lround(cx + p.x * scale);
        int py = (int)std::lround(cy - p.y * scale);


        if (px < 0 || px >= WIN_W || py < 0 || py >= WIN_H) continue;


        if (p.dist < NEAR_MAX_M) {
            SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
        } else if (p.dist < MID_MAX_M) {
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
        } else {
            SDL_SetRenderDrawColor(ren, 0, 128, 255, 255);
        }


        SDL_RenderDrawPoint(ren, px, py);
    }


    SDL_RenderPresent(ren);
}


int main(int argc, char** argv) {
    const char* port = (argc > 1) ? argv[1] : "/dev/ttyUSB0";


    std::cout << "Opening LiDAR on port: " << port << "\n";


    int fd = openSerial(port);
    if (fd < 0) {
        std::cerr << "Failed to open serial port: " << port << "\n";
        return 1;
    }


    const uint8_t STOP_CMD[2] = {0xA5, 0x65};
    const uint8_t SCAN_CMD[2] = {0xA5, 0x60};


    auto cleanupAndExit = [&](SDL_Window* win, SDL_Renderer* ren, int code) {
        writeAll(fd, STOP_CMD, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ::close(fd);


        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        return code;
    };


    writeAll(fd, STOP_CMD, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tcflush(fd, TCIOFLUSH);


    if (!writeAll(fd, SCAN_CMD, 2)) {
        std::cerr << "Failed to send SCAN command\n";
        ::close(fd);
        return 1;
    }


    std::this_thread::sleep_for(std::chrono::milliseconds(100));


    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        ::close(fd);
        return 1;
    }


    SDL_Window* win = SDL_CreateWindow(
        "Mini LiDAR map",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n";
        SDL_Quit();
        ::close(fd);
        return 1;
    }


    SDL_Renderer* ren = SDL_CreateRenderer(
        win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );


    if (!ren) {
        std::cerr << "Accelerated renderer failed: " << SDL_GetError()
                  << "\nTrying software renderer...\n";


        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }


    if (!ren) {
        std::cerr << "SDL_CreateRenderer error: " << SDL_GetError() << "\n";
        return cleanupAndExit(win, nullptr, 1);
    }


    std::cout << "Mini LiDAR SDL map running.\n";
    std::cout << "Press ESC or close the window to exit.\n";


    bool quit = false;
    std::vector<Point> points;
    points.reserve(8000);
    int packetCount = 0;


    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                quit = true;
        }


        if (!findHeader(fd)) {
            std::cerr << "Lost LiDAR header\n";
            break;
        }


        uint8_t hdr[1 + 1 + 2 + 2 + 2];
        if (!readExact(fd, hdr, sizeof(hdr))) {
            std::cerr << "Failed to read packet header\n";
            break;
        }


        uint8_t  CT  = hdr[0];
        uint8_t  LSN = hdr[1];
        uint16_t FSA = hdr[2] | (hdr[3] << 8);
        uint16_t LSA = hdr[4] | (hdr[5] << 8);
        uint16_t CS  = hdr[6] | (hdr[7] << 8);


        (void)CT;
        (void)CS;


        if (LSN == 0) continue;


        std::vector<uint8_t> sampleBytes(LSN * 3);
        if (!readExact(fd, sampleBytes.data(), sampleBytes.size())) {
            std::cerr << "Failed to read sample bytes\n";
            break;
        }


        double angle_start = (FSA >> 1) / 64.0;
        double angle_end   = (LSA >> 1) / 64.0;


        double diff = angle_end - angle_start;
        if (diff < 0.0) diff += 360.0;


        double step = (LSN > 1) ? (diff / (LSN - 1)) : 0.0;


        for (int i = 0; i < LSN; ++i) {
            uint8_t s0 = sampleBytes[i * 3 + 0];
            uint8_t s1 = sampleBytes[i * 3 + 1];
            uint8_t s2 = sampleBytes[i * 3 + 2];


            (void)s0; // intensity כרגע לא בשימוש


            uint16_t dist_mm = (static_cast<uint16_t>(s2) << 6) |
                               (static_cast<uint16_t>(s1) >> 2);


            if (dist_mm == 0) continue;


            double dist_m = dist_mm / 1000.0;
            double angle_deg = angle_start + step * i;
            double angle_rad = (angle_deg + ANGLE_OFFSET_DEG) * PI / 180.0;


            double y = dist_m * std::cos(angle_rad);
            double x = dist_m * std::sin(angle_rad);


            points.push_back(Point{x, y, dist_m});
        }


        ++packetCount;


        if (packetCount % 2 == 0) {
            renderMap(ren, points);


            if (points.size() > 8000) {
                points.erase(points.begin(), points.begin() + points.size() / 2);
            }
        }
    }


    return cleanupAndExit(win, ren, 0);
}




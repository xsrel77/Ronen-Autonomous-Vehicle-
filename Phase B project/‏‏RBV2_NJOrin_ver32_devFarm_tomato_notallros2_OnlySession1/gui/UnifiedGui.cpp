#include "gui/UnifiedGui.h"
#include "perception/ObjectDetector.h"
#include "lidar/MiniLidarSDL.h"


#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>


namespace {
constexpr int WIN_W = 1400;
constexpr int WIN_H = 760;
constexpr int PAD   = 12;
constexpr int DEFAULT_DASHBOARD_H = 430;
constexpr int DEFAULT_BODY_H = 430;
constexpr int SCROLLBAR_W = 12;


static std::string normalizeLabel(std::string s) {
    for (char& c : s) {
        if (c == '_') c = ' ';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}


static int bestClassConfidencePct(const DetectionSnapshot& snap,
                                  int classId,
                                  const std::string& fallbackLabel) {
    if (!snap.valid || !snap.isFresh) {
        return 0;
    }


    const std::string wanted = normalizeLabel(fallbackLabel);
    float best = 0.0f;


    for (const auto& d : snap.detections) {
        if (!d.valid) continue;


        const std::string got = normalizeLabel(d.label);
        if (d.classId == classId || got == wanted) {
            if (d.confidence > best) {
                best = d.confidence;
            }
        }
    }


    return static_cast<int>(std::lround(best * 100.0f));
}


static const std::map<char, std::array<uint8_t, 7>>& font5x7() {
    static const std::map<char, std::array<uint8_t, 7>> font = {
        {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
        {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
        {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
        {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
        {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
        {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}},
        {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'I',{0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
        {'J',{0x07,0x02,0x02,0x02,0x12,0x12,0x0C}},
        {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
        {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
        {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
        {'N',{0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
        {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
        {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
        {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
        {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
        {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
        {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
        {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
        {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
        {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
        {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
        {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
        {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
        {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
        {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
        {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
        {'5',{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
        {'6',{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}},
        {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
        {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
        {'9',{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
        {':',{0x00,0x04,0x04,0x00,0x04,0x04,0x00}},
        {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
        {'/',{0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
        {'.',{0x00,0x00,0x00,0x00,0x00,0x06,0x06}},
        {'%',{0x19,0x19,0x02,0x04,0x08,0x13,0x13}},
        {' ',{0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
    };
    return font;
}
}


UnifiedGui::UnifiedGui() = default;
UnifiedGui::~UnifiedGui() { close(); }


void UnifiedGui::setSources(ObjectDetector* detector, MiniLidarSDL* lidar) {
    detector_ = detector;
    lidar_ = lidar;
}


void UnifiedGui::setRobotState(const RobotState& state) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    robotState_ = state;


    const std::uint64_t ts = state.detections.frame.timestampMs;
    if (ts > 0 && lastFrameTimestampMs_ > 0 && ts > lastFrameTimestampMs_) {
        const double dtMs = static_cast<double>(ts - lastFrameTimestampMs_);
        if (dtMs > 0.1) {
            const double instFps = 1000.0 / dtMs;
            if (cameraFps_ <= 0.0) cameraFps_ = instFps;
            else cameraFps_ = cameraFps_ * 0.85 + instFps * 0.15;
        }
    }
    if (ts > 0) lastFrameTimestampMs_ = ts;
}


bool UnifiedGui::open() {
    if (open_) return true;


    if (!window_) {
        window_ = SDL_CreateWindow(
            "RaspBot Unified GUI",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            WIN_W,
            WIN_H,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );
        if (!window_) {
            std::cerr << "[gui] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
            return false;
        }
        windowId_ = SDL_GetWindowID(window_);
    }


    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(
            window_, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
        );
        if (!renderer_) {
            std::cerr << "[gui] Accelerated renderer failed: " << SDL_GetError()
                      << "\n[gui] Trying software renderer...\n";
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer_) {
            std::cerr << "[gui] SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            windowId_ = 0;
            return false;
        }
    }


    open_ = true;
    return true;
}


void UnifiedGui::close() {
    open_ = false;


    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    windowId_ = 0;
}


bool UnifiedGui::isOpen() const {
    return open_;
}


void UnifiedGui::handleSDLEvent(const SDL_Event& e) {
    if (!open_ || windowId_ == 0) return;


    if (e.type == SDL_WINDOWEVENT &&
        e.window.windowID == windowId_ &&
        e.window.event == SDL_WINDOWEVENT_CLOSE) {
        close();
        return;
    }


    if (e.type == SDL_KEYDOWN && e.key.windowID == windowId_) {
        if (e.key.keysym.sym == SDLK_ESCAPE) {
            close();
            return;
        }


        if (e.key.keysym.sym == SDLK_PAGEUP) {
            scrollOffsetY_ -= 80;
            if (scrollOffsetY_ < 0) scrollOffsetY_ = 0;
            return;
        }


        if (e.key.keysym.sym == SDLK_PAGEDOWN) {
            scrollOffsetY_ += 80;
            const int maxScroll = std::max(0, contentHeight_ - viewportH_);
            if (scrollOffsetY_ > maxScroll) scrollOffsetY_ = maxScroll;
            return;
        }
    }


    if (e.type == SDL_MOUSEWHEEL) {
        scrollOffsetY_ -= e.wheel.y * 32;
        if (scrollOffsetY_ < 0) scrollOffsetY_ = 0;


        const int maxScroll = std::max(0, contentHeight_ - viewportH_);
        if (scrollOffsetY_ > maxScroll) scrollOffsetY_ = maxScroll;
    }
}


void UnifiedGui::drawBackground() {
    SDL_SetRenderDrawColor(renderer_, 18, 18, 22, 255);
    SDL_RenderClear(renderer_);
}


void UnifiedGui::drawScrollBar(int viewportW, int viewportH) {
    if (contentHeight_ <= viewportH || viewportW <= 0 || viewportH <= 0) {
        return;
    }


    const int trackMargin = 8;
    SDL_Rect track{
        viewportW - SCROLLBAR_W - trackMargin,
        trackMargin,
        SCROLLBAR_W,
        viewportH - trackMargin * 2
    };


    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 18, 18, 24, 190);
    SDL_RenderFillRect(renderer_, &track);
    SDL_SetRenderDrawColor(renderer_, 90, 90, 105, 255);
    SDL_RenderDrawRect(renderer_, &track);


    const double visibleRatio = static_cast<double>(viewportH) / static_cast<double>(contentHeight_);
    int thumbH = static_cast<int>(std::lround(track.h * visibleRatio));
    thumbH = std::max(36, std::min(track.h, thumbH));


    const int maxScroll = std::max(1, contentHeight_ - viewportH);
    const int travel = std::max(0, track.h - thumbH);
    const int thumbY = track.y + static_cast<int>(std::lround(
        static_cast<double>(scrollOffsetY_) / static_cast<double>(maxScroll) * travel
    ));


    SDL_Rect thumb{ track.x + 2, thumbY + 2, track.w - 4, std::max(8, thumbH - 4) };
    SDL_SetRenderDrawColor(renderer_, 120, 180, 255, 235);
    SDL_RenderFillRect(renderer_, &thumb);
    SDL_SetRenderDrawColor(renderer_, 220, 230, 255, 255);
    SDL_RenderDrawRect(renderer_, &thumb);
}


void UnifiedGui::drawDashboardCard(const SDL_Rect& rect, const std::string& title) {
    SDL_SetRenderDrawColor(renderer_, 14, 14, 18, 220);
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawColor(renderer_, 100, 100, 115, 255);
    SDL_RenderDrawRect(renderer_, &rect);


    SDL_Rect titleBar{ rect.x, rect.y, rect.w, 22 };
    SDL_SetRenderDrawColor(renderer_, 24, 24, 30, 255);
    SDL_RenderFillRect(renderer_, &titleBar);
    SDL_SetRenderDrawColor(renderer_, 120, 120, 135, 255);
    SDL_RenderDrawRect(renderer_, &titleBar);


    drawText(rect.x + 8, rect.y + 5, title, SDL_Color{225,225,230,255}, 2);
}


int UnifiedGui::countClassDetections(const DetectionSnapshot& snap,
                                     int classId,
                                     const std::string& fallbackLabel) const {
    if (!snap.valid || !snap.isFresh) return 0;


    int count = 0;
    const std::string wanted = normalizeLabel(
        detector_ ? detector_->getClassDisplayName(classId, fallbackLabel) : fallbackLabel
    );


    for (const auto& d : snap.detections) {
        if (!d.valid) continue;


        const std::string got = normalizeLabel(
            detector_ ? detector_->getClassDisplayName(d.classId, d.label) : d.label
        );


        if (d.classId == classId || got == wanted) ++count;
    }


    return count;
}


void UnifiedGui::drawSingleDetectionTag(int x, int y, int w,
                                        const std::string& label,
                                        SDL_Color color,
                                        int count,
                                        int bestPct) {
    SDL_Rect tag{ x, y, w, 12 };


    SDL_SetRenderDrawColor(renderer_, 32, 32, 40, 255);
    SDL_RenderFillRect(renderer_, &tag);
    SDL_SetRenderDrawColor(renderer_, 120, 120, 135, 255);
    SDL_RenderDrawRect(renderer_, &tag);


    SDL_Rect swatch{ x + 3, y + 2, 7, 7 };
    if (count > 0) SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    else SDL_SetRenderDrawColor(renderer_, 70, 70, 80, 255);


    SDL_RenderFillRect(renderer_, &swatch);
    SDL_SetRenderDrawColor(renderer_, 200, 200, 210, 255);
    SDL_RenderDrawRect(renderer_, &swatch);


    drawText(x + 14, y + 3, label, SDL_Color{225,225,230,255}, 1);


    std::string rightText = std::to_string(count);
    if (count > 0 && bestPct > 0) {
        rightText = std::to_string(bestPct) + "%" + std::to_string(count);
    }


    const int textX = std::max(x + 14, x + w - static_cast<int>(rightText.size()) * 6 - 4);
    drawText(textX, y + 3, rightText, SDL_Color{120,180,255,255}, 1);
}





void UnifiedGui::drawDetectionTags(int x, int y, int w, const DetectionSnapshot& snap) {
    drawText(x, y, "TAGS", SDL_Color{220,220,225,255}, 1);


    const std::string p0 = detector_ ? detector_->getClassDisplayName(0, "PERSON") : "PERSON";
    const std::string p1 = detector_ ? detector_->getClassDisplayName(1, "HEALTHY_LETTUCE") : "HEALTHY_LETTUCE";
    const std::string p2 = detector_ ? detector_->getClassDisplayName(2, "SICK_LETTUCE") : "SICK_LETTUCE";
    const std::string p3 = detector_ ? detector_->getClassDisplayName(3, "WEED") : "WEED";


    const int count0 = countClassDetections(snap, 0, p0);
    const int count1 = countClassDetections(snap, 1, p1);
    const int count2 = countClassDetections(snap, 2, p2);
    const int count3 = countClassDetections(snap, 3, p3);


    const int pct0 = bestClassConfidencePct(snap, 0, p0);
    const int pct1 = bestClassConfidencePct(snap, 1, p1);
    const int pct2 = bestClassConfidencePct(snap, 2, p2);
    const int pct3 = bestClassConfidencePct(snap, 3, p3);


    drawSingleDetectionTag(x, y + 10, w, normalizeLabel(p0), SDL_Color{60,220,90,255}, count0, pct0);
    drawSingleDetectionTag(x, y + 24, w, normalizeLabel(p1), SDL_Color{80,200,120,255}, count1, pct1);
    drawSingleDetectionTag(x, y + 38, w, normalizeLabel(p2), SDL_Color{255,180,60,255}, count2, pct2);
    drawSingleDetectionTag(x, y + 52, w, normalizeLabel(p3), SDL_Color{255,70,150,255}, count3, pct3);
}




void UnifiedGui::drawNavStatusBox(const SDL_Rect& rect, const RobotState& rs) {
    PanelPaintContext ctx;
    ctx.renderer = renderer_;
    ctx.drawText = [this](int x, int y, const std::string& text, SDL_Color color, int scale) {
        this->drawText(x, y, text, color, scale);
    };
    ctx.trimNumber = [](double value, int precision, int maxLen) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        std::string s = oss.str();
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (static_cast<int>(s.size()) > maxLen) s = s.substr(0, maxLen);
        return s;
    };


    navStatusPanel_.draw(rect, rs, ctx);
}


void UnifiedGui::drawM5DashboardBox(const SDL_Rect& rect, const RobotState& rs) {
    PanelPaintContext ctx;
    ctx.renderer = renderer_;
    ctx.drawText = [this](int x, int y, const std::string& text, SDL_Color color, int scale) {
        this->drawText(x, y, text, color, scale);
    };
    ctx.trimNumber = [](double value, int precision, int maxLen) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        std::string s = oss.str();
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (static_cast<int>(s.size()) > maxLen) s = s.substr(0, maxLen);
        return s;
    };


    m5SensorsPanel_.draw(rect, rs, ctx);
}


void UnifiedGui::drawTopDashboard(const SDL_Rect& rect) {
    RobotState rs{};
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        rs = robotState_;
    }


    SDL_SetRenderDrawColor(renderer_, 12, 12, 16, 255);
    SDL_RenderFillRect(renderer_, &rect);
    SDL_SetRenderDrawColor(renderer_, 90, 90, 100, 255);
    SDL_RenderDrawRect(renderer_, &rect);


    const int outerPad = 10;
    const int gap = 10;
    const int row1H = 200;
    const int row2H = rect.h - (outerPad * 2) - row1H - gap;


    const int innerX = rect.x + outerPad;
    const int innerY = rect.y + outerPad;
    const int innerW = rect.w - outerPad * 2;


    const int row1Y = innerY;
    const int row2Y = row1Y + row1H + gap;


    const int fixedW1 = 240;
    const int fixedW2 = 230;
    const int fixedW3 = 210;
    const int fixedGaps = gap * 3;
    const int diagW = std::max(300, innerW - fixedW1 - fixedW2 - fixedW3 - fixedGaps);


    SDL_Rect card1{ innerX, row1Y, fixedW1, row1H };
    SDL_Rect card2{ card1.x + card1.w + gap, row1Y, fixedW2, row1H };
    SDL_Rect card3{ card2.x + card2.w + gap, row1Y, fixedW3, row1H };
    SDL_Rect card4{ card3.x + card3.w + gap, row1Y, diagW, row1H };


    drawDashboardCard(card1, "DETECTIONS");
    drawDashboardCard(card2, "DRIVE");
    drawDashboardCard(card3, "LIDAR PROXIMITY");
    drawDashboardCard(card4, "DIAGNOSTICS");


    PanelPaintContext ctx;
    ctx.renderer = renderer_;
    ctx.drawText = [this](int x, int y, const std::string& text, SDL_Color color, int scale) {
        this->drawText(x, y, text, color, scale);
    };
    ctx.trimNumber = [](double value, int precision, int maxLen) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        std::string s = oss.str();
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (static_cast<int>(s.size()) > maxLen) s = s.substr(0, maxLen);
        return s;
    };


    detectionsPanel_.draw(card1, rs, ctx);
    drawDetectionTags(card1.x + 10, card1.y + 122, card1.w - 20, rs.detections);



    driveStatusPanel_.draw(card2, rs, ctx);
    lidarProximityPanel_.draw(card3, rs, ctx);
    diagnosticsPanel_.draw(card4, rs, ctx);


    const int row2LeftW = std::max(360, static_cast<int>(innerW * 0.46));
    const int row2RightW = innerW - row2LeftW - gap;


    SDL_Rect m5Card{ innerX, row2Y, row2LeftW, row2H };
    SDL_Rect navCard{ m5Card.x + m5Card.w + gap, row2Y, row2RightW, row2H };


    drawDashboardCard(m5Card, "M5STICK SENSORS");
    drawDashboardCard(navCard, "NAV STATUS");


    SDL_Rect m5Box{
        m5Card.x + 8,
        m5Card.y + 24,
        m5Card.w - 16,
        m5Card.h - 32
    };


    SDL_Rect navBox{
        navCard.x + 8,
        navCard.y + 24,
        navCard.w - 16,
        navCard.h - 32
    };


    drawM5DashboardBox(m5Box, rs);
    drawNavStatusBox(navBox, rs);
}


void UnifiedGui::drawGlyph5x7(int x, int y, const uint8_t glyph[7], SDL_Color color, int scale) {
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);


    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (glyph[row] & (1 << (4 - col))) {
                SDL_Rect px{ x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer_, &px);
            }
        }
    }
}


void UnifiedGui::drawChar(int x, int y, char c, SDL_Color color, int scale) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = font5x7().find(c);
    if (it == font5x7().end()) it = font5x7().find(' ');
    drawGlyph5x7(x, y, it->second.data(), color, scale);
}


void UnifiedGui::drawText(int x, int y, const std::string& text, SDL_Color color, int scale) {
    int cursorX = x;
    for (char c : text) {
        drawChar(cursorX, y, c, color, scale);
        cursorX += 6 * scale;
    }
}




bool UnifiedGui::ros2MapScreenToDisplayMeters(int screenX, int screenY, double& mapX, double& mapY) const {
    if (!open_ || !window_) return false;

    RobotState rs{};
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        rs = robotState_;
    }

    const auto& map = rs.ros2Map;
    if (!map.loaded || !map.valid || map.width <= 0 || map.height <= 0 || map.resolutionM <= 0.0) {
        return false;
    }

    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);

    const int dashboardH = std::max(DEFAULT_DASHBOARD_H, h / 2 + 40);
    const int bodyYLogical = PAD + dashboardH + PAD;
    const int bodyH = std::max(DEFAULT_BODY_H, h - 220);
    const int bodyW = w - PAD * 2;
    const int halfW = (bodyW - PAD) / 2;
    const int bodyY = bodyYLogical - scrollOffsetY_;

    SDL_Rect rightRect{ PAD + halfW + PAD, bodyY, halfW, bodyH };
    SDL_Rect content{ rightRect.x + 4, rightRect.y + 26 + 4, rightRect.w - 8, rightRect.h - 26 - 8 };

    // This must match LidarViewPanel::draw() when a ROS2 map is loaded.
    const int sideW = std::min(170, std::max(125, content.w / 4));
    SDL_Rect mapRect{content.x + sideW + 6, content.y + 4,
                     content.w - sideW - 10, content.h - 8};

    const double mapWm = static_cast<double>(map.width) * map.resolutionM;
    const double mapHm = static_cast<double>(map.height) * map.resolutionM;
    if (mapRect.w <= 0 || mapRect.h <= 0 || mapWm <= 0.0 || mapHm <= 0.0) {
        return false;
    }

    const double scale = std::max(1.0, std::min(static_cast<double>(mapRect.w) / mapWm,
                                               static_cast<double>(mapRect.h) / mapHm));
    const double usedW = mapWm * scale;
    const double usedH = mapHm * scale;
    const double offX = static_cast<double>(mapRect.x) + (static_cast<double>(mapRect.w) - usedW) * 0.5;
    const double offY = static_cast<double>(mapRect.y) + (static_cast<double>(mapRect.h) - usedH) * 0.5;

    if (static_cast<double>(screenX) < offX || static_cast<double>(screenX) >= offX + usedW ||
        static_cast<double>(screenY) < offY || static_cast<double>(screenY) >= offY + usedH) {
        return false;
    }

    mapX = (static_cast<double>(screenX) - offX) / scale;
    mapY = (static_cast<double>(screenY) - offY) / scale;
    return true;
}

void UnifiedGui::render() {
    if (!open_ || !renderer_) return;


    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    viewportW_ = w;
    viewportH_ = h;


    const int dashboardH = std::max(DEFAULT_DASHBOARD_H, h / 2 + 40);
    const int bodyYLogical = PAD + dashboardH + PAD;
    const int bodyH = std::max(DEFAULT_BODY_H, h - 220);
    const int bodyW = w - PAD * 2;
    const int halfW = (bodyW - PAD) / 2;


    contentHeight_ = bodyYLogical + bodyH + PAD;


    const int maxScroll = std::max(0, contentHeight_ - h);
    if (scrollOffsetY_ > maxScroll) scrollOffsetY_ = maxScroll;
    if (scrollOffsetY_ < 0) scrollOffsetY_ = 0;


    drawBackground();


    SDL_Rect contentClip{0, 0, std::max(0, w - (SCROLLBAR_W + 18)), h};
    SDL_RenderSetClipRect(renderer_, &contentClip);


    const SDL_Rect dashboardRect{
        PAD,
        PAD - scrollOffsetY_,
        bodyW,
        dashboardH
    };


    const int bodyY = bodyYLogical - scrollOffsetY_;


    SDL_Rect leftRect{ PAD, bodyY, halfW, bodyH };
    SDL_Rect rightRect{ PAD + halfW + PAD, bodyY, halfW, bodyH };


    RobotState rs{};
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        rs = robotState_;
    }


    PanelPaintContext ctx;
    ctx.renderer = renderer_;
    ctx.drawText = [this](int x, int y, const std::string& text, SDL_Color color, int scale) {
        this->drawText(x, y, text, color, scale);
    };
    ctx.trimNumber = [](double value, int precision, int maxLen) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        std::string s = oss.str();
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (static_cast<int>(s.size()) > maxLen) s = s.substr(0, maxLen);
        return s;
    };


    drawTopDashboard(dashboardRect);
    cameraViewPanel_.draw(leftRect, renderer_, detector_, rs, cameraFps_, ctx);
    lidarViewPanel_.draw(rightRect, renderer_, lidar_, rs, ctx);


    SDL_RenderSetClipRect(renderer_, nullptr);
    drawScrollBar(w, h);
    SDL_RenderPresent(renderer_);
}




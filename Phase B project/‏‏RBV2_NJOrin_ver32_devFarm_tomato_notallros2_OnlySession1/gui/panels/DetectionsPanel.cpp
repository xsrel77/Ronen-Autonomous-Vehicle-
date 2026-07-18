#include "gui/panels/DetectionsPanel.h"


#include <algorithm>
#include <cctype>


namespace {
static std::string normalizeLabel(std::string s) {
    for (char& c : s) {
        if (c == '_') c = ' ';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}
}


int DetectionsPanel::activeDetectionCount(const DetectionSnapshot& snap) {
    if (!snap.valid || !snap.isFresh) return 0;


    int count = 0;
    for (const auto& d : snap.detections) {
        if (d.valid && !d.weak) ++count;
    }
    return count;
}


bool DetectionsPanel::hasAnyActiveDetection(const DetectionSnapshot& snap) {
    return activeDetectionCount(snap) > 0;
}


bool DetectionsPanel::snapshotHasTomato(const DetectionSnapshot& snap) {
    if (!snap.valid || !snap.isFresh) return false;


    for (const auto& d : snap.detections) {
        if (!d.valid || d.weak) continue;
        const std::string label = normalizeLabel(d.label);
        if (label == "ERIPE BUNCH" ||
            label == "RIPE" ||
            label == "UNRIPE" ||
            label == "UNRIPE BUNCH") {
            return true;
        }
    }
    return false;
}


void DetectionsPanel::drawDetectionDots(const PanelPaintContext& ctx,
                                        int x, int y, int count, bool hasTomato,
                                        const std::string& label) const {
    ctx.drawText(x, y, label, SDL_Color{220,220,225,255}, 2);


    const int maxDots = 8;
    const int shown = std::min(count, maxDots);


    for (int i = 0; i < maxDots; ++i) {
        SDL_Rect dot{ x + i * 14, y + 16, 10, 10 };


        if (i < shown) {
            if (hasTomato) SDL_SetRenderDrawColor(ctx.renderer, 60, 220, 90, 255);
            else           SDL_SetRenderDrawColor(ctx.renderer, 80, 150, 255, 255);
        } else {
            SDL_SetRenderDrawColor(ctx.renderer, 55, 55, 65, 255);
        }


        SDL_RenderFillRect(ctx.renderer, &dot);
        SDL_SetRenderDrawColor(ctx.renderer, 180, 180, 190, 255);
        SDL_RenderDrawRect(ctx.renderer, &dot);
    }
}


void DetectionsPanel::draw(const SDL_Rect& rect,
                           const RobotState& rs,
                           const PanelPaintContext& ctx) const {
    const int detCount = activeDetectionCount(rs.detections);
    const bool hasTomato = snapshotHasTomato(rs.detections);
    const bool fresh = rs.health.detectionsFresh;
    const bool anyDet = hasAnyActiveDetection(rs.detections);


    ctx.drawText(rect.x + 10, rect.y + 30, "COUNT", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 72, rect.y + 30,
                 std::to_string(detCount),
                 SDL_Color{120,180,255,255}, 2);


    ctx.drawText(rect.x + 10, rect.y + 48, "TOMATO", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 84, rect.y + 48,
                 hasTomato ? "YES" : "NO",
                 hasTomato ? SDL_Color{120,255,170,255} : SDL_Color{180,180,190,255}, 2);


    ctx.drawText(rect.x + 10, rect.y + 66, "STATE", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 72, rect.y + 66,
                 anyDet ? "ACTIVE" : "NONE",
                 anyDet ? SDL_Color{255,210,80,255} : SDL_Color{180,180,190,255}, 2);


    ctx.drawText(rect.x + 10, rect.y + 84, "FRESH", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 72, rect.y + 84,
                 fresh ? "YES" : "NO",
                 fresh ? SDL_Color{120,255,170,255} : SDL_Color{255,170,120,255}, 2);


    drawDetectionDots(ctx, rect.x + 10, rect.y + 106, detCount, hasTomato, "TOMATOES");
}






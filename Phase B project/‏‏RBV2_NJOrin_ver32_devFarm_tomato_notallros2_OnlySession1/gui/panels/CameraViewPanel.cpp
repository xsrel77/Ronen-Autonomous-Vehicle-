#include "gui/panels/CameraViewPanel.h"
#include "perception/ObjectDetector.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
constexpr int TITLE_H = 26;

static int activeDetectionCount(const DetectionSnapshot& snap) {
    if (!snap.valid || !snap.isFresh) return 0;
    int count = 0;
    for (const auto& d : snap.detections) {
        if (d.valid && !d.weak && !d.displaySuppressed) ++count;
    }
    return count;
}

static bool hasAnyActiveDetection(const DetectionSnapshot& snap) {
    return activeDetectionCount(snap) > 0;
}

static std::string formatFps(double fps) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << fps;
    return oss.str();
}

static bool isRipeBunch(const Detection& d) {
    return d.classId == 0;  // eripe bunch
}

static bool isRipeSingle(const Detection& d) {
    return d.classId == 1;  // ripe
}

static bool isUnripeSingle(const Detection& d) {
    return d.classId == 2;  // unripe
}

static bool isUnripeBunch(const Detection& d) {
    return d.classId == 3;  // unripe bunch
}

static bool isBunchDetection(const Detection& d) {
    return d.heuristic || isRipeBunch(d) || isUnripeBunch(d) || d.classId == 4;
}

static SDL_Color colorForDetection(const Detection& d) {
    if (d.heuristic || d.sourceType == "heuristic" || d.policyStatus == "heuristic") {
        return SDL_Color{255, 230, 0, 255};       // Ver32 Heuristics: yellow
    }
    if (d.policyStatus == "noise") {
        return SDL_Color{145, 145, 145, 255};     // Ver32 Noise: gray
    }
    if (d.weak) return SDL_Color{0, 191, 255, 255};      // Weak: cyan-blue

    if (isRipeBunch(d))    return SDL_Color{185, 70, 255, 255};  // ripe bunch: purple box
    if (isRipeSingle(d))   return SDL_Color{255, 0, 0, 255};     // ripe tomato: red mask/box
    if (isUnripeSingle(d)) return SDL_Color{0, 230, 60, 255};    // unripe tomato: green mask/box
    if (isUnripeBunch(d))  return SDL_Color{255, 165, 0, 255};   // unripe bunch: orange box

    return SDL_Color{0, 255, 255, 255};
}

static int boxThicknessForDetection(const Detection& d) {
    if (d.heuristic || d.sourceType == "heuristic" || d.policyStatus == "heuristic") return 5;
    if (d.weak) return 1;
    if (isBunchDetection(d)) return 5;   // bunches should be easy to see from the robot screen
    return 2;
}

static void drawThickRect(SDL_Renderer* renderer, const SDL_Rect& box, int thickness) {
    if (box.w <= 0 || box.h <= 0) return;
    thickness = std::max(1, thickness);

    for (int i = 0; i < thickness; ++i) {
        SDL_Rect r{box.x - i, box.y - i, box.w + 2 * i, box.h + 2 * i};
        if (r.w > 0 && r.h > 0) {
            SDL_RenderDrawRect(renderer, &r);
        }
    }
}

static std::string labelForDetection(const Detection& d) {
    if (d.heuristic || d.sourceType == "heuristic" || d.policyStatus == "heuristic") {
        std::ostringstream oss;
        oss << d.label;
        if (d.childCount > 0) oss << " c" << d.childCount;
        if (d.weightedChildCount > 0.0f) oss << " w" << std::fixed << std::setprecision(1) << d.weightedChildCount;
        if (d.anchorBunchConfidence > 0.0f) oss << " a" << std::fixed << std::setprecision(2) << d.anchorBunchConfidence;
        return oss.str();
    }
    if (d.weak) {
        if (d.policyStatus == "noise") return d.rejectReason.empty() ? "noise" : "noise " + d.rejectReason;
        if (!d.rejectReason.empty()) return d.rejectReason;
        return "weak/noise";
    }

    std::ostringstream oss;
    if (d.trackId > 0) {
        oss << "#" << d.trackId << " ";
    }
    if (d.roiPass) {
        oss << "ROI ";
    }
    if (d.policyStatus == "review") oss << "REVIEW ";
    if (d.policyStatus == "color_corrected") oss << "CORR ";
    oss << d.label << " " << std::fixed << std::setprecision(2) << d.confidence;
    if (d.roiGroupSize > 0) {
        oss << " g" << d.roiGroupSize;
    }
    if (d.trackHits > 0) {
        oss << " h" << d.trackHits;
    }
    return oss.str();
}

static bool shouldDrawMaskForDetection(const Detection& d, const ObjectDetector::Config& cfg) {
    if (d.heuristic || d.sourceType == "heuristic") return false;
    if (d.weak) return false;
    if (d.roiPass) return false;  // ROI masks are crop-local; draw the ROI box only.
    if (d.mask.empty() || d.maskWidth <= 0 || d.maskHeight <= 0) return false;

    // For greenhouse debugging, bunches are intentionally shown as thick boxes only.
    // Single tomatoes keep the segmentation overlay:
    // ripe   -> red mask
    // unripe -> green mask
    if (isBunchDetection(d)) return false;
    return cfg.drawMasksForSingles && (isRipeSingle(d) || isUnripeSingle(d));
}

static void drawLowResMaskOverlay(SDL_Renderer* renderer,
                                  const SDL_Rect& dst,
                                  const Detection& d,
                                  const SDL_Color& c) {
    if (d.mask.empty() || d.maskWidth <= 0 || d.maskHeight <= 0) return;

    const double cellW = static_cast<double>(dst.w) / static_cast<double>(d.maskWidth);
    const double cellH = static_cast<double>(dst.h) / static_cast<double>(d.maskHeight);

    // Ver26: draw the source-space mask densely.
    // Previous sparse drawing looked like the segmentation covered only part of the tomato.
    const int step = 1;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 105);

    for (int my = 0; my < d.maskHeight; my += step) {
        for (int mx = 0; mx < d.maskWidth; mx += step) {
            const size_t idx = static_cast<size_t>(my) * static_cast<size_t>(d.maskWidth) +
                               static_cast<size_t>(mx);
            if (idx >= d.mask.size() || d.mask[idx] == 0) continue;

            SDL_Rect r;
            r.x = dst.x + static_cast<int>(std::lround(mx * cellW));
            r.y = dst.y + static_cast<int>(std::lround(my * cellH));
            r.w = std::max(1, static_cast<int>(std::ceil(cellW * step)));
            r.h = std::max(1, static_cast<int>(std::ceil(cellH * step)));
            SDL_RenderFillRect(renderer, &r);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

}

CameraViewPanel::~CameraViewPanel() {
    if (cameraTexture_) {
        SDL_DestroyTexture(cameraTexture_);
        cameraTexture_ = nullptr;
    }
}

void CameraViewPanel::drawPanelFrame(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    SDL_SetRenderDrawColor(renderer, 90, 90, 100, 255);
    SDL_RenderDrawRect(renderer, &rect);
}

void CameraViewPanel::drawPanelTitle(SDL_Renderer* renderer,
                                     const SDL_Rect& rect,
                                     const PanelPaintContext& ctx,
                                     const std::string& title) const {
    SDL_Rect titleBar{ rect.x, rect.y, rect.w, TITLE_H };
    SDL_SetRenderDrawColor(renderer, 26, 26, 34, 255);
    SDL_RenderFillRect(renderer, &titleBar);
    SDL_SetRenderDrawColor(renderer, 120, 120, 135, 255);
    SDL_RenderDrawRect(renderer, &titleBar);
    ctx.drawText(rect.x + 10, rect.y + 6, title, SDL_Color{220,220,225,255}, 2);
}

void CameraViewPanel::drawTargetLockFrame(SDL_Renderer* renderer,
                                          const SDL_Rect& rect,
                                          bool trackingEnabled,
                                          bool targetSelected) const {
    if (!trackingEnabled) return;

    if (targetSelected) SDL_SetRenderDrawColor(renderer, 0, 255, 120, 255);
    else                SDL_SetRenderDrawColor(renderer, 255, 200, 0, 255);

    const int len = 24;

    SDL_RenderDrawLine(renderer, rect.x, rect.y, rect.x + len, rect.y);
    SDL_RenderDrawLine(renderer, rect.x, rect.y, rect.x, rect.y + len);

    SDL_RenderDrawLine(renderer, rect.x + rect.w - len, rect.y, rect.x + rect.w, rect.y);
    SDL_RenderDrawLine(renderer, rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + len);

    SDL_RenderDrawLine(renderer, rect.x, rect.y + rect.h - len, rect.x, rect.y + rect.h);
    SDL_RenderDrawLine(renderer, rect.x, rect.y + rect.h, rect.x + len, rect.y + rect.h);

    SDL_RenderDrawLine(renderer, rect.x + rect.w - len, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h);
    SDL_RenderDrawLine(renderer, rect.x + rect.w, rect.y + rect.h - len, rect.x + rect.w, rect.y + rect.h);
}

void CameraViewPanel::drawCornerStats(SDL_Renderer* renderer,
                                      const SDL_Rect& rect,
                                      const RobotState& rs,
                                      double cameraFps,
                                      const PanelPaintContext& ctx) const {
    const SDL_Rect stats{ rect.x + 8, rect.y + 8, 200, 122 };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 8, 8, 12, 180);
    SDL_RenderFillRect(renderer, &stats);
    SDL_SetRenderDrawColor(renderer, 180, 180, 190, 255);
    SDL_RenderDrawRect(renderer, &stats);

    const bool camOn = rs.health.detectorRunning;
    const bool detOn = hasAnyActiveDetection(rs.detections);

    ctx.drawText(stats.x + 8, stats.y + 8,  "CAM STATS", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(stats.x + 8, stats.y + 26, camOn ? "CAM ON" : "CAM OFF", SDL_Color{120,220,140,255}, 2);
    ctx.drawText(stats.x + 8, stats.y + 44, detOn ? "DET ON" : "DET OFF", SDL_Color{255,210,80,255}, 2);
    ctx.drawText(stats.x + 8, stats.y + 62, "DETS", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(stats.x + 58, stats.y + 62, std::to_string(activeDetectionCount(rs.detections)),
                 SDL_Color{120,180,255,255}, 2);
    ctx.drawText(stats.x + 8, stats.y + 80, "FPS", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(stats.x + 46, stats.y + 80, formatFps(cameraFps),
                 SDL_Color{120,220,255,255}, 2);

    std::ostringstream zoomText;
    zoomText << std::fixed << std::setprecision(2) << rs.cameraServo.digitalZoom << "x";
    ctx.drawText(stats.x + 8, stats.y + 98, "ZOOM", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(stats.x + 64, stats.y + 98, zoomText.str(),
                 SDL_Color{255,210,80,255}, 2);
}

void CameraViewPanel::draw(const SDL_Rect& rect,
                           SDL_Renderer* renderer,
                           ObjectDetector* detector,
                           const RobotState& rs,
                           double cameraFps,
                           const PanelPaintContext& ctx) {
    SDL_SetRenderDrawColor(renderer, 28, 28, 35, 255);
    SDL_RenderFillRect(renderer, &rect);
    drawPanelFrame(renderer, rect);
    drawPanelTitle(renderer, rect, ctx, "CAMERA");

    SDL_Rect content{ rect.x + 4, rect.y + TITLE_H + 4, rect.w - 8, rect.h - TITLE_H - 8 };
    SDL_SetRenderDrawColor(renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(renderer, &content);

    if (!detector) return;

    ObjectDetector::Snapshot snap;
    if (!detector->getLatestSnapshot(snap) || !snap.valid ||
        snap.frame.width <= 0 || snap.frame.height <= 0 ||
        snap.frameBytes.empty()) {
        return;
    }

    if (!cameraTexture_ || texW_ != snap.frame.width || texH_ != snap.frame.height) {
        if (cameraTexture_) {
            SDL_DestroyTexture(cameraTexture_);
            cameraTexture_ = nullptr;
        }

        cameraTexture_ = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_BGR24,
            SDL_TEXTUREACCESS_STREAMING,
            snap.frame.width,
            snap.frame.height
        );

        if (!cameraTexture_) {
            std::cerr << "[gui] SDL_CreateTexture failed: " << SDL_GetError() << "\n";
            texW_ = texH_ = 0;
            return;
        }

        texW_ = snap.frame.width;
        texH_ = snap.frame.height;
    }

    if (SDL_UpdateTexture(cameraTexture_, nullptr, snap.frameBytes.data(), snap.frame.strideBytes) != 0) {
        std::cerr << "[gui] SDL_UpdateTexture failed: " << SDL_GetError() << "\n";
        return;
    }

    SDL_Rect dst = content;

    const double srcAspect = static_cast<double>(snap.frame.width) / static_cast<double>(snap.frame.height);
    const double dstAspect = static_cast<double>(content.w) / static_cast<double>(content.h);

    if (srcAspect > dstAspect) {
        dst.h = static_cast<int>(content.w / srcAspect);
        dst.y = content.y;
    } else {
        dst.w = static_cast<int>(content.h * srcAspect);
        dst.x = content.x + (content.w - dst.w) / 2;
        dst.y = content.y;
    }

    SDL_RenderCopy(renderer, cameraTexture_, nullptr, &dst);

    const double sx = static_cast<double>(dst.w) / static_cast<double>(snap.frame.width);
    const double sy = static_cast<double>(dst.h) / static_cast<double>(snap.frame.height);

    const auto& detCfg = detector->config();

    for (const auto& d : snap.detections) {
        if (!d.valid || d.displaySuppressed) continue;

        const SDL_Color c = colorForDetection(d);
        if (shouldDrawMaskForDetection(d, detCfg)) {
            drawLowResMaskOverlay(renderer, dst, d, c);
        }

        SDL_Rect box;
        box.x = dst.x + static_cast<int>(std::lround(d.x * sx));
        box.y = dst.y + static_cast<int>(std::lround(d.y * sy));
        box.w = static_cast<int>(std::lround(d.w * sx));
        box.h = static_cast<int>(std::lround(d.h * sy));

        if (box.w <= 1 || box.h <= 1) continue;

        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        drawThickRect(renderer, box, boxThicknessForDetection(d));

        const int textX = std::max(dst.x, box.x);
        const int textY = std::max(dst.y + 2, box.y - 14);
        ctx.drawText(textX, textY, labelForDetection(d), c, 1);
    }

    const int baseCx = dst.x + dst.w / 2;
    const int baseCy = dst.y + dst.h / 2;

    const float normX = std::max(-1.0f, std::min(rs.tracking.targetOffsetX / 320.0f, 1.0f));
    const float normY = std::max(-1.0f, std::min(rs.tracking.targetOffsetY / 240.0f, 1.0f));

    const int dynamicCx = baseCx + static_cast<int>(std::lround(normX * 40.0f));
    const int dynamicCy = baseCy + static_cast<int>(std::lround(normY * 30.0f));

    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawLine(renderer, dynamicCx - 12, dynamicCy, dynamicCx + 12, dynamicCy);
    SDL_RenderDrawLine(renderer, dynamicCx, dynamicCy - 12, dynamicCx, dynamicCy + 12);

    drawTargetLockFrame(renderer, dst, rs.tracking.trackingEnabled, rs.tracking.targetSelected);
    drawCornerStats(renderer, dst, rs, cameraFps, ctx);
}


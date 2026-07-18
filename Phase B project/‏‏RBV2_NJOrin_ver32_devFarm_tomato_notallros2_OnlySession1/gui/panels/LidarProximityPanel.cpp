#include "gui/panels/LidarProximityPanel.h"


#include <algorithm>
#include <cmath>


SDL_Color LidarProximityPanel::colorForDistance(double meters) {
    if (meters < 0.0)  return SDL_Color{90, 90, 100, 255};
    if (meters < 0.50) return SDL_Color{255, 60, 60, 255};
    if (meters < 1.50) return SDL_Color{255, 210, 60, 255};
    return SDL_Color{60, 180, 255, 255};
}


void LidarProximityPanel::drawLidarSectorBar(const PanelPaintContext& ctx,
                                             int x, int y, int w, int h,
                                             double valueMeters, double maxMeters,
                                             const std::string& label) const {
    ctx.drawText(x, y, label, SDL_Color{220,220,225,255}, 2);


    SDL_Rect frame{ x, y + 16, w, h };
    SDL_SetRenderDrawColor(ctx.renderer, 40, 40, 48, 220);
    SDL_RenderFillRect(ctx.renderer, &frame);
    SDL_SetRenderDrawColor(ctx.renderer, 180, 180, 190, 255);
    SDL_RenderDrawRect(ctx.renderer, &frame);


    if (valueMeters < 0.0 || maxMeters <= 0.0) return;


    const double clamped = std::max(0.0, std::min(valueMeters, maxMeters));
    const double ratio = clamped / maxMeters;
    const int fillW = static_cast<int>(std::lround(ratio * frame.w));


    SDL_Rect fill{ frame.x + 1, frame.y + 1, std::max(0, fillW - 2), frame.h - 2 };
    const SDL_Color c = colorForDistance(clamped);


    SDL_SetRenderDrawColor(ctx.renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ctx.renderer, &fill);
}


void LidarProximityPanel::draw(const SDL_Rect& rect,
                               const RobotState& rs,
                               const PanelPaintContext& ctx) const {
    drawLidarSectorBar(ctx, rect.x + 10, rect.y + 30, rect.w - 20, 12, rs.lidarSummary.frontMinMeters, 3.0, "FRONT");
    drawLidarSectorBar(ctx, rect.x + 10, rect.y + 56, rect.w - 20, 12, rs.lidarSummary.leftMinMeters,  3.0, "LEFT");
    drawLidarSectorBar(ctx, rect.x + 10, rect.y + 82, rect.w - 20, 12, rs.lidarSummary.rightMinMeters, 3.0, "RIGHT");
    drawLidarSectorBar(ctx, rect.x + 10, rect.y + 108, rect.w - 20, 12, rs.lidarSummary.rearMinMeters,  3.0, "REAR");


    ctx.drawText(rect.x + 10, rect.y + 142, "LIDAR", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 72, rect.y + 142,
                 rs.health.lidarFresh ? "FRESH" : "STALE",
                 rs.health.lidarFresh ? SDL_Color{120,255,170,255}
                                      : SDL_Color{255,170,120,255}, 2);
}




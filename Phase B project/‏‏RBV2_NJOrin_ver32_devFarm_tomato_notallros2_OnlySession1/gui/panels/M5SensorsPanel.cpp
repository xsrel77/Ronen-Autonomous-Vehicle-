#include "gui/panels/M5SensorsPanel.h"


#include <SDL2/SDL.h>
#include <string>


namespace {
static std::string yn(bool v) {
    return v ? "ON" : "OFF";
}
}


void M5SensorsPanel::draw(const SDL_Rect& rect,
                          const RobotState& rs,
                          const PanelPaintContext& ctx) const {
    SDL_SetRenderDrawColor(ctx.renderer, 10, 10, 14, 255);
    SDL_RenderFillRect(ctx.renderer, &rect);


    SDL_SetRenderDrawColor(ctx.renderer, 80, 80, 96, 255);
    SDL_RenderDrawRect(ctx.renderer, &rect);


    const int x = rect.x + 8;
    int y = rect.y + 8;


    ctx.drawText(x, y, "LINK: " + yn(rs.m5stick.connected), SDL_Color{220,220,225,255}, 2);
    ctx.drawText(x + 128, y, "IMU: " + yn(rs.m5stick.imuEnabled && rs.m5stick.imu.hwOk), SDL_Color{220,220,225,255}, 2);
    ctx.drawText(x + 248, y, "ENV: " + yn(rs.m5stick.envEnabled && rs.m5stick.env.hwOk), SDL_Color{220,220,225,255}, 2);
    y += 24;


    ctx.drawText(x, y,
                 "T:" + ctx.trimNumber(rs.m5stick.env.tempC, 1, 6) + "C",
                 SDL_Color{255,210,80,255}, 2);
    ctx.drawText(x + 92, y,
                 "H:" + ctx.trimNumber(rs.m5stick.env.humidityPct, 1, 6) + "%",
                 SDL_Color{120,255,170,255}, 2);
    y += 18;


    ctx.drawText(x, y,
                 "P:" + ctx.trimNumber(rs.m5stick.env.pressureHpa, 1, 8),
                 SDL_Color{120,180,255,255}, 2);
    y += 18;


    ctx.drawText(x, y,
                 "GAS:" + ctx.trimNumber(rs.m5stick.env.gasKohm, 1, 8),
                 SDL_Color{255,210,80,255}, 2);
    y += 18;


    ctx.drawText(x, y,
                 "A:" +
                 ctx.trimNumber(rs.m5stick.imu.ax, 2, 6) + " " +
                 ctx.trimNumber(rs.m5stick.imu.ay, 2, 6) + " " +
                 ctx.trimNumber(rs.m5stick.imu.az, 2, 6),
                 SDL_Color{220,220,225,255}, 2);
    y += 18;


    ctx.drawText(x, y,
                 "G:" +
                 ctx.trimNumber(rs.m5stick.imu.gx, 2, 6) + " " +
                 ctx.trimNumber(rs.m5stick.imu.gy, 2, 6) + " " +
                 ctx.trimNumber(rs.m5stick.imu.gz, 2, 6),
                 SDL_Color{255,210,80,255}, 2);
    y += 22;


    ctx.drawText(x, y,
                 "IMU: " + std::string(rs.m5stick.imu.isFresh ? "FRESH" : "STALE"),
                 rs.m5stick.imu.isFresh ? SDL_Color{120,255,170,255}
                                        : SDL_Color{255,170,120,255}, 2);
    y += 18;


    ctx.drawText(x, y,
                 "ENV: " + std::string(rs.m5stick.env.isFresh ? "FRESH" : "STALE"),
                 rs.m5stick.env.isFresh ? SDL_Color{120,255,170,255}
                                        : SDL_Color{255,170,120,255}, 2);
}




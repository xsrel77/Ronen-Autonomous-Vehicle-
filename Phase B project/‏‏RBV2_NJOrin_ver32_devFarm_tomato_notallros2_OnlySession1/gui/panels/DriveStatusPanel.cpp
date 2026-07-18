#include "gui/panels/DriveStatusPanel.h"


#include <algorithm>
#include <cmath>


void DriveStatusPanel::drawSignedBar(const PanelPaintContext& ctx,
                                     int x, int y, int w, int h,
                                     float value, float maxAbsValue,
                                     const std::string& label) const {
    ctx.drawText(x, y, label, SDL_Color{220,220,225,255}, 2);


    SDL_Rect frame{ x, y + 16, w, h };
    SDL_SetRenderDrawColor(ctx.renderer, 40, 40, 48, 220);
    SDL_RenderFillRect(ctx.renderer, &frame);
    SDL_SetRenderDrawColor(ctx.renderer, 180, 180, 190, 255);
    SDL_RenderDrawRect(ctx.renderer, &frame);


    const int midX = frame.x + frame.w / 2;
    SDL_RenderDrawLine(ctx.renderer, midX, frame.y, midX, frame.y + frame.h);


    if (maxAbsValue <= 0.0f) return;


    const float clamped = std::max(-maxAbsValue, std::min(value, maxAbsValue));
    const int fill = static_cast<int>(std::lround((std::abs(clamped) / maxAbsValue) * (frame.w / 2.0f)));


    SDL_Rect bar{};
    if (clamped >= 0.0f) {
        bar = SDL_Rect{ midX, frame.y + 2, fill, frame.h - 4 };
        SDL_SetRenderDrawColor(ctx.renderer, 60, 160, 255, 255);
    } else {
        bar = SDL_Rect{ midX - fill, frame.y + 2, fill, frame.h - 4 };
        SDL_SetRenderDrawColor(ctx.renderer, 255, 170, 60, 255);
    }


    SDL_RenderFillRect(ctx.renderer, &bar);
}


std::string DriveStatusPanel::navMotionToString(NavMotionState state) {
    switch (state) {
        case NavMotionState::Forward: return "FORWARD";
        case NavMotionState::Reverse: return "REVERSE";
        case NavMotionState::Stop:
        default:
            return "STOP";
    }
}


std::string DriveStatusPanel::navTurnToString(NavTurnState state) {
    switch (state) {
        case NavTurnState::Left: return "LEFT";
        case NavTurnState::Right: return "RIGHT";
        case NavTurnState::Straight:
        default:
            return "STRAIGHT";
    }
}


void DriveStatusPanel::draw(const SDL_Rect& rect,
                            const RobotState& rs,
                            const PanelPaintContext& ctx) const {
    drawSignedBar(ctx, rect.x + 10, rect.y + 42, rect.w - 20, 16,
                  rs.drive.currentForwardSpeed, 255.0f, "FORWARD");


    drawSignedBar(ctx, rect.x + 10, rect.y + 82, rect.w - 20, 16,
                  rs.drive.currentSteeringSpeed, 255.0f, "STEERING");


    ctx.drawText(rect.x + 10, rect.y + 126, "MOTION", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 10, rect.y + 144,
                 navMotionToString(rs.nav.motionState),
                 SDL_Color{120,180,255,255}, 2);


    ctx.drawText(rect.x + 10, rect.y + 164, "TURN", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 62, rect.y + 164,
                 navTurnToString(rs.nav.turnState),
                 SDL_Color{255,210,80,255}, 2);
}




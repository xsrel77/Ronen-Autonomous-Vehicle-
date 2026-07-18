#pragma once


#include <SDL2/SDL.h>
#include <string>


#include "core/SystemState.h"
#include "gui/panels/PanelPaintContext.h"


class DriveStatusPanel {
public:
    void draw(const SDL_Rect& rect, const RobotState& rs, const PanelPaintContext& ctx) const;


private:
    void drawSignedBar(const PanelPaintContext& ctx,
                       int x, int y, int w, int h,
                       float value, float maxAbsValue,
                       const std::string& label) const;


    static std::string navMotionToString(NavMotionState state);
    static std::string navTurnToString(NavTurnState state);
};




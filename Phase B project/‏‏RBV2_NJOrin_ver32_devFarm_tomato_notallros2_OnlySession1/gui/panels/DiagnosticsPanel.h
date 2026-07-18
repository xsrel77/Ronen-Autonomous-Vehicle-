#pragma once


#include <string>
#include <SDL2/SDL.h>


#include "core/SystemState.h"
#include "gui/panels/PanelPaintContext.h"


class DiagnosticsPanel {
public:
    void draw(const SDL_Rect& rect, const RobotState& rs, const PanelPaintContext& ctx) const;


private:
    void drawStatusIndicator(const PanelPaintContext& ctx,
                             int x, int y, int w, int h,
                             bool on, const std::string& label) const;
};




#pragma once


#include <SDL2/SDL.h>


#include "core/SystemState.h"
#include "gui/panels/NavStatusPanel.h"
#include "gui/panels/PanelPaintContext.h"


class M5SensorsPanel
{
public:
    void draw(const SDL_Rect& rect,
              const RobotState& rs,
              const PanelPaintContext& ctx) const;


private:
    NavStatusPanel navPanel_{};
};




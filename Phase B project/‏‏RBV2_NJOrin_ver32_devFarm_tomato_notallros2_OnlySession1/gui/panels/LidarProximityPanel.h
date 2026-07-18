#pragma once


#include <SDL2/SDL.h>
#include <string>
#include "core/SystemState.h"
#include "gui/panels/PanelPaintContext.h"


class LidarProximityPanel {
public:
    void draw(const SDL_Rect& rect, const RobotState& rs, const PanelPaintContext& ctx) const;


private:
    static SDL_Color colorForDistance(double meters);


    void drawLidarSectorBar(const PanelPaintContext& ctx,
                            int x, int y, int w, int h,
                            double valueMeters, double maxMeters,
                            const std::string& label) const;
};




#pragma once


#include <SDL2/SDL.h>
#include <vector>
#include "core/SystemState.h"
#include "gui/panels/PanelPaintContext.h"


class MiniLidarSDL;


class LidarViewPanel {
public:
    void draw(const SDL_Rect& rect,
              SDL_Renderer* renderer,
              MiniLidarSDL* lidar,
              const RobotState& rs,
              const PanelPaintContext& ctx) const;


private:
    void drawPanelFrame(SDL_Renderer* renderer, const SDL_Rect& rect) const;
    void drawPanelTitle(SDL_Renderer* renderer, const SDL_Rect& rect,
                        const PanelPaintContext& ctx,
                        const std::string& title) const;


    static SDL_Color colorForDistance(double meters);


    void drawDangerArcs(SDL_Renderer* renderer,
                        const SDL_Rect& rect,
                        double frontMin, double leftMin,
                        double rightMin, double rearMin) const;

    void drawRos2MapOverlay(SDL_Renderer* renderer,
                            const SDL_Rect& content,
                            MiniLidarSDL* lidar,
                            const RobotState& rs,
                            const PanelPaintContext& ctx) const;
};




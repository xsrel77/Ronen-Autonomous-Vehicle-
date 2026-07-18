#pragma once


#include <SDL2/SDL.h>
#include "core/SystemState.h"
#include "gui/panels/PanelPaintContext.h"


class ObjectDetector;


class CameraViewPanel {
public:
    ~CameraViewPanel();


    void draw(const SDL_Rect& rect,
              SDL_Renderer* renderer,
              ObjectDetector* detector,
              const RobotState& rs,
              double cameraFps,
              const PanelPaintContext& ctx);


private:
    void drawPanelFrame(SDL_Renderer* renderer, const SDL_Rect& rect) const;
    void drawPanelTitle(SDL_Renderer* renderer, const SDL_Rect& rect,
                        const PanelPaintContext& ctx,
                        const std::string& title) const;


    void drawTargetLockFrame(SDL_Renderer* renderer,
                             const SDL_Rect& rect,
                             bool trackingEnabled,
                             bool targetSelected) const;


    void drawCornerStats(SDL_Renderer* renderer,
                         const SDL_Rect& rect,
                         const RobotState& rs,
                         double cameraFps,
                         const PanelPaintContext& ctx) const;


private:
    SDL_Texture* cameraTexture_ = nullptr;
    int texW_ = 0;
    int texH_ = 0;
};




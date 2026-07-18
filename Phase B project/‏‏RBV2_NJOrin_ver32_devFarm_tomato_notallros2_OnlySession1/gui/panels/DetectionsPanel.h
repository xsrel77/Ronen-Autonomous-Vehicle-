#pragma once


#include <SDL2/SDL.h>
#include <string>


#include "core/SystemState.h"
#include "gui/panels/PanelPaintContext.h"


class DetectionsPanel {
public:
    void draw(const SDL_Rect& rect, const RobotState& rs, const PanelPaintContext& ctx) const;


private:
    static int activeDetectionCount(const DetectionSnapshot& snap);
    static bool hasAnyActiveDetection(const DetectionSnapshot& snap);
    static bool snapshotHasTomato(const DetectionSnapshot& snap);


    void drawDetectionDots(const PanelPaintContext& ctx,
                           int x, int y, int count, bool hasTomato,
                           const std::string& label) const;
};






#include "gui/panels/DiagnosticsPanel.h"


void DiagnosticsPanel::drawStatusIndicator(const PanelPaintContext& ctx,
                                           int x, int y, int w, int h,
                                           bool on, const std::string& label) const {
    ctx.drawText(x, y, label, SDL_Color{220,220,225,255}, 2);


    SDL_Rect r{ x, y + 16, w, h };
    if (on) SDL_SetRenderDrawColor(ctx.renderer, 40, 200, 80, 255);
    else    SDL_SetRenderDrawColor(ctx.renderer, 120, 40, 40, 255);


    SDL_RenderFillRect(ctx.renderer, &r);
    SDL_SetRenderDrawColor(ctx.renderer, 220, 220, 220, 255);
    SDL_RenderDrawRect(ctx.renderer, &r);
}


void DiagnosticsPanel::draw(const SDL_Rect& rect,
                            const RobotState& rs,
                            const PanelPaintContext& ctx) const {
    const BehaviorDecision& bd = rs.behavior;


    const bool camOn = rs.health.detectorRunning;
    const bool detOn = rs.health.detectionsFresh;
    const bool trkOn = rs.tracking.trackingEnabled &&
                       rs.tracking.targetSelected &&
                       rs.tracking.isFresh;
    const bool ldrOn = rs.health.lidarAvailable;
    const bool joyOn = rs.health.joystickConnected;
    const bool m5On  = rs.m5stick.connected;
    const bool ros2MapOn = rs.ros2Slam.mappingActive;
    const bool ros2MapLoaded = (rs.ros2Slam.latestMapLoaded && rs.ros2Slam.latestMapValid) || (rs.ros2Map.loaded && rs.ros2Map.valid);


    const bool odomOk    = rs.odom.valid;
    const bool yawOk     = rs.odom.yawValid;
    const bool odomFresh = rs.odom.isFresh;


    const int ledW = 16;
    const int ledH = 12;


    // Row 0: top device indicators
    const int row0Y = rect.y + 20;
    drawStatusIndicator(ctx, rect.x + 10,  row0Y, ledW, ledH, camOn, "CAM");
    drawStatusIndicator(ctx, rect.x + 64,  row0Y, ledW, ledH, detOn, "DET");
    drawStatusIndicator(ctx, rect.x + 118, row0Y, ledW, ledH, trkOn, "TRK");
    drawStatusIndicator(ctx, rect.x + 172, row0Y, ledW, ledH, ldrOn, "LDR");
    drawStatusIndicator(ctx, rect.x + 226, row0Y, ledW, ledH, joyOn, "JOY");
    drawStatusIndicator(ctx, rect.x + 280, row0Y, ledW, ledH, m5On,  "M5");


    // Status text block
    const int statusLabelY = rect.y + 58;
    const int statusValueY = rect.y + 76;


    ctx.drawText(rect.x + 10, statusLabelY, "STATUS", SDL_Color{220,220,225,255}, 2);
    ctx.drawText(rect.x + 10, statusValueY,
                 bd.statusText.empty() ? "IDLE" : bd.statusText,
                 SDL_Color{255,210,80,255}, 2);


    // Diagnostic rows with more spacing
    const int row1Y = rect.y + 104;
    const int row2Y = rect.y + 134;
    const int row3Y = rect.y + 164;
    const int row4Y = rect.y + 194;


    // Row 1
    drawStatusIndicator(ctx, rect.x + 10,  row1Y, ledW, ledH, bd.warningActive,  "WARN");
    drawStatusIndicator(ctx, rect.x + 92,  row1Y, ledW, ledH, bd.targetLost,     "TARGET LOST");
    drawStatusIndicator(ctx, rect.x + 230, row1Y, ledW, ledH, bd.obstacleClose,  "OBS");
    drawStatusIndicator(ctx, rect.x + 298, row1Y, ledW, ledH, bd.emergencyStop,  "E-STOP");
    drawStatusIndicator(ctx, rect.x + 386, row1Y, ledW, ledH, bd.m5stickWarning, "M5-WARN");


    // Row 2
    drawStatusIndicator(ctx, rect.x + 10,  row2Y, ledW, ledH, rs.health.detectionsFresh, "D-FRESH");
    drawStatusIndicator(ctx, rect.x + 126, row2Y, ledW, ledH, rs.health.trackingFresh,   "T-FRESH");
    drawStatusIndicator(ctx, rect.x + 242, row2Y, ledW, ledH, rs.health.driveFresh,      "DRV-FRESH");


    // Row 3 - ODOM
    drawStatusIndicator(ctx, rect.x + 10,  row3Y, ledW, ledH, odomOk,    "ODOM OK");
    drawStatusIndicator(ctx, rect.x + 126, row3Y, ledW, ledH, yawOk,     "YAW OK");
    drawStatusIndicator(ctx, rect.x + 242, row3Y, ledW, ledH, odomFresh, "ODOM FRESH");

    // Row 4 - ROS2 SLAM mapping state
    drawStatusIndicator(ctx, rect.x + 10,  row4Y, ledW, ledH, ros2MapOn,     "ROS2 MAP");
    drawStatusIndicator(ctx, rect.x + 138, row4Y, ledW, ledH, ros2MapLoaded, "MAP LOAD");

    if (!rs.ros2Slam.statusText.empty()) {
        ctx.drawText(rect.x + 10, row4Y + 22,
                     "ROS2: " + rs.ros2Slam.statusText,
                     ros2MapOn ? SDL_Color{120,255,170,255} : SDL_Color{220,220,225,255},
                     2);
    }

    if (rs.ros2Slam.latestMapLoaded) {
        ctx.drawText(rect.x + 10, row4Y + 42,
                     rs.ros2Map.loaded && rs.ros2Map.valid ? "R2 GUI MAP READY" : (rs.ros2Slam.latestMapValid ? "R2 MAP READY" : "R2 MAP LOAD FAILED"),
                     ros2MapLoaded ? SDL_Color{120,255,170,255} : SDL_Color{255,120,120,255},
                     2);
    } else if (!rs.ros2Slam.lastError.empty()) {
        ctx.drawText(rect.x + 10, row4Y + 42,
                     "ROS2 ERR: " + rs.ros2Slam.lastError,
                     SDL_Color{255,120,120,255},
                     2);
    }

    if (rs.ros2Slam.mappingActive || rs.ros2Slam.odomUdpPacketsSent > 0) {
        const std::string odomUdpText =
            "ROS2 ODOM UDP sent=" + std::to_string(rs.ros2Slam.odomUdpPacketsSent) +
            " x=" + std::to_string(rs.ros2Slam.odomUdpLastX).substr(0, 5) +
            " y=" + std::to_string(rs.ros2Slam.odomUdpLastY).substr(0, 5);
        ctx.drawText(rect.x + 10, row4Y + 62,
                     odomUdpText,
                     rs.ros2Slam.odomUdpPacketsSent > 0 ? SDL_Color{120,255,170,255} : SDL_Color{255,180,80,255},
                     2);
    }

    if (rs.ros2Map.loaded && rs.ros2Map.valid) {
        const std::string mapPoseText =
            "GUI MAP pose x=" + std::to_string(rs.ros2Map.robotX).substr(0, 5) +
            " y=" + std::to_string(rs.ros2Map.robotY).substr(0, 5) +
            " d=" + std::to_string(rs.ros2Map.robotDistanceM).substr(0, 5);
        ctx.drawText(rect.x + 10, row4Y + 82,
                     mapPoseText,
                     SDL_Color{130,255,170,255},
                     2);
    }


    if (bd.emergencyStop || rs.emergencyStop) {
        SDL_Rect emer{ rect.x + rect.w - 132, rect.y + 54, 112, 24 };
        SDL_SetRenderDrawColor(ctx.renderer, 220, 40, 40, 255);
        SDL_RenderFillRect(ctx.renderer, &emer);
        SDL_SetRenderDrawColor(ctx.renderer, 255, 220, 220, 255);
        SDL_RenderDrawRect(ctx.renderer, &emer);
        ctx.drawText(emer.x + 8, emer.y + 6, "EMERGENCY", SDL_Color{255,255,255,255}, 2);
    }
}




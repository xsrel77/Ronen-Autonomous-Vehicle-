#include "gui/panels/LidarViewPanel.h"
#include "lidar/MiniLidarSDL.h"




#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>




namespace {
constexpr int TITLE_H = 26;
constexpr double PI_D = 3.14159265358979323846;

// Ver30 map-overlay fix8:
// Draw the saved map.pgm exactly as stored. The operator selects the robot
// start point with a mouse click and adjusts the initial heading with keys
// 1/2. No fixed rotation, mirror, or offset is applied to the map image.


void drawSmallPoint(SDL_Renderer* renderer, int x, int y, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawPoint(renderer, x, y);
}


std::string fixedValue(double value, int precision)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}
}






void LidarViewPanel::drawPanelFrame(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    SDL_SetRenderDrawColor(renderer, 90, 90, 100, 255);
    SDL_RenderDrawRect(renderer, &rect);
}




void LidarViewPanel::drawPanelTitle(SDL_Renderer* renderer,
                                    const SDL_Rect& rect,
                                    const PanelPaintContext& ctx,
                                    const std::string& title) const {
    SDL_Rect titleBar{ rect.x, rect.y, rect.w, TITLE_H };
    SDL_SetRenderDrawColor(renderer, 26, 26, 34, 255);
    SDL_RenderFillRect(renderer, &titleBar);
    SDL_SetRenderDrawColor(renderer, 120, 120, 135, 255);
    SDL_RenderDrawRect(renderer, &titleBar);
    ctx.drawText(rect.x + 10, rect.y + 6, title, SDL_Color{220,220,225,255}, 2);
}




SDL_Color LidarViewPanel::colorForDistance(double meters) {
    if (meters < 0.0)  return SDL_Color{90, 90, 100, 255};
    if (meters < 0.50) return SDL_Color{255, 60, 60, 255};
    if (meters < 1.50) return SDL_Color{255, 210, 60, 255};
    return SDL_Color{60, 180, 255, 255};
}




SDL_Color colorForMapPoint(const DevFarmMapState& map, const DevFarmMapPoint& p) {
    if (map.occupancyGridEnabled) {
        if (p.hits >= 25) return SDL_Color{80, 230, 255, 255};
        if (p.hits >= 8)  return SDL_Color{60, 180, 255, 255};
        return SDL_Color{70, 120, 190, 255};
    }


    if (p.distanceM < 0.0)  return SDL_Color{90, 90, 100, 255};
    if (p.distanceM < 0.50) return SDL_Color{255, 60, 60, 255};
    if (p.distanceM < 1.50) return SDL_Color{255, 210, 60, 255};
    return SDL_Color{60, 180, 255, 255};
}




void LidarViewPanel::drawDangerArcs(SDL_Renderer* renderer,
                                    const SDL_Rect& rect,
                                    double frontMin, double leftMin,
                                    double rightMin, double rearMin) const {
    const int cx = rect.x + rect.w / 2;
    const int cy = rect.y + rect.h / 2;




    auto drawArc = [&](double startDeg, double endDeg, int radius, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int steps = 28;
        double prevX = 0.0;
        double prevY = 0.0;
        bool first = true;




        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(steps);
            const double angDeg = startDeg + (endDeg - startDeg) * t;
            const double angRad = angDeg * PI_D / 180.0;




            const double x = cx + std::cos(angRad) * radius;
            const double y = cy - std::sin(angRad) * radius;




            if (!first) {
                SDL_RenderDrawLine(renderer,
                                   static_cast<int>(std::lround(prevX)),
                                   static_cast<int>(std::lround(prevY)),
                                   static_cast<int>(std::lround(x)),
                                   static_cast<int>(std::lround(y)));
            }




            prevX = x;
            prevY = y;
            first = false;
        }
    };




    const int baseRadius = std::max(18, std::min(rect.w, rect.h) / 2 - 12);
    const int sideRadius = std::max(14, baseRadius - 8);
    const int rearRadius = std::max(12, baseRadius - 14);

    drawArc(60.0, 120.0,   baseRadius, colorForDistance(frontMin));
    drawArc(150.0, 210.0,  sideRadius, colorForDistance(leftMin));
    drawArc(-30.0, 30.0,   sideRadius, colorForDistance(rightMin));
    drawArc(-120.0, -60.0, rearRadius, colorForDistance(rearMin));
}





void LidarViewPanel::drawRos2MapOverlay(SDL_Renderer* renderer,
                                        const SDL_Rect& content,
                                        MiniLidarSDL* lidar,
                                        const RobotState& rs,
                                        const PanelPaintContext& ctx) const
{
    const auto& map = rs.ros2Map;
    if (!map.loaded || !map.valid || map.width <= 0 || map.height <= 0 || map.pixels.empty()) {
        ctx.drawText(content.x + 8, content.y + 8,
                     "ROS2 MAP NOT READY",
                     SDL_Color{255, 160, 80, 255}, 2);
        return;
    }

    // Fix8: draw map.pgm AS-IS, exactly like an image viewer would display it.
    // No rotation, no mirror, no fixed start offset. The robot start point is
    // chosen manually by clicking inside the map, and keys 1/2 rotate its
    // initial heading. Display-map coordinates are meters in image space:
    // X=image-right, Y=image-down.
    const double mapWm = static_cast<double>(map.width) * map.resolutionM;
    const double mapHm = static_cast<double>(map.height) * map.resolutionM;
    if (mapWm <= 0.0 || mapHm <= 0.0) {
        return;
    }

    const double scale = std::max(1.0, std::min(static_cast<double>(content.w) / mapWm,
                                               static_cast<double>(content.h) / mapHm));
    const double usedW = mapWm * scale;
    const double usedH = mapHm * scale;
    const double offX = static_cast<double>(content.x) + (static_cast<double>(content.w) - usedW) * 0.5;
    const double offY = static_cast<double>(content.y) + (static_cast<double>(content.h) - usedH) * 0.5;

    auto imageMetersToScreen = [&](double mx, double my, int& px, int& py) {
        px = static_cast<int>(std::lround(offX + mx * scale));
        py = static_cast<int>(std::lround(offY + my * scale));
    };

    SDL_Rect imageRect{static_cast<int>(std::lround(offX)),
                       static_cast<int>(std::lround(offY)),
                       static_cast<int>(std::lround(usedW)),
                       static_cast<int>(std::lround(usedH))};
    SDL_SetRenderDrawColor(renderer, 36, 38, 45, 255);
    SDL_RenderFillRect(renderer, &imageRect);

    const int cell = std::max(1, static_cast<int>(std::ceil(map.resolutionM * scale)));
    for (int my = 0; my < map.height; ++my) {
        for (int mx = 0; mx < map.width; ++mx) {
            const std::size_t idx = static_cast<std::size_t>(my) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(mx);
            if (idx >= map.pixels.size()) continue;
            const std::uint8_t v = map.pixels[idx];

            SDL_Color col{};
            if (v < 80) {
                col = SDL_Color{215, 225, 235, 255};     // occupied
            } else if (v > 220) {
                col = SDL_Color{34, 38, 44, 255};        // free
            } else {
                col = SDL_Color{72, 74, 86, 255};        // unknown
            }

            int px = 0;
            int py = 0;
            imageMetersToScreen((static_cast<double>(mx) + 0.5) * map.resolutionM,
                                (static_cast<double>(my) + 0.5) * map.resolutionM,
                                px,
                                py);

            if (px < content.x || px >= content.x + content.w || py < content.y || py >= content.y + content.h) {
                continue;
            }

            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            if (cell <= 1) {
                SDL_RenderDrawPoint(renderer, px, py);
            } else {
                SDL_Rect r{px - cell / 2, py - cell / 2, cell, cell};
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }

    // Draw manual start point if set.
    if (map.manualStartSet) {
        int sx = 0;
        int sy = 0;
        imageMetersToScreen(map.manualStartX, map.manualStartY, sx, sy);
        SDL_SetRenderDrawColor(renderer, 80, 255, 100, 255);
        SDL_RenderDrawLine(renderer, sx - 8, sy, sx + 8, sy);
        SDL_RenderDrawLine(renderer, sx, sy - 8, sx, sy + 8);
    }

    // Draw robot trail on top of the map.
    if (map.trail.size() >= 2) {
        SDL_SetRenderDrawColor(renderer, 90, 255, 130, 255);
        for (std::size_t i = 1; i < map.trail.size(); ++i) {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            imageMetersToScreen(map.trail[i - 1].xM, map.trail[i - 1].yM, x1, y1);
            imageMetersToScreen(map.trail[i].xM, map.trail[i].yM, x2, y2);
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }
    }

    // Draw live LiDAR scan in loaded-map display coordinates while Q is active.
    std::vector<MiniLidarSDL::LidarPoint> livePoints;
    const bool liveLidar = lidar && lidar->isRunning();
    if (liveLidar && map.manualStartSet && map.poseValid) {
        lidar->getLatestPoints(livePoints);
        const double yawRad = map.robotYawDeg * PI_D / 180.0;
        const double syaw = std::sin(yawRad);
        const double cyaw = std::cos(yawRad);

        SDL_SetRenderDrawColor(renderer, 255, 220, 70, 255);
        for (const auto& p : livePoints) {
            if (p.dist < 0.05 || p.dist > 6.0) continue;

            const double localForward = p.y;
            const double localRight = p.x;
            const double mx = map.robotX + localForward * syaw + localRight * cyaw;
            const double my = map.robotY - localForward * cyaw + localRight * syaw;

            int px = 0;
            int py = 0;
            imageMetersToScreen(mx, my, px, py);
            if (px < content.x || px >= content.x + content.w || py < content.y || py >= content.y + content.h) {
                continue;
            }
            SDL_RenderDrawPoint(renderer, px, py);
        }
    }

    // Draw robot marker and heading arrow.
    if (map.manualStartSet && map.poseValid) {
        int rx = 0;
        int ry = 0;
        imageMetersToScreen(map.robotX, map.robotY, rx, ry);
        const double yawRad = map.robotYawDeg * PI_D / 180.0;
        const double headingLenM = 0.35;
        int hx = rx;
        int hy = ry;
        // Fix9: the map pose/trail direction is correct, but the displayed
        // heading arrow was mirrored left/right. Flip only the arrow's lateral
        // component. Do not change map pixels, trail, odom, or live LiDAR scan.
        imageMetersToScreen(map.robotX - std::sin(yawRad) * headingLenM,
                            map.robotY - std::cos(yawRad) * headingLenM,
                            hx,
                            hy);

        SDL_SetRenderDrawColor(renderer, 255, 80, 80, 255);
        SDL_Rect body{rx - 5, ry - 5, 10, 10};
        SDL_RenderFillRect(renderer, &body);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, rx, ry, hx, hy);
    }

    ctx.drawText(content.x + 8, content.y + 8,
                 liveLidar ? "ROS2 MAP + LIVE Q" : "ROS2 MAP LOADED",
                 liveLidar ? SDL_Color{255, 230, 90, 255} : SDL_Color{130, 190, 255, 255},
                 2);
    ctx.drawText(content.x + 8, content.y + 28,
                 std::to_string(map.width) + "x" + std::to_string(map.height) +
                 " res " + fixedValue(map.resolutionM, 2) + "m",
                 SDL_Color{205, 215, 230, 255},
                 2);

    if (map.manualStartSet) {
        ctx.drawText(content.x + 8, content.y + 48,
                     "START X " + fixedValue(map.manualStartX, 2) +
                     " Y " + fixedValue(map.manualStartY, 2) +
                     " YAW " + fixedValue(map.manualStartYawDeg, 1),
                     SDL_Color{130, 255, 170, 255},
                     2);
        ctx.drawText(content.x + 8, content.y + 68,
                     "POSE X " + fixedValue(map.robotX, 2) +
                     " Y " + fixedValue(map.robotY, 2) +
                     " D " + fixedValue(map.robotDistanceM, 2),
                     SDL_Color{130, 255, 170, 255},
                     2);
        ctx.drawText(content.x + 8, content.y + 88,
                     "HDG " + fixedValue(map.robotYawDeg, 1) +
                     " TRAIL " + std::to_string(map.trailCount),
                     SDL_Color{130, 255, 170, 255},
                     2);
    } else {
        ctx.drawText(content.x + 8, content.y + 48,
                     "CLICK MAP TO SET START",
                     SDL_Color{255, 230, 90, 255},
                     2);
        ctx.drawText(content.x + 8, content.y + 68,
                     "KEYS 1/2 ROTATE START YAW",
                     SDL_Color{190, 200, 215, 255},
                     2);
    }

    const std::string mapName = map.sessionDir.empty() ? map.mapYaml : map.sessionDir;
    if (!mapName.empty()) {
        const std::string clipped = mapName.size() > 34 ? mapName.substr(mapName.size() - 34) : mapName;
        ctx.drawText(content.x + 8, content.y + content.h - 22,
                     clipped,
                     SDL_Color{190, 200, 215, 255},
                     2);
    }
}

void LidarViewPanel::draw(const SDL_Rect& rect,
                          SDL_Renderer* renderer,
                          MiniLidarSDL* lidar,
                          const RobotState& rs,
                          const PanelPaintContext& ctx) const {
    SDL_SetRenderDrawColor(renderer, 24, 24, 30, 255);
    SDL_RenderFillRect(renderer, &rect);
    drawPanelFrame(renderer, rect);
    drawPanelTitle(renderer, rect, ctx, "LIDAR");




    SDL_Rect content{ rect.x + 4, rect.y + TITLE_H + 4, rect.w - 8, rect.h - TITLE_H - 8 };
    SDL_SetRenderDrawColor(renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(renderer, &content);


    if (rs.ros2Map.loaded) {
        const int sideW = std::min(170, std::max(125, content.w / 4));
        SDL_Rect proximityRect{content.x + 4, content.y + 4, sideW - 8, content.h - 8};
        SDL_Rect mapRect{content.x + sideW + 6, content.y + 4,
                         content.w - sideW - 10, content.h - 8};

        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderFillRect(renderer, &proximityRect);
        SDL_SetRenderDrawColor(renderer, 65, 65, 78, 255);
        SDL_RenderDrawRect(renderer, &proximityRect);
        ctx.drawText(proximityRect.x + 8, proximityRect.y + 8,
                     "PROX", SDL_Color{255, 230, 90, 255}, 2);
        ctx.drawText(proximityRect.x + 8, proximityRect.y + 30,
                     "F " + fixedValue(rs.lidarSummary.frontMinMeters, 2),
                     colorForDistance(rs.lidarSummary.frontMinMeters), 2);
        ctx.drawText(proximityRect.x + 8, proximityRect.y + 50,
                     "L " + fixedValue(rs.lidarSummary.leftMinMeters, 2),
                     colorForDistance(rs.lidarSummary.leftMinMeters), 2);
        ctx.drawText(proximityRect.x + 8, proximityRect.y + 70,
                     "R " + fixedValue(rs.lidarSummary.rightMinMeters, 2),
                     colorForDistance(rs.lidarSummary.rightMinMeters), 2);
        ctx.drawText(proximityRect.x + 8, proximityRect.y + 90,
                     "B " + fixedValue(rs.lidarSummary.rearMinMeters, 2),
                     colorForDistance(rs.lidarSummary.rearMinMeters), 2);
        SDL_Rect arcRect{proximityRect.x + 8, proximityRect.y + 118,
                         proximityRect.w - 16, std::min(proximityRect.w - 16, proximityRect.h - 126)};
        if (arcRect.w > 40 && arcRect.h > 40) {
            drawDangerArcs(renderer, arcRect,
                           rs.lidarSummary.frontMinMeters,
                           rs.lidarSummary.leftMinMeters,
                           rs.lidarSummary.rightMinMeters,
                           rs.lidarSummary.rearMinMeters);
        }

        drawRos2MapOverlay(renderer, mapRect, lidar, rs, ctx);
        return;
    }


    const bool showDevFarmMap =
        rs.devFarmMap.valid &&
        (!rs.devFarmMap.previewPoints.empty() || !rs.devFarmMap.points.empty());




    std::vector<MiniLidarSDL::LidarPoint> points;
    if (!showDevFarmMap) {
        if (!lidar || !lidar->isRunning()) return;
        lidar->getLatestPoints(points);
        if (points.empty()) return;
    }




    const double maxRange = lidar ? lidar->maxRangeMeters() : 6.0;
    const double cx = content.x + content.w / 2.0;
    const double cy = content.y + content.h / 2.0;
    double scale = (std::min(content.w, content.h) / 2.0 - 20.0) / maxRange;




    if (showDevFarmMap) {
        const auto& mapPts = !rs.devFarmMap.previewPoints.empty()
            ? rs.devFarmMap.previewPoints
            : rs.devFarmMap.points;


        double maxAbs = 0.25;
        for (const auto& p : mapPts) {
            maxAbs = std::max(maxAbs, std::fabs(p.xM));
            maxAbs = std::max(maxAbs, std::fabs(p.yM));
        }


        scale = (std::min(content.w, content.h) / 2.0 - 20.0) / maxAbs;
    }




    SDL_SetRenderDrawColor(renderer, 60, 60, 70, 255);
    SDL_RenderDrawLine(renderer, static_cast<int>(cx), content.y,
                       static_cast<int>(cx), content.y + content.h);
    SDL_RenderDrawLine(renderer, content.x, static_cast<int>(cy),
                       content.x + content.w, static_cast<int>(cy));




    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int r = -3; r <= 3; ++r) {
        for (int c = -3; c <= 3; ++c) {
            SDL_RenderDrawPoint(renderer,
                                static_cast<int>(std::lround(cx + c)),
                                static_cast<int>(std::lround(cy + r)));
        }
    }




    if (showDevFarmMap) {
        const auto& mapPts = !rs.devFarmMap.previewPoints.empty()
            ? rs.devFarmMap.previewPoints
            : rs.devFarmMap.points;


        for (const auto& p : mapPts) {
            const int px = static_cast<int>(std::lround(cx + p.xM * scale));
            const int py = static_cast<int>(std::lround(cy - p.yM * scale));


            if (px < content.x || px >= content.x + content.w ||
                py < content.y || py >= content.y + content.h) {
                continue;
            }


            SDL_Color color = colorForMapPoint(rs.devFarmMap, p);
            drawSmallPoint(renderer, px, py, color);
        }


        const std::string mapTitle = rs.devFarmMap.recording
            ? std::string("MAP REC")
            : (rs.devFarmMap.loaded ? std::string("MAP LOADED") : std::string("MAP SAVED"));


        const SDL_Color mapTitleColor = rs.devFarmMap.recording
            ? SDL_Color{255, 210, 60, 255}
            : (rs.devFarmMap.loaded
                ? SDL_Color{130, 190, 255, 255}
                : SDL_Color{120, 220, 160, 255});


        ctx.drawText(content.x + 8, content.y + 8,
                     mapTitle,
                     mapTitleColor,
                     2);


        ctx.drawText(content.x + 8, content.y + 28,
                     "PTS " + std::to_string(rs.devFarmMap.pointsCount),
                     SDL_Color{210, 210, 220, 255},
                     2);


        ctx.drawText(content.x + 8, content.y + 48,
                     "VIEW " + std::to_string(rs.devFarmMap.previewPointsCount),
                     SDL_Color{180, 210, 255, 255},
                     2);


        int infoY = content.y + 68;


        if (rs.devFarmMap.occupancyGridEnabled) {
            ctx.drawText(content.x + 8, infoY,
                         "OCC GRID " + fixedValue(rs.devFarmMap.occupancyResolutionM * 100.0, 0) + "cm",
                         SDL_Color{120, 240, 200, 255},
                         2);
            infoY += 20;


            ctx.drawText(content.x + 8, infoY,
                         "OCC " + std::to_string(rs.devFarmMap.occupancyOccupiedCells) +
                         " FREE " + std::to_string(rs.devFarmMap.occupancyFreeCells),
                         SDL_Color{170, 230, 255, 255},
                         2);
            infoY += 20;


            ctx.drawText(content.x + 8, infoY,
                         "RAYS " + std::to_string(rs.devFarmMap.raysIntegratedCount),
                         SDL_Color{170, 230, 255, 255},
                         2);
            infoY += 20;
        }


        if (rs.devFarmMap.mappingGateOpen && rs.devFarmMap.poseValid) {
            ctx.drawText(content.x + 8, infoY,
                         "POSE " + (rs.devFarmMap.poseSource.empty() ? std::string("ODOM") : rs.devFarmMap.poseSource),
                         SDL_Color{150, 220, 255, 255},
                         2);
            infoY += 20;
            ctx.drawText(content.x + 8, infoY,
                         "X " + fixedValue(rs.devFarmMap.lastPoseXM, 2) +
                         " Y " + fixedValue(rs.devFarmMap.lastPoseYM, 2) +
                         " YAW " + fixedValue(rs.devFarmMap.lastPoseYawDeg, 1),
                         SDL_Color{150, 190, 255, 255},
                         2);
            infoY += 20;
        } else {
            ctx.drawText(content.x + 8, infoY,
                         "MAP WAIT POSE",
                         SDL_Color{255, 190, 80, 255},
                         2);
            infoY += 20;
            const std::string reason = rs.devFarmMap.mappingSkipReason.empty()
                ? std::string("pose_not_ready")
                : rs.devFarmMap.mappingSkipReason;
            ctx.drawText(content.x + 8, infoY,
                         reason.substr(0, 26),
                         SDL_Color{255, 190, 80, 255},
                         2);
            infoY += 20;
        }


        if (rs.devFarmMap.occupancyGridEnabled) {
            ctx.drawText(content.x + 8, infoY,
                         "WALL " + std::to_string(rs.devFarmMap.wallProtectionStopCount) +
                         " SNAP " + std::to_string(rs.devFarmMap.endpointSnapCount),
                         SDL_Color{170, 230, 255, 255},
                         2);
            infoY += 20;


            ctx.drawText(content.x + 8, infoY,
                         "SCAN OK " + std::to_string(rs.devFarmMap.scansIntegratedCount) +
                         " SKIP " + std::to_string(rs.devFarmMap.scansSkippedNoPoseCount +
                                                   rs.devFarmMap.scansSkippedBadPoseCount),
                         SDL_Color{170, 230, 255, 255},
                         2);
            infoY += 20;
        }


        if (rs.devFarmMap.slamLiteEnabled) {
            const SDL_Color slamColor = rs.devFarmMap.slamMatchAccepted
                ? SDL_Color{120, 240, 160, 255}
                : (rs.devFarmMap.slamMatchWeak
                    ? SDL_Color{255, 210, 80, 255}
                    : SDL_Color{160, 190, 255, 255});


            const std::string slamStatus = rs.devFarmMap.slamMatchAccepted
                ? "SLAM LITE OK "
                : (rs.devFarmMap.slamMatchWeak ? "SLAM LITE WEAK " : "SLAM LITE ON ");


            ctx.drawText(content.x + 8, infoY,
                         slamStatus + fixedValue(rs.devFarmMap.slamMatchScore * 100.0, 0) + "%",
                         slamColor,
                         2);
            infoY += 20;


            ctx.drawText(content.x + 8, infoY,
                         "DX " + fixedValue(rs.devFarmMap.slamDxM, 2) +
                         " DY " + fixedValue(rs.devFarmMap.slamDyM, 2) +
                         " YAW " + fixedValue(rs.devFarmMap.slamDYawDeg, 1),
                         SDL_Color{190, 210, 255, 255},
                         2);
        }
    } else {
        for (const auto& p : points) {
            if (p.dist < 0.05 || p.dist > maxRange) continue;




            const int px = static_cast<int>(std::lround(cx + p.x * scale));
            const int py = static_cast<int>(std::lround(cy - p.y * scale));




            if (px < content.x || px >= content.x + content.w ||
                py < content.y || py >= content.y + content.h) {
                continue;
            }




            if (p.dist < 0.50) SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            else if (p.dist < 1.50) SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            else SDL_SetRenderDrawColor(renderer, 0, 128, 255, 255);




            SDL_RenderDrawPoint(renderer, px, py);
        }
    }




    drawDangerArcs(renderer, content,
                   rs.lidarSummary.frontMinMeters,
                   rs.lidarSummary.leftMinMeters,
                   rs.lidarSummary.rightMinMeters,
                   rs.lidarSummary.rearMinMeters);
}












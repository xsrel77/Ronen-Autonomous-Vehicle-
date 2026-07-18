#pragma once


#include <SDL2/SDL.h>
#include <mutex>
#include <string>
#include <vector>


#include "core/SystemState.h"
#include "gui/panels/DetectionsPanel.h"
#include "gui/panels/DriveStatusPanel.h"
#include "gui/panels/LidarProximityPanel.h"
#include "gui/panels/DiagnosticsPanel.h"
#include "gui/panels/M5SensorsPanel.h"
#include "gui/panels/NavStatusPanel.h"
#include "gui/panels/PanelPaintContext.h"
#include "gui/panels/CameraViewPanel.h"
#include "gui/panels/LidarViewPanel.h"


class ObjectDetector;
class MiniLidarSDL;


class UnifiedGui {
public:
    UnifiedGui();
    ~UnifiedGui();


    UnifiedGui(const UnifiedGui&) = delete;
    UnifiedGui& operator=(const UnifiedGui&) = delete;


    void setSources(ObjectDetector* detector, MiniLidarSDL* lidar);
    void setRobotState(const RobotState& state);


    bool open();
    void close();
    bool isOpen() const;


    void handleSDLEvent(const SDL_Event& e);
    void render();

    // Returns true when the screen point is inside the currently displayed
    // ROS2 map image in the LIDAR panel. Output coordinates are display-map
    // meters: X=image-right, Y=image-down, using map.resolutionM.
    bool ros2MapScreenToDisplayMeters(int screenX, int screenY, double& mapX, double& mapY) const;


private:
    void drawBackground();
    void drawScrollBar(int viewportW, int viewportH);


    void drawTopDashboard(const SDL_Rect& rect);
    void drawDashboardCard(const SDL_Rect& rect, const std::string& title);


    void drawDetectionTags(int x, int y, int w, const DetectionSnapshot& snap);
    void drawSingleDetectionTag(int x, int y, int w,
                                const std::string& label,
                                SDL_Color color,
                                int count,
                                int bestPct);


    void drawNavStatusBox(const SDL_Rect& rect, const RobotState& rs);
    void drawM5DashboardBox(const SDL_Rect& rect, const RobotState& rs);


    void drawText(int x, int y, const std::string& text, SDL_Color color, int scale = 2);
    void drawChar(int x, int y, char c, SDL_Color color, int scale = 2);
    void drawGlyph5x7(int x, int y, const uint8_t glyph[7], SDL_Color color, int scale);


    int countClassDetections(const DetectionSnapshot& snap,
                             int classId,
                             const std::string& fallbackLabel) const;


private:
    ObjectDetector* detector_ = nullptr;
    MiniLidarSDL* lidar_ = nullptr;


    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    Uint32 windowId_ = 0;


    bool open_ = false;


    int scrollOffsetY_ = 0;
    int contentHeight_ = 0;
    int viewportW_ = 0;
    int viewportH_ = 0;


    mutable std::mutex stateMutex_;
    RobotState robotState_{};


    std::uint64_t lastFrameTimestampMs_ = 0;
    double cameraFps_ = 0.0;


    DetectionsPanel detectionsPanel_{};
    DriveStatusPanel driveStatusPanel_{};
    LidarProximityPanel lidarProximityPanel_{};
    DiagnosticsPanel diagnosticsPanel_{};
    M5SensorsPanel m5SensorsPanel_{};
    NavStatusPanel navStatusPanel_{};
    CameraViewPanel cameraViewPanel_{};
    LidarViewPanel lidarViewPanel_{};
};




#pragma once




#include "core/SystemState.h"
#include "core/LidarTypes.h"
#include "dev_farm/DevFarmScanMatcher.h"
#include "dev_farm/DevFarmOccupancyGridMapper.h"
#include "dev_farm/DevFarmPoseProvider.h"




#include <cstdint>
#include <string>




class DevFarmLidarMapper {
public:
    DevFarmLidarMapper() = default;
    ~DevFarmLidarMapper();




    bool start(std::uint64_t nowMs);
    bool stopAndSave(const std::string& reason, std::uint64_t nowMs);




    bool loadLatestMap(std::uint64_t nowMs);
    bool loadMap(const std::string& path, std::uint64_t nowMs);




    bool isRecording() const;
    const DevFarmMapState& getState() const;




    void updateFromSnapshot(const LidarSnapshot& snapshot,
                            const DevFarmPoseProvider::Pose& pose,
                            std::uint64_t nowMs);




    static std::string makeTimestampedMapPath();




private:
    bool saveJson(const std::string& path) const;
    bool saveLatestPathFile(const std::string& mapPath) const;
    void refreshPreviewPoints();
    void rebuildScanMatcherFromCurrentMap(std::uint64_t nowMs, bool force);




    static std::string latestPathFilePath();
    static std::string trim(const std::string& value);
    static bool extractDouble(const std::string& line, const std::string& key, double& outValue);
    static bool extractUint64(const std::string& line, const std::string& key, std::uint64_t& outValue);
    static bool parsePointLine(const std::string& line, DevFarmMapPoint& outPoint);
    static double normalizeAngleDeg(double angleDeg);




private:
    DevFarmMapState state_{};
    DevFarmScanMatcher scanMatcher_{};
    DevFarmOccupancyGridMapper occupancyGrid_{};
    std::uint64_t lastSampleMs_ = 0;
    std::uint64_t lastMatcherRebuildMs_ = 0;


    bool slamPoseInitialized_ = false;
    double lastRawPoseXM_ = 0.0;
    double lastRawPoseYM_ = 0.0;
    double lastRawPoseYawDeg_ = 0.0;
    double correctedPoseXM_ = 0.0;
    double correctedPoseYM_ = 0.0;
    double correctedPoseYawDeg_ = 0.0;




    // Occupancy grid map for long greenhouse scans.
    // Ray clearing marks all cells between robot and hit as free, and only the endpoint as occupied.
    static constexpr std::uint64_t kMinSampleIntervalMs = 100;
    static constexpr std::size_t kMaxStoredPoints = 3000000;
    static constexpr std::size_t kPointSampleStride = 10; // kept for legacy metadata/load compatibility
    static constexpr std::size_t kMaxPreviewPoints = 6000;
    static constexpr double kOccupancyResolutionM = 0.05;
    static constexpr std::uint64_t kMatcherRebuildIntervalMs = 650;
    static constexpr std::size_t kMatcherMaxCells = 25000;
    static constexpr double kSlamCorrectionGainXY = 0.55;
    static constexpr double kSlamCorrectionGainYaw = 0.45;
};
















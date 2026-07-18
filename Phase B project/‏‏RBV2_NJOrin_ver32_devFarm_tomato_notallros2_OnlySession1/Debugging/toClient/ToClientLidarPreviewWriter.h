#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/LidarTypes.h"
#include "core/SystemState.h"

/*
 * Lightweight LiDAR export for TO_CLIENT_JSON sessions.
 *
 * This class is intentionally separate from dev_farm/DevFarmLidarMapper.
 * It does not try to build a full SLAM/debug map. It writes a compact summary
 * stream plus a small preview point set for the Next.js client.
 */
class ToClientLidarPreviewWriter
{
public:
    struct PreviewPoint
    {
        double xM = 0.0;
        double yM = 0.0;
        double distanceM = 0.0;
        double angleDeg = 0.0;
        std::uint64_t timestampMs = 0;
        long long cellKey = 0;
    };

    struct Config
    {
        std::uint64_t summaryPeriodMs = 1000;
        std::size_t maxPreviewPoints = 6000;
        std::size_t pointStride = 1;

        // Ver28 client preview down-sampling.
        // Keep one point per grid cell and limit each scan contribution.
        // Accumulate into the preview map only while the robot is actually moving.
        double gridResolutionM = 0.08;
        std::size_t maxPointsPerScanForClient = 220;
        bool accumulateOnlyWhileMoving = true;
        double driveCommandThreshold = 1.0;
        double odomDeltaMinM = 0.04;
        double odomDeltaMinYawDeg = 2.0;
    };

    ToClientLidarPreviewWriter() = default;
    ~ToClientLidarPreviewWriter();

    bool start(const std::string& summaryJsonlPath,
               const std::string& previewJsonPath,
               const Config& cfg);
    void stop(std::uint64_t stoppedAtMs, const std::string& reason);

    bool isEnabled() const;

    void update(const RobotState& state, std::uint64_t nowMs);

    std::size_t previewPointCount() const;
    std::uint64_t summariesWritten() const;
    const std::string& previewPath() const;
    const std::string& summaryPath() const;

private:
    static const char* boolText(bool v);
    static std::string jsonText(const std::string& value);
    static double angleDeg(double x, double y);
    static long long makeCellKey(double x, double y, double resolutionM);
    void trimPreviewToLimit();
    void writeSummary(const RobotState& state, std::uint64_t nowMs);
    void writePreviewFile(std::uint64_t stoppedAtMs, const std::string& reason);
    bool shouldAccumulateMapPreview(const RobotState& state, std::uint64_t nowMs);
    void addPreviewPoints(const LidarSnapshot& snapshot, std::uint64_t nowMs);

private:
    Config cfg_{};
    std::ofstream summaryFile_{};
    std::string summaryPath_{};
    std::string previewPath_{};
    std::vector<PreviewPoint> previewPoints_{};
    std::unordered_set<long long> occupiedCells_{};
    bool enabled_ = false;
    std::uint64_t lastSummaryMs_ = 0;
    std::uint64_t summariesWritten_ = 0;
    bool haveLastAcceptedOdomPose_ = false;
    double lastAcceptedOdomXM_ = 0.0;
    double lastAcceptedOdomYM_ = 0.0;
    double lastAcceptedOdomYawDeg_ = 0.0;
    std::uint64_t scansAcceptedForPreview_ = 0;
    std::uint64_t scansSkippedNoMotion_ = 0;
};

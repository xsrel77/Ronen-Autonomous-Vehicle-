#include "dev_farm/DevFarmLidarMapper.h"




#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <utility>




namespace fs = std::filesystem;




namespace {
constexpr double PI_D = 3.14159265358979323846;




std::string jsonEscape(const std::string& s)
{
    std::ostringstream out;
    for (char ch : s) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"':  out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}
}




DevFarmLidarMapper::~DevFarmLidarMapper()
{
    if (state_.recording) {
        stopAndSave("destructor", state_.timestampMs);
    }
}




bool DevFarmLidarMapper::start(std::uint64_t nowMs)
{
    state_ = DevFarmMapState{};
    state_.recording = true;
    state_.valid = true;
    state_.isFresh = true;
    state_.loaded = false;
    state_.stopReason.clear();
    state_.startedAtMs = nowMs;
    state_.timestampMs = nowMs;
    state_.outputPath = makeTimestampedMapPath();
    state_.latestPathFile = "Debugging/devFarm/maps/latest_lidar_map_path.txt";
    state_.mapMode = "occupancy_grid_ray_clearing_2d";
    state_.pointSampleStride = kPointSampleStride;
    state_.gridResolutionM = kOccupancyResolutionM;
    state_.occupancyResolutionM = kOccupancyResolutionM;
    state_.occupancyGridEnabled = true;
    state_.rayClearingEnabled = true;
    state_.slamLiteEnabled = false;
    state_.poseSource = "wait_pose";
    state_.mappingGateStatus = "WAIT_POSE";
    state_.mappingSkipReason = "mapping_not_started";
    lastSampleMs_ = 0;
    lastMatcherRebuildMs_ = 0;
    slamPoseInitialized_ = false;
    lastRawPoseXM_ = 0.0;
    lastRawPoseYM_ = 0.0;
    lastRawPoseYawDeg_ = 0.0;
    correctedPoseXM_ = 0.0;
    correctedPoseYM_ = 0.0;
    correctedPoseYawDeg_ = 0.0;
    scanMatcher_.reset();
    occupancyGrid_.reset();
    DevFarmOccupancyGridMapper::Config occCfg{};
    occCfg.resolutionM = kOccupancyResolutionM;
    occCfg.wallProtectionEnabled = true;
    occCfg.stableWallLogOdds = 1.35;
    occCfg.stableWallMinHits = 8;
    occCfg.endpointSnapRadiusCells = 2;
    occCfg.wallProtectionEndSkipCells = 3;
    occupancyGrid_.setConfig(occCfg);




    state_.points.reserve(120000);
    state_.previewPoints.reserve(kMaxPreviewPoints);




    std::error_code ec;
    fs::create_directories(fs::path(state_.outputPath).parent_path(), ec);
    if (ec) {
        std::cerr << "[devFarm][lidar-map] failed to create maps folder: "
                  << ec.message() << "\n";
        state_ = DevFarmMapState{};
        return false;
    }




    std::cout << "[devFarm][lidar-map] x START OCCUPANCY-GRID mapping -> "
              << state_.outputPath
              << " resolution=" << kOccupancyResolutionM
              << "m rayClearing=ON"
              << " guiPreviewMax=" << kMaxPreviewPoints << "\n";
    return true;
}




bool DevFarmLidarMapper::stopAndSave(const std::string& reason, std::uint64_t nowMs)
{
    if (!state_.recording && state_.points.empty()) {
        return false;
    }




    state_.recording = false;
    state_.isFresh = false;
    state_.stopReason = reason;
    state_.stoppedAtMs = nowMs;
    state_.timestampMs = nowMs;
    state_.pointsCount = state_.points.size();
    refreshPreviewPoints();




    const bool ok = saveJson(state_.outputPath);
    state_.lastSaveOk = ok;




    std::cout << "[devFarm][lidar-map] x STOP OCCUPANCY-GRID mapping. reason=" << reason
              << " occupiedPoints=" << state_.points.size()
              << " totalCells=" << state_.occupancyTotalCells
              << " freeCells=" << state_.occupancyFreeCells
              << " rays=" << state_.raysIntegratedCount
              << " saved=" << (ok ? "YES" : "NO")
              << " file=" << state_.outputPath << "\n";




    if (ok) {
        const bool latestOk = saveLatestPathFile(state_.outputPath);
        if (!latestOk) {
            std::cerr << "[devFarm][lidar-map] failed to update latest path file\n";
        }
    }




    return ok;
}




bool DevFarmLidarMapper::isRecording() const
{
    return state_.recording;
}




const DevFarmMapState& DevFarmLidarMapper::getState() const
{
    return state_;
}








bool DevFarmLidarMapper::loadLatestMap(std::uint64_t nowMs)
{
    const std::string latestPath = latestPathFilePath();




    std::ifstream in(latestPath);
    if (!in.is_open()) {
        std::cerr << "[devFarm][lidar-map] c LOAD failed: latest path file not found: "
                  << latestPath << "\n";
        return false;
    }




    std::string mapPath;
    std::getline(in, mapPath);
    mapPath = trim(mapPath);




    if (mapPath.empty()) {
        std::cerr << "[devFarm][lidar-map] c LOAD failed: latest path file is empty: "
                  << latestPath << "\n";
        return false;
    }




    return loadMap(mapPath, nowMs);
}








bool DevFarmLidarMapper::loadMap(const std::string& path, std::uint64_t nowMs)
{
    const std::string mapPath = trim(path);
    if (mapPath.empty()) {
        std::cerr << "[devFarm][lidar-map] c LOAD failed: empty map path\n";
        return false;
    }




    if (state_.recording) {
        std::cerr << "[devFarm][lidar-map] c LOAD blocked: mapping is currently recording. Stop x first.\n";
        return false;
    }




    std::ifstream in(mapPath);
    if (!in.is_open()) {
        std::cerr << "[devFarm][lidar-map] c LOAD failed: cannot open map file: "
                  << mapPath << "\n";
        return false;
    }




    DevFarmMapState loaded{};
    loaded.recording = false;
    loaded.loaded = true;
    loaded.valid = true;
    loaded.isFresh = true;
    loaded.outputPath = mapPath;
    loaded.latestPathFile = latestPathFilePath();
    loaded.stopReason = "loaded_from_file";
    loaded.timestampMs = nowMs;
    loaded.stoppedAtMs = nowMs;
    loaded.pointSampleStride = kPointSampleStride;
    loaded.mapMode = "loaded_map";
    loaded.gridResolutionM = 0.0;
    loaded.occupancyResolutionM = 0.0;
    loaded.occupancyGridEnabled = false;
    loaded.rayClearingEnabled = false;
    loaded.slamLiteEnabled = false;
    loaded.poseSource = "loaded_map";




    loaded.points.reserve(200000);




    std::string line;
    std::uint64_t expectedPoints = 0;
    while (std::getline(in, line)) {
        std::uint64_t tempU64 = 0;
        double tempDouble = 0.0;




        if (extractUint64(line, "points_count", tempU64)) {
            expectedPoints = tempU64;
            if (expectedPoints > 0 && expectedPoints < kMaxStoredPoints) {
                loaded.points.reserve(static_cast<std::size_t>(expectedPoints));
            }
        }
        if (extractUint64(line, "started_at_ms", tempU64)) {
            loaded.startedAtMs = tempU64;
        }
        if (extractUint64(line, "stopped_at_ms", tempU64)) {
            loaded.stoppedAtMs = tempU64;
        }
        if (extractUint64(line, "source_samples_count", tempU64)) {
            loaded.sourceSamplesCount = tempU64;
        }
        if (extractUint64(line, "accepted_samples_count", tempU64)) {
            loaded.acceptedSamplesCount = tempU64;
        }
        if (extractUint64(line, "point_sample_stride", tempU64)) {
            loaded.pointSampleStride = static_cast<std::size_t>(tempU64);
        }
        if (extractDouble(line, "resolution_m", tempDouble)) {
            loaded.gridResolutionM = tempDouble;
            loaded.occupancyResolutionM = tempDouble;
            loaded.occupancyGridEnabled = true;
            loaded.rayClearingEnabled = true;
            loaded.mapMode = "occupancy_grid_ray_clearing_2d";
        }
        if (extractUint64(line, "occupancy_total_cells", tempU64)) {
            loaded.occupancyTotalCells = static_cast<std::size_t>(tempU64);
        }
        if (extractUint64(line, "occupancy_occupied_cells", tempU64)) {
            loaded.occupancyOccupiedCells = static_cast<std::size_t>(tempU64);
        }
        if (extractUint64(line, "occupancy_free_cells", tempU64)) {
            loaded.occupancyFreeCells = static_cast<std::size_t>(tempU64);
        }
        if (extractUint64(line, "rays_integrated_count", tempU64)) {
            loaded.raysIntegratedCount = tempU64;
        }
        if (extractUint64(line, "free_updates_count", tempU64)) {
            loaded.freeUpdatesCount = tempU64;
        }
        if (extractUint64(line, "occupied_updates_count", tempU64)) {
            loaded.occupiedUpdatesCount = tempU64;
        }
        if (extractDouble(line, "last_pose_x_m", tempDouble)) {
            loaded.lastPoseXM = tempDouble;
        }
        if (extractDouble(line, "last_pose_y_m", tempDouble)) {
            loaded.lastPoseYM = tempDouble;
        }
        if (extractDouble(line, "last_pose_yaw_deg", tempDouble)) {
            loaded.lastPoseYawDeg = tempDouble;
        }




        DevFarmMapPoint point{};
        if (parsePointLine(line, point)) {
            if (loaded.points.size() >= kMaxStoredPoints) {
                loaded.maxPointsReached = true;
                break;
            }




            loaded.points.push_back(point);
        }
    }




    if (loaded.points.empty()) {
        std::cerr << "[devFarm][lidar-map] c LOAD failed: no map points parsed from: "
                  << mapPath << "\n";
        return false;
    }




    loaded.pointsCount = loaded.points.size();
    if (loaded.occupancyOccupiedCells == 0 && loaded.occupancyGridEnabled) {
        loaded.occupancyOccupiedCells = loaded.points.size();
    }
    loaded.previewPoints.reserve(kMaxPreviewPoints);
    state_ = std::move(loaded);
    refreshPreviewPoints();
    state_.previewPointsCount = state_.previewPoints.size();
    state_.lastSaveOk = true;




    rebuildScanMatcherFromCurrentMap(nowMs, true);




    std::cout << "[devFarm][lidar-map] c LOAD OK: "
              << state_.points.size() << " points, preview="
              << state_.previewPoints.size() << " file=" << mapPath << "\n";




    return true;
}




void DevFarmLidarMapper::updateFromSnapshot(const LidarSnapshot& snapshot,
                                            const DevFarmPoseProvider::Pose& pose,
                                            std::uint64_t nowMs)
{
    if (!state_.recording) {
        return;
    }




    state_.timestampMs = nowMs;
    state_.isFresh = true;




    if (!snapshot.valid || snapshot.points.empty()) {
        state_.mappingGateStatus = "WAIT_LIDAR";
        state_.mappingSkipReason = "lidar_snapshot_not_valid";
        return;
    }




    if (lastSampleMs_ > 0 && (nowMs - lastSampleMs_) < kMinSampleIntervalMs) {
        return;
    }
    lastSampleMs_ = nowMs;




    state_.poseValid = pose.valid;
    state_.poseFresh = pose.fresh;
    state_.poseRejected = pose.rejected;
    state_.poseSource = pose.source.empty() ? std::string("wait_pose") : pose.source;
    state_.lastPoseDeltaM = pose.deltaMeters;
    state_.lastPoseDeltaYawDeg = pose.deltaYawDeg;




    state_.slamLiteEnabled = true;
    state_.slamMatchAttempted = false;
    state_.slamMatchAccepted = false;
    state_.slamMatchWeak = false;
    state_.slamMatchScore = 0.0;
    state_.slamBaseScore = 0.0;
    state_.slamMatchImprovement = 0.0;
    state_.slamDxM = 0.0;
    state_.slamDyM = 0.0;
    state_.slamDYawDeg = 0.0;
    state_.slamMapCells = scanMatcher_.mapCellsCount();




    if (!pose.valid) {
        state_.mappingGateOpen = false;
        state_.mappingGateStatus = "WAIT_POSE";
        state_.mappingSkipReason = pose.rejectReason.empty()
            ? std::string("pose_not_valid")
            : pose.rejectReason;




        if (pose.rejected) {
            state_.scansSkippedBadPoseCount += 1;
        } else {
            state_.scansSkippedNoPoseCount += 1;
        }




        const auto occStats = occupancyGrid_.stats();
        state_.mapMode = "occupancy_grid_wait_pose";
        state_.gridResolutionM = occupancyGrid_.config().resolutionM;
        state_.occupancyResolutionM = occupancyGrid_.config().resolutionM;
        state_.occupancyGridEnabled = true;
        state_.rayClearingEnabled = true;
        state_.occupancyTotalCells = occStats.totalCells;
        state_.occupancyOccupiedCells = occStats.occupiedCells;
        state_.occupancyFreeCells = occStats.freeCells;
        state_.raysIntegratedCount = occStats.raysIntegrated;
        state_.occupiedUpdatesCount = occStats.occupiedUpdates;
        state_.freeUpdatesCount = occStats.freeUpdates;
        state_.wallProtectionStopCount = occStats.wallProtectionStops;
        state_.endpointSnapCount = occStats.endpointSnaps;
        state_.sourceSamplesCount = occStats.sourceSamples;
        state_.acceptedSamplesCount = occStats.occupiedCells;
        state_.maxPointsReached = occStats.maxCellsReached;
        state_.pointsCount = state_.points.size();
        refreshPreviewPoints();
        return;
    }




    const double rawX = pose.xM;
    const double rawY = pose.yM;
    const double rawYaw = normalizeAngleDeg(pose.yawDeg);




    if (!slamPoseInitialized_) {
        slamPoseInitialized_ = true;
        lastRawPoseXM_ = rawX;
        lastRawPoseYM_ = rawY;
        lastRawPoseYawDeg_ = rawYaw;
        correctedPoseXM_ = rawX;
        correctedPoseYM_ = rawY;
        correctedPoseYawDeg_ = rawYaw;
    } else {
        const double dxRaw = rawX - lastRawPoseXM_;
        const double dyRaw = rawY - lastRawPoseYM_;
        const double dyawRaw = normalizeAngleDeg(rawYaw - lastRawPoseYawDeg_);




        correctedPoseXM_ += dxRaw;
        correctedPoseYM_ += dyRaw;
        correctedPoseYawDeg_ = normalizeAngleDeg(correctedPoseYawDeg_ + dyawRaw);




        lastRawPoseXM_ = rawX;
        lastRawPoseYM_ = rawY;
        lastRawPoseYawDeg_ = rawYaw;
    }




    state_.mappingGateOpen = true;
    state_.mappingGateStatus = "POSE_OK";
    state_.mappingSkipReason.clear();
    state_.usesOdomTransform = true;




    const auto match = scanMatcher_.matchPose(snapshot,
                                             correctedPoseXM_,
                                             correctedPoseYM_,
                                             correctedPoseYawDeg_,
                                             nowMs);




    state_.slamMapCells = match.mapCells;
    state_.slamMatchAttempted = match.attempted;
    state_.slamMatchAccepted = match.accepted;
    state_.slamMatchScore = match.score;
    state_.slamBaseScore = match.baseScore;
    state_.slamMatchImprovement = match.improvement;
    state_.slamDxM = match.dxM;
    state_.slamDyM = match.dyM;
    state_.slamDYawDeg = match.dYawDeg;




    if (match.attempted) {
        state_.slamAttemptsCount += 1;
    }




    if (match.accepted) {
        correctedPoseXM_ += match.dxM * kSlamCorrectionGainXY;
        correctedPoseYM_ += match.dyM * kSlamCorrectionGainXY;
        correctedPoseYawDeg_ = normalizeAngleDeg(correctedPoseYawDeg_ + match.dYawDeg * kSlamCorrectionGainYaw);
        state_.slamAcceptedCount += 1;
        state_.poseSource += "+slam";
    } else if (match.attempted && match.mapReady) {
        state_.slamMatchWeak = true;
    }




    state_.lastPoseXM = correctedPoseXM_;
    state_.lastPoseYM = correctedPoseYM_;
    state_.lastPoseYawDeg = normalizeAngleDeg(correctedPoseYawDeg_);




    state_.scansIntegratedCount += 1;




    occupancyGrid_.updateFromSnapshot(snapshot,
                                      state_.lastPoseXM,
                                      state_.lastPoseYM,
                                      state_.lastPoseYawDeg,
                                      true,
                                      nowMs);




    const auto occStats = occupancyGrid_.stats();
    state_.mapMode = "occupancy_grid_slam_lite_ray_clearing_2d";
    state_.gridResolutionM = occupancyGrid_.config().resolutionM;
    state_.occupancyResolutionM = occupancyGrid_.config().resolutionM;
    state_.occupancyGridEnabled = true;
    state_.rayClearingEnabled = true;
    state_.occupancyTotalCells = occStats.totalCells;
    state_.occupancyOccupiedCells = occStats.occupiedCells;
    state_.occupancyFreeCells = occStats.freeCells;
    state_.raysIntegratedCount = occStats.raysIntegrated;
    state_.occupiedUpdatesCount = occStats.occupiedUpdates;
    state_.freeUpdatesCount = occStats.freeUpdates;
    state_.wallProtectionStopCount = occStats.wallProtectionStops;
    state_.endpointSnapCount = occStats.endpointSnaps;
    state_.sourceSamplesCount = occStats.sourceSamples;
    state_.acceptedSamplesCount = occStats.occupiedCells;
    state_.maxPointsReached = occStats.maxCellsReached;




    occupancyGrid_.exportOccupiedPoints(state_.points, kMaxStoredPoints, true, nowMs);
    state_.pointsCount = state_.points.size();
    refreshPreviewPoints();
    rebuildScanMatcherFromCurrentMap(nowMs, false);
}






void DevFarmLidarMapper::rebuildScanMatcherFromCurrentMap(std::uint64_t nowMs, bool force)
{
    if (!force && lastMatcherRebuildMs_ > 0 && (nowMs - lastMatcherRebuildMs_) < kMatcherRebuildIntervalMs) {
        return;
    }




    scanMatcher_.reset();
    if (!state_.points.empty()) {
        const std::size_t step = std::max<std::size_t>(1U, state_.points.size() / kMatcherMaxCells);
        for (std::size_t i = 0; i < state_.points.size(); i += step) {
            scanMatcher_.addMapPoint(state_.points[i].xM, state_.points[i].yM);
        }
    }




    state_.slamMapCells = scanMatcher_.mapCellsCount();
    lastMatcherRebuildMs_ = nowMs;
}




std::string DevFarmLidarMapper::makeTimestampedMapPath()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmValue{};
    localtime_r(&t, &tmValue);




    std::ostringstream oss;
    oss << "Debugging/devFarm/maps/farm_lidar_map_"
        << std::put_time(&tmValue, "%Y%m%d_%H%M%S")
        << ".json";
    return oss.str();
}




bool DevFarmLidarMapper::saveLatestPathFile(const std::string& mapPath) const
{
    const std::string latestPath = state_.latestPathFile.empty()
        ? std::string("Debugging/devFarm/maps/latest_lidar_map_path.txt")
        : state_.latestPathFile;




    std::error_code ec;
    fs::create_directories(fs::path(latestPath).parent_path(), ec);
    if (ec) {
        std::cerr << "[devFarm][lidar-map] create latest directory failed: "
                  << ec.message() << "\n";
        return false;
    }




    std::ofstream out(latestPath, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[devFarm][lidar-map] failed to open latest path file: "
                  << latestPath << "\n";
        return false;
    }




    out << mapPath << "\n";
    return true;
}




bool DevFarmLidarMapper::saveJson(const std::string& path) const
{
    if (path.empty()) {
        return false;
    }




    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (ec) {
        std::cerr << "[devFarm][lidar-map] create directory failed: "
                  << ec.message() << "\n";
        return false;
    }




    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[devFarm][lidar-map] failed to open json for writing: "
                  << path << "\n";
        return false;
    }




    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"metadata\": {\n";
    out << "    \"version\": \"RBV2_NJOrin_ver22_devFarm\",\n";
    out << "    \"source\": \"MiniLidarSDL\",\n";
    out << "    \"map_type\": \"occupancy_grid_ray_clearing_2d\",\n";
    out << "    \"coordinate_frame\": \"devfarm_lidar_map_2d\",\n";
    out << "    \"resolution_m\": " << state_.occupancyResolutionM << ",\n";
    out << "    \"ray_clearing_enabled\": " << (state_.rayClearingEnabled ? "true" : "false") << ",\n";
    out << "    \"ray_beam_stride\": " << occupancyGrid_.config().beamStride << ",\n";
    out << "    \"occupancy_total_cells\": " << state_.occupancyTotalCells << ",\n";
    out << "    \"occupancy_occupied_cells\": " << state_.occupancyOccupiedCells << ",\n";
    out << "    \"occupancy_free_cells\": " << state_.occupancyFreeCells << ",\n";
    out << "    \"rays_integrated_count\": " << state_.raysIntegratedCount << ",\n";
    out << "    \"free_updates_count\": " << state_.freeUpdatesCount << ",\n";
    out << "    \"occupied_updates_count\": " << state_.occupiedUpdatesCount << ",\n";
    out << "    \"wall_protection_stop_count\": " << state_.wallProtectionStopCount << ",\n";
    out << "    \"endpoint_snap_count\": " << state_.endpointSnapCount << ",\n";
    out << "    \"scans_integrated_count\": " << state_.scansIntegratedCount << ",\n";
    out << "    \"scans_skipped_no_pose_count\": " << state_.scansSkippedNoPoseCount << ",\n";
    out << "    \"scans_skipped_bad_pose_count\": " << state_.scansSkippedBadPoseCount << ",\n";
    out << "    \"started_at_ms\": " << state_.startedAtMs << ",\n";
    out << "    \"stopped_at_ms\": " << state_.stoppedAtMs << ",\n";
    out << "    \"points_count\": " << state_.points.size() << ",\n";
    out << "    \"max_points_limit\": " << kMaxStoredPoints << ",\n";
    out << "    \"point_sample_stride\": " << kPointSampleStride << ",\n";
    out << "    \"source_samples_count\": " << state_.sourceSamplesCount << ",\n";
    out << "    \"accepted_samples_count\": " << state_.acceptedSamplesCount << ",\n";
    out << "    \"gui_preview_max_points\": " << kMaxPreviewPoints << ",\n";
    out << "    \"pose_valid\": " << (state_.poseValid ? "true" : "false") << ",\n";
    out << "    \"pose_fresh\": " << (state_.poseFresh ? "true" : "false") << ",\n";
    out << "    \"pose_rejected\": " << (state_.poseRejected ? "true" : "false") << ",\n";
    out << "    \"mapping_gate_open\": " << (state_.mappingGateOpen ? "true" : "false") << ",\n";
    out << "    \"mapping_gate_status\": \"" << jsonEscape(state_.mappingGateStatus) << "\",\n";
    out << "    \"mapping_skip_reason\": \"" << jsonEscape(state_.mappingSkipReason) << "\",\n";
    out << "    \"pose_source\": \"" << jsonEscape(state_.poseSource) << "\",\n";
    out << "    \"last_pose_x_m\": " << state_.lastPoseXM << ",\n";
    out << "    \"last_pose_y_m\": " << state_.lastPoseYM << ",\n";
    out << "    \"last_pose_yaw_deg\": " << state_.lastPoseYawDeg << ",\n";
    out << "    \"last_pose_delta_m\": " << state_.lastPoseDeltaM << ",\n";
    out << "    \"last_pose_delta_yaw_deg\": " << state_.lastPoseDeltaYawDeg << ",\n";
    out << "    \"slam_lite_enabled\": " << (state_.slamLiteEnabled ? "true" : "false") << ",\n";
    out << "    \"slam_attempts_count\": " << state_.slamAttemptsCount << ",\n";
    out << "    \"slam_accepted_count\": " << state_.slamAcceptedCount << ",\n";
    out << "    \"slam_last_score\": " << state_.slamMatchScore << ",\n";
    out << "    \"slam_last_base_score\": " << state_.slamBaseScore << ",\n";
    out << "    \"slam_last_dx_m\": " << state_.slamDxM << ",\n";
    out << "    \"slam_last_dy_m\": " << state_.slamDyM << ",\n";
    out << "    \"slam_last_dyaw_deg\": " << state_.slamDYawDeg << ",\n";
    out << "    \"uses_odom_transform\": " << (state_.usesOdomTransform ? "true" : "false") << ",\n";
    out << "    \"max_points_reached\": " << (state_.maxPointsReached ? "true" : "false") << ",\n";
    out << "    \"stop_reason\": \"" << jsonEscape(state_.stopReason) << "\",\n";
    out << "    \"units\": {\"x\": \"m\", \"y\": \"m\", \"distance\": \"m\", \"angle\": \"deg\"}\n";
    out << "  },\n";
    out << "  \"points\": [\n";




    for (std::size_t i = 0; i < state_.points.size(); ++i) {
        const auto& p = state_.points[i];
        out << "    {"
            << "\"x_m\": " << p.xM << ", "
            << "\"y_m\": " << p.yM << ", "
            << "\"hits\": " << p.hits
            << "}";
        if (i + 1 < state_.points.size()) {
            out << ",";
        }
        out << "\n";
    }




    out << "  ]\n";
    out << "}\n";
    return true;
}




void DevFarmLidarMapper::refreshPreviewPoints()
{
    state_.previewPoints.clear();




    if (state_.points.empty()) {
        state_.previewPointsCount = 0;
        return;
    }




    const std::size_t step = std::max<std::size_t>(1, state_.points.size() / kMaxPreviewPoints);
    state_.previewPoints.reserve(std::min<std::size_t>(state_.points.size(), kMaxPreviewPoints));




    for (std::size_t i = 0; i < state_.points.size(); i += step) {
        state_.previewPoints.push_back(state_.points[i]);
        if (state_.previewPoints.size() >= kMaxPreviewPoints) {
            break;
        }
    }




    state_.previewPointsCount = state_.previewPoints.size();
}








std::string DevFarmLidarMapper::latestPathFilePath()
{
    return "Debugging/devFarm/maps/latest_lidar_map_path.txt";
}








std::string DevFarmLidarMapper::trim(const std::string& value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }




    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }




    return value.substr(begin, end - begin);
}








bool DevFarmLidarMapper::extractDouble(const std::string& line,
                                        const std::string& key,
                                        double& outValue)
{
    const std::string token = "\"" + key + "\"";
    const std::size_t keyPos = line.find(token);
    if (keyPos == std::string::npos) {
        return false;
    }




    const std::size_t colonPos = line.find(':', keyPos + token.size());
    if (colonPos == std::string::npos) {
        return false;
    }




    const char* begin = line.c_str() + colonPos + 1;
    char* endPtr = nullptr;
    const double value = std::strtod(begin, &endPtr);
    if (endPtr == begin) {
        return false;
    }




    outValue = value;
    return true;
}








bool DevFarmLidarMapper::extractUint64(const std::string& line,
                                        const std::string& key,
                                        std::uint64_t& outValue)
{
    double value = 0.0;
    if (!extractDouble(line, key, value)) {
        return false;
    }




    if (value < 0.0) {
        return false;
    }




    outValue = static_cast<std::uint64_t>(value);
    return true;
}








bool DevFarmLidarMapper::parsePointLine(const std::string& line,
                                        DevFarmMapPoint& outPoint)
{
    if (line.find("\"x_m\"") == std::string::npos ||
        line.find("\"y_m\"") == std::string::npos) {
        return false;
    }




    DevFarmMapPoint p{};
    if (!extractDouble(line, "x_m", p.xM) ||
        !extractDouble(line, "y_m", p.yM)) {
        return false;
    }




    if (!extractDouble(line, "local_x_m", p.localXM)) {
        p.localXM = p.xM;
    }
    if (!extractDouble(line, "local_y_m", p.localYM)) {
        p.localYM = p.yM;
    }
    if (!extractDouble(line, "distance_m", p.distanceM)) {
        p.distanceM = std::sqrt(p.localXM * p.localXM + p.localYM * p.localYM);
    }
    if (!extractDouble(line, "angle_deg", p.angleDeg)) {
        p.angleDeg = normalizeAngleDeg(std::atan2(p.localYM, p.localXM) * 180.0 / PI_D);
    }
    if (!extractDouble(line, "robot_x_m", p.robotXM)) {
        p.robotXM = 0.0;
    }
    if (!extractDouble(line, "robot_y_m", p.robotYM)) {
        p.robotYM = 0.0;
    }
    if (!extractDouble(line, "robot_yaw_deg", p.robotYawDeg)) {
        p.robotYawDeg = 0.0;
    }




    std::uint64_t tempU64 = 0;
    if (extractUint64(line, "timestamp_ms", tempU64)) {
        p.timestampMs = tempU64;
    }
    if (extractUint64(line, "hits", tempU64)) {
        p.hits = static_cast<std::uint32_t>(std::min<std::uint64_t>(tempU64, 0xFFFFFFFFULL));
    } else {
        p.hits = 1;
    }




    outPoint = p;
    return true;
}








double DevFarmLidarMapper::normalizeAngleDeg(double angleDeg)
{
    while (angleDeg > 180.0) angleDeg -= 360.0;
    while (angleDeg < -180.0) angleDeg += 360.0;
    return angleDeg;
}
















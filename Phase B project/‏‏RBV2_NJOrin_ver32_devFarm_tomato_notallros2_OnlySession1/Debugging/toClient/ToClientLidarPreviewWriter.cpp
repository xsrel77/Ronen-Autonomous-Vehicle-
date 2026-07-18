#include "Debugging/toClient/ToClientLidarPreviewWriter.h"

#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>

namespace fs = std::filesystem;

ToClientLidarPreviewWriter::~ToClientLidarPreviewWriter()
{
    stop(0, "destructor");
}

bool ToClientLidarPreviewWriter::start(const std::string& summaryJsonlPath,
                                       const std::string& previewJsonPath,
                                       const Config& cfg)
{
    stop(0, "restart");

    cfg_ = cfg;
    summaryPath_ = summaryJsonlPath;
    previewPath_ = previewJsonPath;
    previewPoints_.clear();
    occupiedCells_.clear();
    lastSummaryMs_ = 0;
    summariesWritten_ = 0;
    haveLastAcceptedOdomPose_ = false;
    lastAcceptedOdomXM_ = 0.0;
    lastAcceptedOdomYM_ = 0.0;
    lastAcceptedOdomYawDeg_ = 0.0;
    scansAcceptedForPreview_ = 0;
    scansSkippedNoMotion_ = 0;

    try {
        fs::create_directories(fs::path(summaryJsonlPath).parent_path());
        fs::create_directories(fs::path(previewJsonPath).parent_path());
    } catch (const std::exception& e) {
        std::cerr << "[toClient][lidar] mkdir failed: " << e.what() << "\n";
        enabled_ = false;
        return false;
    }

    summaryFile_.open(summaryJsonlPath, std::ios::out | std::ios::trunc);
    if (!summaryFile_.is_open()) {
        std::cerr << "[toClient][lidar] failed to open summary JSONL: " << summaryJsonlPath << "\n";
        enabled_ = false;
        return false;
    }

    enabled_ = true;
    return true;
}

void ToClientLidarPreviewWriter::stop(std::uint64_t stoppedAtMs, const std::string& reason)
{
    const bool wasEnabled = enabled_ || summaryFile_.is_open();

    if (summaryFile_.is_open()) {
        summaryFile_.flush();
        summaryFile_.close();
    }

    if (wasEnabled && !previewPath_.empty()) {
        writePreviewFile(stoppedAtMs, reason);
    }

    enabled_ = false;
}

bool ToClientLidarPreviewWriter::isEnabled() const
{
    return enabled_ && summaryFile_.is_open();
}

std::size_t ToClientLidarPreviewWriter::previewPointCount() const
{
    return previewPoints_.size();
}

std::uint64_t ToClientLidarPreviewWriter::summariesWritten() const
{
    return summariesWritten_;
}

const std::string& ToClientLidarPreviewWriter::previewPath() const
{
    return previewPath_;
}

const std::string& ToClientLidarPreviewWriter::summaryPath() const
{
    return summaryPath_;
}

const char* ToClientLidarPreviewWriter::boolText(bool v)
{
    return v ? "true" : "false";
}

std::string ToClientLidarPreviewWriter::jsonText(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(ch >> 4) & 0x0F]);
                    out.push_back(hex[ch & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

double ToClientLidarPreviewWriter::angleDeg(double x, double y)
{
    return std::atan2(y, x) * 180.0 / 3.14159265358979323846;
}

long long ToClientLidarPreviewWriter::makeCellKey(double x, double y, double resolutionM)
{
    const double res = std::max(0.01, resolutionM);
    const auto gx = static_cast<std::int32_t>(std::llround(x / res));
    const auto gy = static_cast<std::int32_t>(std::llround(y / res));
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(gy));
    return static_cast<long long>((ux << 32) ^ uy);
}

void ToClientLidarPreviewWriter::trimPreviewToLimit()
{
    if (cfg_.maxPreviewPoints == 0) {
        previewPoints_.clear();
        occupiedCells_.clear();
        return;
    }

    while (previewPoints_.size() > cfg_.maxPreviewPoints) {
        const std::size_t eraseCount = std::min<std::size_t>(
            previewPoints_.size() - cfg_.maxPreviewPoints,
            std::max<std::size_t>(1, cfg_.maxPreviewPoints / 10));

        for (std::size_t i = 0; i < eraseCount; ++i) {
            occupiedCells_.erase(previewPoints_[i].cellKey);
        }
        previewPoints_.erase(previewPoints_.begin(), previewPoints_.begin() + static_cast<std::ptrdiff_t>(eraseCount));
    }
}

void ToClientLidarPreviewWriter::update(const RobotState& state, std::uint64_t nowMs)
{
    if (!isEnabled()) {
        return;
    }

    if (state.lidar.valid && state.lidar.isFresh) {
        if (shouldAccumulateMapPreview(state, nowMs)) {
            addPreviewPoints(state.lidar, nowMs);
            ++scansAcceptedForPreview_;
        } else {
            ++scansSkippedNoMotion_;
        }
    }

    if (lastSummaryMs_ == 0 || nowMs < lastSummaryMs_ || (nowMs - lastSummaryMs_) >= cfg_.summaryPeriodMs) {
        lastSummaryMs_ = nowMs;
        writeSummary(state, nowMs);
    }
}


bool ToClientLidarPreviewWriter::shouldAccumulateMapPreview(const RobotState& state, std::uint64_t /*nowMs*/)
{
    if (!cfg_.accumulateOnlyWhileMoving) {
        return true;
    }

    const bool driveCommandMoving =
        std::fabs(state.drive.currentForwardSpeed) >= cfg_.driveCommandThreshold ||
        std::fabs(state.drive.currentSteeringSpeed) >= cfg_.driveCommandThreshold ||
        std::fabs(state.nav.forwardCommand) >= cfg_.driveCommandThreshold ||
        std::fabs(state.nav.steeringCommand) >= cfg_.driveCommandThreshold ||
        std::fabs(state.odom.forwardCommand) >= cfg_.driveCommandThreshold ||
        std::fabs(state.odom.steeringCommand) >= cfg_.driveCommandThreshold;

    if (driveCommandMoving) {
        if (state.odom.valid) {
            haveLastAcceptedOdomPose_ = true;
            lastAcceptedOdomXM_ = state.odom.xMeters;
            lastAcceptedOdomYM_ = state.odom.yMeters;
            lastAcceptedOdomYawDeg_ = state.odom.yawDeg;
        }
        return true;
    }

    if (state.odom.valid) {
        if (!haveLastAcceptedOdomPose_) {
            haveLastAcceptedOdomPose_ = true;
            lastAcceptedOdomXM_ = state.odom.xMeters;
            lastAcceptedOdomYM_ = state.odom.yMeters;
            lastAcceptedOdomYawDeg_ = state.odom.yawDeg;
            return false;
        }

        const double dx = state.odom.xMeters - lastAcceptedOdomXM_;
        const double dy = state.odom.yMeters - lastAcceptedOdomYM_;
        double dyaw = std::fabs(state.odom.yawDeg - lastAcceptedOdomYawDeg_);
        while (dyaw > 180.0) dyaw = std::fabs(dyaw - 360.0);
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (dist >= cfg_.odomDeltaMinM || dyaw >= cfg_.odomDeltaMinYawDeg) {
            lastAcceptedOdomXM_ = state.odom.xMeters;
            lastAcceptedOdomYM_ = state.odom.yMeters;
            lastAcceptedOdomYawDeg_ = state.odom.yawDeg;
            return true;
        }
    }

    return false;
}

void ToClientLidarPreviewWriter::addPreviewPoints(const LidarSnapshot& snapshot, std::uint64_t nowMs)
{
    if (snapshot.points.empty() || cfg_.maxPreviewPoints == 0) {
        return;
    }

    const std::size_t stride = std::max<std::size_t>(1, cfg_.pointStride);
    const std::size_t maxFromScan = std::max<std::size_t>(1, cfg_.maxPointsPerScanForClient);
    std::size_t addedFromScan = 0;

    for (std::size_t i = 0; i < snapshot.points.size(); i += stride) {
        if (addedFromScan >= maxFromScan) break;

        const auto& p = snapshot.points[i];
        if (p.dist <= 0.05) continue;

        const long long key = makeCellKey(p.x, p.y, cfg_.gridResolutionM);
        if (occupiedCells_.find(key) != occupiedCells_.end()) {
            continue;
        }

        PreviewPoint pp;
        pp.xM = p.x;
        pp.yM = p.y;
        pp.distanceM = p.dist;
        pp.angleDeg = angleDeg(p.x, p.y);
        pp.timestampMs = nowMs;
        pp.cellKey = key;

        previewPoints_.push_back(pp);
        occupiedCells_.insert(key);
        ++addedFromScan;
        trimPreviewToLimit();
    }
}

void ToClientLidarPreviewWriter::writeSummary(const RobotState& s, std::uint64_t nowMs)
{
    if (!summaryFile_.is_open()) {
        return;
    }

    summaryFile_ << std::fixed << std::setprecision(4);
    summaryFile_ << "{";
    summaryFile_ << "\"event_type\":\"lidar_summary\",";
    summaryFile_ << "\"timestamp_ms\":" << nowMs << ",";
    summaryFile_ << "\"lidar_valid\":" << boolText(s.lidar.valid) << ",";
    summaryFile_ << "\"lidar_fresh\":" << boolText(s.lidar.isFresh) << ",";
    summaryFile_ << "\"raw_point_count\":" << s.lidar.points.size() << ",";
    summaryFile_ << "\"preview_point_count\":" << previewPoints_.size() << ",";
    summaryFile_ << "\"grid_resolution_m\":" << cfg_.gridResolutionM << ",";
    summaryFile_ << "\"max_points_per_scan_for_client\":" << cfg_.maxPointsPerScanForClient << ",";
    summaryFile_ << "\"accumulate_only_while_moving\":" << boolText(cfg_.accumulateOnlyWhileMoving) << ",";
    summaryFile_ << "\"scans_accepted_for_preview\":" << scansAcceptedForPreview_ << ",";
    summaryFile_ << "\"scans_skipped_no_motion\":" << scansSkippedNoMotion_ << ",";
    summaryFile_ << "\"summary\":{";
    summaryFile_ << "\"valid\":" << boolText(s.lidarSummary.valid) << ",";
    summaryFile_ << "\"fresh\":" << boolText(s.lidarSummary.isFresh) << ",";
    summaryFile_ << "\"front_m\":" << s.lidarSummary.frontMinMeters << ",";
    summaryFile_ << "\"left_m\":" << s.lidarSummary.leftMinMeters << ",";
    summaryFile_ << "\"right_m\":" << s.lidarSummary.rightMinMeters << ",";
    summaryFile_ << "\"rear_m\":" << s.lidarSummary.rearMinMeters << ",";
    summaryFile_ << "\"front_close\":" << boolText(s.lidarSummary.frontObstacleClose) << ",";
    summaryFile_ << "\"left_close\":" << boolText(s.lidarSummary.leftObstacleClose) << ",";
    summaryFile_ << "\"right_close\":" << boolText(s.lidarSummary.rightObstacleClose) << ",";
    summaryFile_ << "\"any_close\":" << boolText(s.lidarSummary.obstacleClose);
    summaryFile_ << "},";
    summaryFile_ << "\"pose\":{";
    summaryFile_ << "\"valid\":" << boolText(s.lidarPose.valid) << ",";
    summaryFile_ << "\"fresh\":" << boolText(s.lidarPose.isFresh) << ",";
    summaryFile_ << "\"confidence\":" << s.lidarPose.confidence << ",";
    summaryFile_ << "\"center_error_m\":" << s.lidarPose.centerErrorM << ",";
    summaryFile_ << "\"heading_hint_deg\":" << s.lidarPose.headingHintDeg << ",";
    summaryFile_ << "\"front_clearance_m\":" << s.lidarPose.frontClearanceM << ",";
    summaryFile_ << "\"obstacle_ahead\":" << boolText(s.lidarPose.obstacleAhead);
    summaryFile_ << "}";
    summaryFile_ << "}\n";
    summaryFile_.flush();
    ++summariesWritten_;
}

void ToClientLidarPreviewWriter::writePreviewFile(std::uint64_t stoppedAtMs, const std::string& reason)
{
    std::ofstream out(previewPath_, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[toClient][lidar] failed to write preview JSON: " << previewPath_ << "\n";
        return;
    }

    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"schema_version\": 2,\n";
    out << "  \"kind\": \"to_client_lidar_preview\",\n";
    out << "  \"stopped_at_ms\": " << stoppedAtMs << ",\n";
    out << "  \"stop_reason\": " << jsonText(reason) << ",\n";
    out << "  \"max_preview_points\": " << cfg_.maxPreviewPoints << ",\n";
    out << "  \"grid_resolution_m\": " << cfg_.gridResolutionM << ",\n";
    out << "  \"max_points_per_scan_for_client\": " << cfg_.maxPointsPerScanForClient << ",\n";
    out << "  \"accumulate_only_while_moving\": " << boolText(cfg_.accumulateOnlyWhileMoving) << ",\n";
    out << "  \"scans_accepted_for_preview\": " << scansAcceptedForPreview_ << ",\n";
    out << "  \"scans_skipped_no_motion\": " << scansSkippedNoMotion_ << ",\n";
    out << "  \"point_count\": " << previewPoints_.size() << ",\n";
    out << "  \"coordinate_frame\": \"robot_base_lidar_2d_compact_preview\",\n";
    out << "  \"units\": {\"x\": \"m\", \"y\": \"m\", \"distance\": \"m\", \"angle\": \"deg\"},\n";
    out << "  \"points\": [\n";
    for (std::size_t i = 0; i < previewPoints_.size(); ++i) {
        const auto& p = previewPoints_[i];
        out << "    {\"x_m\": " << p.xM
            << ", \"y_m\": " << p.yM
            << ", \"distance_m\": " << p.distanceM
            << ", \"angle_deg\": " << p.angleDeg
            << ", \"timestamp_ms\": " << p.timestampMs << "}";
        if (i + 1 < previewPoints_.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

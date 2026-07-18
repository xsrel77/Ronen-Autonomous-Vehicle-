#include "Debugging/toClient/ToClientJsonLogger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

ToClientJsonLogger::~ToClientJsonLogger()
{
    stop("destructor", 0);
}

bool ToClientJsonLogger::start(const std::string& jsonlPath,
                               const std::string& latestJsonPath)
{
    Config cfg;
    cfg.baseDir = parentPathOrBase(jsonlPath, "Debugging/toClient");

    // If old code passed Debugging/toClient/to_client_latest.json, keep the new
    // latest.json inside the session. The top-level latest_session.json points
    // to the newest session for the Next.js app.
    (void)latestJsonPath;

    return startSession(cfg.baseDir, 0, cfg);
}

bool ToClientJsonLogger::startSession(const std::string& baseDir,
                                      std::uint64_t startedAtMs,
                                      const Config& cfg)
{
    stop("restart", startedAtMs);

    cfg_ = cfg;
    cfg_.baseDir = baseDir.empty() ? "Debugging/toClient" : baseDir;

    startedAtMs_ = startedAtMs;
    startedAtLocal_ = nowLocalString();
    sessionId_ = "session_" + fileTimeString();
    sessionDir_ = (fs::path(cfg_.baseDir) / "sessions" / sessionId_).string();

    okImagesDir_ = (fs::path(sessionDir_) / "images_ok").string();
    okRawImagesDir_ = (fs::path(sessionDir_) / "images_ok_raw").string();
    weakImagesDir_ = (fs::path(sessionDir_) / "images_weak_noise").string();
    weakRawImagesDir_ = (fs::path(sessionDir_) / "images_weak_noise_raw").string();
    videosDir_ = (fs::path(sessionDir_) / "videos").string();
    lidarDir_ = (fs::path(sessionDir_) / "lidar").string();

    manifestPath_ = (fs::path(sessionDir_) / "session_manifest.json").string();
    latestPath_ = (fs::path(sessionDir_) / "latest.json").string();
    timelinePath_ = (fs::path(sessionDir_) / "robot_timeline.jsonl").string();
    eventsPath_ = (fs::path(sessionDir_) / "detection_events.jsonl").string();
    rawCandidatesPath_ = (fs::path(sessionDir_) / "raw_candidates.jsonl").string();
    mapPoseTimelinePath_ = (fs::path(sessionDir_) / "map_pose_timeline.jsonl").string();
    mapOverlaySummaryPath_ = (fs::path(sessionDir_) / "map_overlay_summary.json").string();
    detectionsOnMapPath_ = (fs::path(sessionDir_) / "detections_on_map.jsonl").string();
    ros2MapAssetsDir_ = (fs::path(sessionDir_) / "ros2_map").string();
    copiedMapSessionDir_.clear();
    copiedMapYamlRel_.clear();
    copiedMapPgmRel_.clear();
    copiedLatestMapRel_.clear();
    videoPath_ = (fs::path(videosDir_) / ("detection_video_" + fileTimeString() + ".mp4")).string();

    try {
        fs::create_directories(okImagesDir_);
        fs::create_directories(okRawImagesDir_);
        fs::create_directories(weakImagesDir_);
        fs::create_directories(weakRawImagesDir_);
        fs::create_directories(videosDir_);
        fs::create_directories(lidarDir_);
        fs::create_directories(ros2MapAssetsDir_);
    } catch (const std::exception& e) {
        std::cerr << "[toClient] failed to create session directories: " << e.what() << "\n";
        enabled_ = false;
        return false;
    }

    timelineFile_.open(timelinePath_, std::ios::out | std::ios::trunc);
    if (!timelineFile_.is_open()) {
        std::cerr << "[toClient] failed to open timeline file: " << timelinePath_ << "\n";
        enabled_ = false;
        return false;
    }

    eventsFile_.open(eventsPath_, std::ios::out | std::ios::trunc);
    if (!eventsFile_.is_open()) {
        std::cerr << "[toClient] failed to open detection events file: " << eventsPath_ << "\n";
        timelineFile_.close();
        enabled_ = false;
        return false;
    }

    rawCandidatesFile_.open(rawCandidatesPath_, std::ios::out | std::ios::trunc);
    if (!rawCandidatesFile_.is_open()) {
        std::cerr << "[toClient] failed to open raw candidates file: " << rawCandidatesPath_ << "\n";
        eventsFile_.close();
        timelineFile_.close();
        enabled_ = false;
        return false;
    }

    mapPoseTimelineFile_.open(mapPoseTimelinePath_, std::ios::out | std::ios::trunc);
    if (!mapPoseTimelineFile_.is_open()) {
        std::cerr << "[toClient] failed to open map pose timeline file: " << mapPoseTimelinePath_ << "\n";
        rawCandidatesFile_.close();
        eventsFile_.close();
        timelineFile_.close();
        enabled_ = false;
        return false;
    }

    detectionsOnMapFile_.open(detectionsOnMapPath_, std::ios::out | std::ios::trunc);
    if (!detectionsOnMapFile_.is_open()) {
        std::cerr << "[toClient] failed to open detections on map file: " << detectionsOnMapPath_ << "\n";
        mapPoseTimelineFile_.close();
        rawCandidatesFile_.close();
        eventsFile_.close();
        timelineFile_.close();
        enabled_ = false;
        return false;
    }

    ToClientVideoRecorder::Config videoCfg;
    videoCfg.targetFps = cfg_.videoFps;
    videoCfg.maxWidth = cfg_.videoMaxWidth;
    videoCfg.maxHeight = cfg_.videoMaxHeight;
    videoCfg.h264BitrateKbps = cfg_.videoBitrateKbps;
    videoCfg.maxBytes = cfg_.videoMaxBytes;
    videoCfg.jpegQuality = cfg_.jpegQuality;
    videoRecorder_.start(videoPath_, startedAtMs_, videoCfg);

    ToClientLidarPreviewWriter::Config lidarCfg;
    lidarCfg.summaryPeriodMs = 1000;
    lidarCfg.maxPreviewPoints = 6000;
    lidarCfg.pointStride = 1;
    lidarCfg.maxPointsPerScanForClient = 220;
    lidarCfg.gridResolutionM = 0.08;
    lidarCfg.accumulateOnlyWhileMoving = true;
    lidarCfg.driveCommandThreshold = 1.0;
    lidarCfg.odomDeltaMinM = 0.04;
    lidarCfg.odomDeltaMinYawDeg = 2.0;
    lidarPreview_.start((fs::path(lidarDir_) / "lidar_summary.jsonl").string(),
                        (fs::path(lidarDir_) / "lidar_map_preview.json").string(),
                        lidarCfg);

    enabled_ = true;
    lastTimelineMs_ = 0;
    lastLatestMs_ = 0;
    lastOkImageMs_ = 0;
    lastWeakImageMs_ = 0;
    timelineRows_ = 0;
    detectionEvents_ = 0;
    rawCandidateEvents_ = 0;
    mapPoseRows_ = 0;
    detectionsOnMapEvents_ = 0;
    okImages_ = 0;
    weakImages_ = 0;

    mapSummaryHaveFirst_ = false;
    mapSummaryHaveLast_ = false;
    mapAssetsCopied_ = false;
    mapAssetsCopyError_.clear();
    lastMapLoaded_ = false;
    lastMapValid_ = false;
    lastMapManualStartSet_ = false;
    lastMapPoseValid_ = false;
    lastMapSessionDir_.clear();
    lastMapYaml_.clear();
    lastMapPgm_.clear();

    writeManifest("running", 0);

    // Stable top-level pointer for the Next.js side.
    const std::string latestSessionPath = (fs::path(cfg_.baseDir) / "latest_session.json").string();
    std::ofstream latestSession(latestSessionPath, std::ios::out | std::ios::trunc);
    if (latestSession.is_open()) {
        latestSession << "{\n";
        latestSession << "  \"session_id\": " << jsonText(sessionId_) << ",\n";
        latestSession << "  \"session_dir\": " << jsonText(sessionDir_) << ",\n";
        latestSession << "  \"manifest_path\": " << jsonText(manifestPath_) << ",\n";
        latestSession << "  \"started_at_local\": " << jsonText(startedAtLocal_) << "\n";
        latestSession << "}\n";
    }

    std::cout << "[toClient] L1 START session: " << sessionDir_ << "\n";
    return true;
}

void ToClientJsonLogger::stop(const std::string& reason, std::uint64_t stoppedAtMs)
{
    const bool wasEnabled = enabled_ || timelineFile_.is_open() || eventsFile_.is_open();

    videoRecorder_.stop(reason, stoppedAtMs);
    lidarPreview_.stop(stoppedAtMs, reason);

    if (wasEnabled) {
        copyRos2MapAssetsToSession();
        writeMapOverlaySummary(reason, stoppedAtMs);
    }

    if (mapPoseTimelineFile_.is_open()) {
        mapPoseTimelineFile_.flush();
        mapPoseTimelineFile_.close();
    }
    if (detectionsOnMapFile_.is_open()) {
        detectionsOnMapFile_.flush();
        detectionsOnMapFile_.close();
    }

    if (timelineFile_.is_open()) {
        timelineFile_.flush();
        timelineFile_.close();
    }
    if (eventsFile_.is_open()) {
        eventsFile_.flush();
        eventsFile_.close();
    }
    if (rawCandidatesFile_.is_open()) {
        rawCandidatesFile_.flush();
        rawCandidatesFile_.close();
    }

    if (wasEnabled && !manifestPath_.empty()) {
        writeManifest(reason, stoppedAtMs);
        std::cout << "[toClient] L2 STOP session: " << sessionDir_
                  << " reason=" << reason << "\n";
    }

    enabled_ = false;
}

bool ToClientJsonLogger::isEnabled() const
{
    return enabled_ && timelineFile_.is_open() && eventsFile_.is_open() && rawCandidatesFile_.is_open();
}

const std::string& ToClientJsonLogger::sessionDir() const
{
    return sessionDir_;
}

const std::string& ToClientJsonLogger::sessionId() const
{
    return sessionId_;
}

const char* ToClientJsonLogger::boolText(bool v)
{
    return v ? "true" : "false";
}

std::string ToClientJsonLogger::jsonText(const std::string& value)
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

std::string ToClientJsonLogger::nowLocalString()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmValue{};
    localtime_r(&t, &tmValue);

    std::ostringstream oss;
    oss << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string ToClientJsonLogger::fileTimeString()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmValue{};
    localtime_r(&t, &tmValue);

    std::ostringstream oss;
    oss << std::put_time(&tmValue, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string ToClientJsonLogger::basename(const std::string& path)
{
    return fs::path(path).filename().string();
}

std::string ToClientJsonLogger::parentPathOrBase(const std::string& path, const std::string& fallback)
{
    if (path.empty()) {
        return fallback;
    }
    fs::path p(path);
    if (p.has_parent_path()) {
        return p.parent_path().string();
    }
    return fallback;
}

std::string ToClientJsonLogger::makeRelativePath(const std::string& base, const std::string& full)
{
    try {
        return fs::relative(fs::path(full), fs::path(base)).string();
    } catch (...) {
        return full;
    }
}

const Detection* ToClientJsonLogger::findBestDetection(const DetectionSnapshot& snapshot)
{
    const Detection* best = nullptr;
    for (const auto& d : snapshot.detections) {
        if (!d.valid || d.weak || d.displaySuppressed) {
            continue;
        }
        if (!best || d.confidence > best->confidence) {
            best = &d;
        }
    }
    return best;
}

void ToClientJsonLogger::countDetections(const DetectionSnapshot& snapshot,
                                         int& acceptedCount,
                                         int& weakCount,
                                         int& rejectedCount)
{
    acceptedCount = 0;
    weakCount = 0;
    rejectedCount = 0;

    for (const auto& d : snapshot.detections) {
        if (d.displaySuppressed) {
            ++rejectedCount;
        } else if (d.valid && !d.weak) {
            ++acceptedCount;
        } else if (d.weak) {
            ++weakCount;
        } else {
            ++rejectedCount;
        }
    }
}

namespace {
struct V32DetectionCounts {
    int heuristic = 0;
    int review = 0;
    int colorCorrected = 0;
    int noise = 0;
};

V32DetectionCounts countV32Detections(const DetectionSnapshot& snapshot)
{
    V32DetectionCounts out;
    for (const auto& d : snapshot.detections) {
        if (d.displaySuppressed) {
            continue;
        }
        if (d.heuristic || d.sourceType == "heuristic" || d.policyStatus == "heuristic") {
            ++out.heuristic;
        }
        if (d.reviewCandidate || d.policyStatus == "review") {
            ++out.review;
        }
        if (d.colorCorrectedByPolicy || d.policyStatus == "color_corrected") {
            ++out.colorCorrected;
        }
        if (d.policyStatus == "noise") {
            ++out.noise;
        }
    }
    return out;
}
}

void ToClientJsonLogger::writeCameraServo(std::ostream& os,
                                           const CameraServoState& servo,
                                           bool pretty,
                                           const char* indent) const
{
    const char* nl = pretty ? "\n" : "";
    const char* sp = pretty ? " " : "";
    const std::string i1 = indent ? indent : "";
    const std::string i2 = i1 + (pretty ? "  " : "");

    os << "{" << nl;
    os << i2 << "\"valid\":" << sp << boolText(servo.valid) << "," << nl;
    os << i2 << "\"pan_servo_lower_deg\":" << sp << servo.panDeg << "," << nl;
    os << i2 << "\"tilt_servo_upper_deg\":" << sp << servo.tiltDeg << "," << nl;
    os << i2 << "\"center_pan_deg\":" << sp << servo.centerPanDeg << "," << nl;
    os << i2 << "\"center_tilt_deg\":" << sp << servo.centerTiltDeg << "," << nl;
    os << i2 << "\"pan_relative_deg\":" << sp << servo.panRelativeDeg << "," << nl;
    os << i2 << "\"tilt_relative_deg\":" << sp << servo.tiltRelativeDeg << "," << nl;
    os << i2 << "\"direction_robot\":" << sp << "{";
    os << "\"x_forward\":" << servo.dirForward << ",";
    os << "\"y_left\":" << servo.dirLeft << ",";
    os << "\"z_up\":" << servo.dirUp << "}," << nl;
    os << i2 << "\"digital_zoom\":" << sp << servo.digitalZoom << "," << nl;
    os << i2 << "\"timestamp_ms\":" << sp << servo.timestampMs << nl;
    os << i1 << "}";
}

void ToClientJsonLogger::writeDetectionVector(std::ostream& os,
                                               const std::vector<Detection>& detections,
                                               bool pretty,
                                               const char* indent) const
{
    const char* nl = pretty ? "\n" : "";
    const std::string i1 = indent ? indent : "";
    const std::string i2 = i1 + (pretty ? "  " : "");

    os << "[";
    for (std::size_t idx = 0; idx < detections.size(); ++idx) {
        const auto& d = detections[idx];
        if (idx > 0) {
            os << ",";
        }

        os << nl << i2 << "{";
        os << "\"label\":" << jsonText(d.label) << ",";
        os << "\"class_id\":" << d.classId << ",";
        os << "\"confidence\":" << d.confidence << ",";
        os << "\"current_confidence\":" << d.currentConfidence << ",";
        os << "\"best_confidence\":" << d.bestConfidence << ",";
        os << "\"track_stable_best\":" << boolText(d.trackStableBest) << ",";
        os << "\"valid\":" << boolText(d.valid) << ",";
        os << "\"weak\":" << boolText(d.weak) << ",";
        os << "\"source_type\":" << jsonText(d.sourceType) << ",";
        os << "\"policy_status\":" << jsonText(d.policyStatus) << ",";
        os << "\"heuristic\":" << boolText(d.heuristic) << ",";
        os << "\"review_candidate\":" << boolText(d.reviewCandidate) << ",";
        os << "\"review_reason\":" << jsonText(d.reviewReason) << ",";
        os << "\"color_corrected_by_policy\":" << boolText(d.colorCorrectedByPolicy) << ",";
        os << "\"correction_reason\":" << jsonText(d.correctionReason) << ",";
        os << "\"color_correction_score\":" << d.colorCorrectionScore << ",";
        os << "\"display_suppressed\":" << boolText(d.displaySuppressed) << ",";
        os << "\"reject_reason\":" << jsonText(d.rejectReason) << ",";
        os << "\"maturity_competition_winner\":" << boolText(d.maturityCompetitionWinner) << ",";
        os << "\"lost_maturity_competition\":" << boolText(d.lostMaturityCompetition) << ",";
        os << "\"class_locked\":" << boolText(d.classLocked) << ",";
        os << "\"switch_candidate_frames\":" << d.switchCandidateFrames << ",";
        os << "\"maturity_score\":" << d.maturityScore << ",";
        os << "\"maturity_score_ripe\":" << d.maturityScoreRipe << ",";
        os << "\"maturity_score_unripe\":" << d.maturityScoreUnripe << ",";
        os << "\"maturity_competition_reason\":" << jsonText(d.maturityCompetitionReason) << ",";
        os << "\"cluster_promoted\":" << boolText(d.clusterPromoted) << ",";
        os << "\"class_corrected\":" << boolText(d.classCorrected) << ",";
        os << "\"original_class_id\":" << d.originalClassId << ",";
        os << "\"original_label\":" << jsonText(d.originalLabel) << ",";
        os << "\"display_source\":" << jsonText(d.displaySource) << ",";
        os << "\"promotion_reason\":" << jsonText(d.promotionReason) << ",";
        os << "\"track_id\":" << d.trackId << ",";
        os << "\"track_hits\":" << d.trackHits << ",";
        os << "\"track_age\":" << d.trackAge << ",";
        os << "\"roi_pass\":" << boolText(d.roiPass) << ",";
        os << "\"roi_group_size\":" << d.roiGroupSize << ",";
        os << "\"roi_reason\":" << jsonText(d.roiReason) << ",";
        os << "\"roi_source_accepted_count\":" << d.roiSourceAcceptedCount << ",";
        os << "\"roi_source_weak_count\":" << d.roiSourceWeakCount << ",";
        os << "\"roi_padding_ratio\":" << d.roiPaddingRatio << ",";
        os << "\"bbox\":{";
        os << "\"x\":" << d.x << ",";
        os << "\"y\":" << d.y << ",";
        os << "\"w\":" << d.w << ",";
        os << "\"h\":" << d.h << "},";
        os << "\"original_bbox\":{";
        os << "\"valid\":" << boolText(d.hasOriginalBbox) << ",";
        os << "\"x\":" << d.originalX << ",";
        os << "\"y\":" << d.originalY << ",";
        os << "\"w\":" << d.originalW << ",";
        os << "\"h\":" << d.originalH << "},";
        os << "\"refined_bbox\":{";
        os << "\"valid\":" << boolText(d.hasRefinedBbox) << ",";
        os << "\"x\":" << d.refinedX << ",";
        os << "\"y\":" << d.refinedY << ",";
        os << "\"w\":" << d.refinedW << ",";
        os << "\"h\":" << d.refinedH << "},";
        os << "\"support\":{";
        os << "\"member_count\":" << d.supportMemberCount << ",";
        os << "\"ripe_count\":" << d.supportRipeCount << ",";
        os << "\"unripe_count\":" << d.supportUnripeCount << ",";
        os << "\"conf_sum\":" << d.supportConfSum << ",";
        os << "\"ripe_conf_sum\":" << d.supportRipeConfSum << ",";
        os << "\"unripe_conf_sum\":" << d.supportUnripeConfSum << ",";
        os << "\"score\":" << d.bunchSupportScore << "},";
        os << "\"heuristic_meta\":{";
        os << "\"type\":" << jsonText(d.heuristicType) << ",";
        os << "\"anchor_bunch_class\":" << jsonText(d.anchorBunchClass) << ",";
        os << "\"anchor_bunch_confidence\":" << d.anchorBunchConfidence << ",";
        os << "\"child_count\":" << d.childCount << ",";
        os << "\"weak_child_count\":" << d.weakChildCount << ",";
        os << "\"strong_child_count\":" << d.strongChildCount << ",";
        os << "\"weighted_child_count\":" << d.weightedChildCount << ",";
        os << "\"ripe_evidence_score\":" << d.ripeEvidenceScore << ",";
        os << "\"unripe_evidence_score\":" << d.unripeEvidenceScore << ",";
        os << "\"dominant_maturity\":" << jsonText(d.dominantMaturity) << ",";
        os << "\"score\":" << d.heuristicScore << "},";
        os << "\"metrics\":{";
        os << "\"box_area\":" << d.boxArea << ",";
        os << "\"mask_area\":" << d.maskArea << ",";
        os << "\"mask_density\":" << d.maskDensity << ",";
        os << "\"red_ratio\":" << d.redRatio << ",";
        os << "\"orange_ratio\":" << d.orangeRatio << ",";
        os << "\"warm_ratio\":" << d.warmRatio << ",";
        os << "\"green_yellow_ratio\":" << d.greenYellowRatio;
        os << "}";
        os << "}";
    }
    if (pretty && !detections.empty()) {
        os << nl << i1;
    }
    os << "]";
}

void ToClientJsonLogger::writeDetectionArray(std::ostream& os,
                                              const DetectionSnapshot& snapshot,
                                              bool pretty,
                                              const char* indent) const
{
    writeDetectionVector(os, snapshot.detections, pretty, indent);
}

void ToClientJsonLogger::writeClientRobotSummary(std::ostream& os,
                                                 const RobotState& s,
                                                 bool pretty) const
{
    const char* nl = pretty ? "\n" : "";
    const char* sp = pretty ? " " : "";
    const char* i1 = pretty ? "  " : "";
    const char* i2 = pretty ? "    " : "";

    int accepted = 0;
    int weak = 0;
    int rejected = 0;
    countDetections(s.detections, accepted, weak, rejected);
    const V32DetectionCounts v32Counts = countV32Detections(s.detections);
    const Detection* best = findBestDetection(s.detections);

    os << std::fixed << std::setprecision(6);
    os << "{" << nl;
    os << i1 << "\"schema_version\":" << sp << 3 << "," << nl;
    os << i1 << "\"session_id\":" << sp << jsonText(sessionId_) << "," << nl;
    os << i1 << "\"timestamp_ms\":" << sp << s.timestampMs << "," << nl;
    os << i1 << "\"timestamp_local\":" << sp << jsonText(nowLocalString()) << "," << nl;

    os << i1 << "\"robot\":" << sp << "{";
    os << "\"emergency_stop\":" << boolText(s.emergencyStop) << ",";
    os << "\"status_text\":" << jsonText(s.behavior.statusText) << ",";
    os << "\"warning_active\":" << boolText(s.behavior.warningActive) << ",";
    os << "\"unified_gui_open\":" << boolText(s.unifiedGuiOpen) << "}," << nl;

    os << i1 << "\"drive\":" << sp << "{";
    os << "\"forward_speed\":" << s.drive.currentForwardSpeed << ",";
    os << "\"steering_speed\":" << s.drive.currentSteeringSpeed << ",";
    os << "\"fresh\":" << boolText(s.drive.isFresh) << "}," << nl;

    os << i1 << "\"camera_view\":" << sp;
    writeCameraServo(os, s.cameraServo, pretty, i2);
    os << "," << nl;

    os << i1 << "\"perception\":" << sp << "{" << nl;
    os << i2 << "\"frame\":" << sp << "{";
    os << "\"width\":" << s.detections.frame.width << ",";
    os << "\"height\":" << s.detections.frame.height << ",";
    os << "\"channels\":" << s.detections.frame.channels << ",";
    os << "\"timestamp_ms\":" << s.detections.frame.timestampMs << "}," << nl;
    os << i2 << "\"valid\":" << sp << boolText(s.detections.valid) << "," << nl;
    os << i2 << "\"fresh\":" << sp << boolText(s.detections.isFresh) << "," << nl;
    os << i2 << "\"accepted_count\":" << sp << accepted << "," << nl;
    os << i2 << "\"weak_count\":" << sp << weak << "," << nl;
    os << i2 << "\"rejected_count\":" << sp << rejected << "," << nl;
    os << i2 << "\"heuristic_count\":" << sp << v32Counts.heuristic << "," << nl;
    os << i2 << "\"review_count\":" << sp << v32Counts.review << "," << nl;
    os << i2 << "\"color_corrected_count\":" << sp << v32Counts.colorCorrected << "," << nl;
    os << i2 << "\"noise_count\":" << sp << v32Counts.noise << "," << nl;
    os << i2 << "\"best_detection\":" << sp;
    if (best) {
        os << "{";
        os << "\"label\":" << jsonText(best->label) << ",";
        os << "\"class_id\":" << best->classId << ",";
        os << "\"confidence\":" << best->confidence << ",";
        os << "\"track_id\":" << best->trackId << ",";
        os << "\"bbox\":{";
        os << "\"x\":" << best->x << ",";
        os << "\"y\":" << best->y << ",";
        os << "\"w\":" << best->w << ",";
        os << "\"h\":" << best->h << "}";
        os << "}";
    } else {
        os << "null";
    }
    os << "," << nl;
    os << i2 << "\"detections\":" << sp;
    writeDetectionArray(os, s.detections, pretty, i2);
    os << nl << i1 << "}," << nl;

    os << i1 << "\"lidar\":" << sp << "{";
    os << "\"valid\":" << boolText(s.lidar.valid) << ",";
    os << "\"fresh\":" << boolText(s.lidar.isFresh) << ",";
    os << "\"raw_point_count\":" << s.lidar.points.size() << ",";
    os << "\"preview_point_count\":" << lidarPreview_.previewPointCount() << ",";
    os << "\"front_m\":" << s.lidarSummary.frontMinMeters << ",";
    os << "\"left_m\":" << s.lidarSummary.leftMinMeters << ",";
    os << "\"right_m\":" << s.lidarSummary.rightMinMeters << ",";
    os << "\"rear_m\":" << s.lidarSummary.rearMinMeters << ",";
    os << "\"any_close\":" << boolText(s.lidarSummary.obstacleClose) << ",";
    os << "\"center_error_m\":" << s.lidarPose.centerErrorM << ",";
    os << "\"heading_hint_deg\":" << s.lidarPose.headingHintDeg << "}," << nl;

    os << i1 << "\"map_overlay\":" << sp << "{";
    os << "\"loaded\":" << boolText(s.ros2Map.loaded) << ",";
    os << "\"valid\":" << boolText(s.ros2Map.valid) << ",";
    os << "\"map_yaml\":" << jsonText(s.ros2Map.mapYaml) << ",";
    os << "\"map_pgm\":" << jsonText(s.ros2Map.mapPgm) << ",";
    os << "\"session_dir\":" << jsonText(s.ros2Map.sessionDir) << ",";
    os << "\"resolution_m\":" << s.ros2Map.resolutionM << ",";
    os << "\"width\":" << s.ros2Map.width << ",";
    os << "\"height\":" << s.ros2Map.height << ",";
    os << "\"origin_x\":" << s.ros2Map.originX << ",";
    os << "\"origin_y\":" << s.ros2Map.originY << ",";
    os << "\"manual_start_set\":" << boolText(s.ros2Map.manualStartSet) << ",";
    os << "\"manual_start_x\":" << s.ros2Map.manualStartX << ",";
    os << "\"manual_start_y\":" << s.ros2Map.manualStartY << ",";
    os << "\"manual_start_yaw_deg\":" << s.ros2Map.manualStartYawDeg << ",";
    os << "\"pose_valid\":" << boolText(s.ros2Map.poseValid) << ",";
    os << "\"robot_x\":" << s.ros2Map.robotX << ",";
    os << "\"robot_y\":" << s.ros2Map.robotY << ",";
    os << "\"robot_yaw_deg\":" << s.ros2Map.robotYawDeg << ",";
    os << "\"robot_distance_m\":" << s.ros2Map.robotDistanceM << ",";
    os << "\"trail_count\":" << s.ros2Map.trailCount;
    os << "}," << nl;

    os << i1 << "\"environment\":" << sp << "{";
    os << "\"valid\":" << boolText(s.m5stick.env.valid) << ",";
    os << "\"fresh\":" << boolText(s.m5stick.env.isFresh) << ",";
    os << "\"temp_c\":" << s.m5stick.env.tempC << ",";
    os << "\"humidity_pct\":" << s.m5stick.env.humidityPct << ",";
    os << "\"pressure_hpa\":" << s.m5stick.env.pressureHpa << "}," << nl;

    os << i1 << "\"paths\":" << sp << "{";
    os << "\"manifest\":" << jsonText(makeRelativePath(sessionDir_, manifestPath_)) << ",";
    os << "\"video\":" << jsonText(makeRelativePath(sessionDir_, videoPath_)) << ",";
    os << "\"detection_events\":" << jsonText(makeRelativePath(sessionDir_, eventsPath_)) << ",";
    os << "\"raw_candidates\":" << jsonText(makeRelativePath(sessionDir_, rawCandidatesPath_)) << ",";
    os << "\"lidar_summary\":" << jsonText(makeRelativePath(sessionDir_, lidarPreview_.summaryPath())) << ",";
    os << "\"lidar_preview\":" << jsonText(makeRelativePath(sessionDir_, lidarPreview_.previewPath()));
    os << "}" << nl;
    os << "}";
}


void ToClientJsonLogger::writeMapPoseObject(std::ostream& os,
                                            const RobotState& s,
                                            bool pretty,
                                            const char* indent) const
{
    const char* nl = pretty ? "\n" : "";
    const char* sp = pretty ? " " : "";
    const std::string i1 = indent ? indent : "";
    const std::string i2 = i1 + (pretty ? "  " : "");

    os << std::fixed << std::setprecision(6);
    os << "{" << nl;
    os << i2 << "\"loaded\":" << sp << boolText(s.ros2Map.loaded) << "," << nl;
    os << i2 << "\"valid\":" << sp << boolText(s.ros2Map.valid) << "," << nl;
    os << i2 << "\"pose_valid\":" << sp << boolText(s.ros2Map.poseValid) << "," << nl;
    os << i2 << "\"map_yaml\":" << sp << jsonText(s.ros2Map.mapYaml) << "," << nl;
    os << i2 << "\"map_pgm\":" << sp << jsonText(s.ros2Map.mapPgm) << "," << nl;
    os << i2 << "\"session_dir\":" << sp << jsonText(s.ros2Map.sessionDir) << "," << nl;
    os << i2 << "\"copied_map_yaml\":" << sp << jsonText(copiedMapYamlRel_) << "," << nl;
    os << i2 << "\"copied_map_pgm\":" << sp << jsonText(copiedMapPgmRel_) << "," << nl;
    os << i2 << "\"resolution_m\":" << sp << s.ros2Map.resolutionM << "," << nl;
    os << i2 << "\"width\":" << sp << s.ros2Map.width << "," << nl;
    os << i2 << "\"height\":" << sp << s.ros2Map.height << "," << nl;
    os << i2 << "\"origin_x\":" << sp << s.ros2Map.originX << "," << nl;
    os << i2 << "\"origin_y\":" << sp << s.ros2Map.originY << "," << nl;
    os << i2 << "\"origin_yaw_rad\":" << sp << s.ros2Map.originYawRad << "," << nl;
    os << i2 << "\"manual_start_set\":" << sp << boolText(s.ros2Map.manualStartSet) << "," << nl;
    os << i2 << "\"manual_start_x\":" << sp << s.ros2Map.manualStartX << "," << nl;
    os << i2 << "\"manual_start_y\":" << sp << s.ros2Map.manualStartY << "," << nl;
    os << i2 << "\"manual_start_yaw_deg\":" << sp << s.ros2Map.manualStartYawDeg << "," << nl;
    os << i2 << "\"robot_x\":" << sp << s.ros2Map.robotX << "," << nl;
    os << i2 << "\"robot_y\":" << sp << s.ros2Map.robotY << "," << nl;
    os << i2 << "\"robot_yaw_deg\":" << sp << s.ros2Map.robotYawDeg << "," << nl;
    os << i2 << "\"robot_distance_m\":" << sp << s.ros2Map.robotDistanceM << "," << nl;
    os << i2 << "\"trail_count\":" << sp << s.ros2Map.trailCount << nl;
    os << i1 << "}";
}

void ToClientJsonLogger::writeMapPoseTimeline(const RobotState& s)
{
    if (!mapPoseTimelineFile_.is_open()) {
        return;
    }

    mapPoseTimelineFile_ << std::fixed << std::setprecision(6);
    mapPoseTimelineFile_ << "{";
    mapPoseTimelineFile_ << "\"schema_version\":1,";
    mapPoseTimelineFile_ << "\"session_id\":" << jsonText(sessionId_) << ",";
    mapPoseTimelineFile_ << "\"timestamp_ms\":" << s.timestampMs << ",";
    mapPoseTimelineFile_ << "\"timestamp_local\":" << jsonText(nowLocalString()) << ",";
    mapPoseTimelineFile_ << "\"lidar_live\":" << boolText(s.lidar.valid && s.lidar.isFresh) << ",";
    mapPoseTimelineFile_ << "\"drive\":{";
    mapPoseTimelineFile_ << "\"forward_speed\":" << s.drive.currentForwardSpeed << ",";
    mapPoseTimelineFile_ << "\"steering_speed\":" << s.drive.currentSteeringSpeed << ",";
    mapPoseTimelineFile_ << "\"fresh\":" << boolText(s.drive.isFresh) << "},";
    mapPoseTimelineFile_ << "\"map_pose\":";
    writeMapPoseObject(mapPoseTimelineFile_, s, false, "");
    mapPoseTimelineFile_ << "}\n";
    mapPoseTimelineFile_.flush();
    ++mapPoseRows_;

    if (s.ros2Map.loaded && s.ros2Map.valid) {
        if (!mapSummaryHaveFirst_) {
            mapSummaryHaveFirst_ = true;
            firstMapTimestampMs_ = s.timestampMs;
            firstMapRobotX_ = s.ros2Map.robotX;
            firstMapRobotY_ = s.ros2Map.robotY;
            firstMapRobotYawDeg_ = s.ros2Map.robotYawDeg;
            firstMapRobotDistanceM_ = s.ros2Map.robotDistanceM;
        }
        mapSummaryHaveLast_ = true;
        lastMapTimestampMs_ = s.timestampMs;
        lastMapRobotX_ = s.ros2Map.robotX;
        lastMapRobotY_ = s.ros2Map.robotY;
        lastMapRobotYawDeg_ = s.ros2Map.robotYawDeg;
        lastMapRobotDistanceM_ = s.ros2Map.robotDistanceM;

        lastMapLoaded_ = s.ros2Map.loaded;
        lastMapValid_ = s.ros2Map.valid;
        lastMapManualStartSet_ = s.ros2Map.manualStartSet;
        lastMapPoseValid_ = s.ros2Map.poseValid;
        lastMapSessionDir_ = s.ros2Map.sessionDir;
        lastMapYaml_ = s.ros2Map.mapYaml;
        lastMapPgm_ = s.ros2Map.mapPgm;
        lastMapWidth_ = s.ros2Map.width;
        lastMapHeight_ = s.ros2Map.height;
        lastMapResolutionM_ = s.ros2Map.resolutionM;
        lastMapOriginX_ = s.ros2Map.originX;
        lastMapOriginY_ = s.ros2Map.originY;
        lastMapOriginYawRad_ = s.ros2Map.originYawRad;
        lastManualStartX_ = s.ros2Map.manualStartX;
        lastManualStartY_ = s.ros2Map.manualStartY;
        lastManualStartYawDeg_ = s.ros2Map.manualStartYawDeg;
        lastTrailCount_ = s.ros2Map.trailCount;
    }
}

void ToClientJsonLogger::copyRos2MapAssetsToSession()
{
    if (ros2MapAssetsDir_.empty() || !lastMapLoaded_ || !lastMapValid_) {
        return;
    }

    mapAssetsCopied_ = false;
    mapAssetsCopyError_.clear();
    copiedMapSessionDir_.clear();
    copiedMapYamlRel_.clear();
    copiedMapPgmRel_.clear();
    copiedLatestMapRel_.clear();

    try {
        fs::create_directories(ros2MapAssetsDir_);

        const fs::path latestSrc = fs::path("maps") / "ros2_slam" / "latest_map.json";
        if (fs::exists(latestSrc)) {
            const fs::path latestDst = fs::path(ros2MapAssetsDir_) / "latest_map.json";
            fs::copy_file(latestSrc, latestDst, fs::copy_options::overwrite_existing);
            copiedLatestMapRel_ = makeRelativePath(sessionDir_, latestDst.string());
        }

        fs::path srcSession = lastMapSessionDir_.empty() ? fs::path() : fs::path(lastMapSessionDir_);
        if (!srcSession.empty() && fs::exists(srcSession) && fs::is_directory(srcSession)) {
            const fs::path dstParent = fs::path(ros2MapAssetsDir_) / "map_session";
            fs::create_directories(dstParent);
            const fs::path dstSession = dstParent / srcSession.filename();
            fs::remove_all(dstSession);
            fs::copy(srcSession, dstSession,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            copiedMapSessionDir_ = makeRelativePath(sessionDir_, dstSession.string());

            const fs::path srcYaml = lastMapYaml_.empty() ? fs::path() : fs::path(lastMapYaml_);
            const fs::path srcPgm = lastMapPgm_.empty() ? fs::path() : fs::path(lastMapPgm_);
            if (!srcYaml.empty()) {
                copiedMapYamlRel_ = makeRelativePath(sessionDir_, (dstSession / srcYaml.filename()).string());
            }
            if (!srcPgm.empty()) {
                copiedMapPgmRel_ = makeRelativePath(sessionDir_, (dstSession / srcPgm.filename()).string());
            }
        }

        mapAssetsCopied_ = true;
    } catch (const std::exception& e) {
        mapAssetsCopyError_ = e.what();
        std::cerr << "[toClient] failed to copy ROS2 map assets: " << mapAssetsCopyError_ << "\n";
    }
}

void ToClientJsonLogger::writeMapOverlaySummary(const std::string& stopReason,
                                                std::uint64_t stoppedAtMs)
{
    if (mapOverlaySummaryPath_.empty()) {
        return;
    }

    std::ofstream out(mapOverlaySummaryPath_, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"kind\": \"rbv2_map_overlay_summary\",\n";
    out << "  \"session_id\": " << jsonText(sessionId_) << ",\n";
    out << "  \"stop_reason\": " << jsonText(stopReason) << ",\n";
    out << "  \"stopped_at_ms\": " << stoppedAtMs << ",\n";
    out << "  \"map_assets_copied\": " << boolText(mapAssetsCopied_) << ",\n";
    out << "  \"map_assets_copy_error\": " << jsonText(mapAssetsCopyError_) << ",\n";
    out << "  \"paths\": {\n";
    out << "    \"map_pose_timeline\": " << jsonText(makeRelativePath(sessionDir_, mapPoseTimelinePath_)) << ",\n";
    out << "    \"detections_on_map\": " << jsonText(makeRelativePath(sessionDir_, detectionsOnMapPath_)) << ",\n";
    out << "    \"copied_latest_map\": " << jsonText(copiedLatestMapRel_) << ",\n";
    out << "    \"copied_map_session_dir\": " << jsonText(copiedMapSessionDir_) << ",\n";
    out << "    \"copied_map_yaml\": " << jsonText(copiedMapYamlRel_) << ",\n";
    out << "    \"copied_map_pgm\": " << jsonText(copiedMapPgmRel_) << "\n";
    out << "  },\n";
    out << "  \"map\": {\n";
    out << "    \"loaded\": " << boolText(lastMapLoaded_) << ",\n";
    out << "    \"valid\": " << boolText(lastMapValid_) << ",\n";
    out << "    \"map_yaml\": " << jsonText(lastMapYaml_) << ",\n";
    out << "    \"map_pgm\": " << jsonText(lastMapPgm_) << ",\n";
    out << "    \"session_dir\": " << jsonText(lastMapSessionDir_) << ",\n";
    out << "    \"width\": " << lastMapWidth_ << ",\n";
    out << "    \"height\": " << lastMapHeight_ << ",\n";
    out << "    \"resolution_m\": " << lastMapResolutionM_ << ",\n";
    out << "    \"origin_x\": " << lastMapOriginX_ << ",\n";
    out << "    \"origin_y\": " << lastMapOriginY_ << ",\n";
    out << "    \"origin_yaw_rad\": " << lastMapOriginYawRad_ << "\n";
    out << "  },\n";
    out << "  \"manual_start\": {\n";
    out << "    \"set\": " << boolText(lastMapManualStartSet_) << ",\n";
    out << "    \"x\": " << lastManualStartX_ << ",\n";
    out << "    \"y\": " << lastManualStartY_ << ",\n";
    out << "    \"yaw_deg\": " << lastManualStartYawDeg_ << "\n";
    out << "  },\n";
    out << "  \"first_pose\": {\n";
    out << "    \"available\": " << boolText(mapSummaryHaveFirst_) << ",\n";
    out << "    \"timestamp_ms\": " << firstMapTimestampMs_ << ",\n";
    out << "    \"x\": " << firstMapRobotX_ << ",\n";
    out << "    \"y\": " << firstMapRobotY_ << ",\n";
    out << "    \"yaw_deg\": " << firstMapRobotYawDeg_ << ",\n";
    out << "    \"distance_m\": " << firstMapRobotDistanceM_ << "\n";
    out << "  },\n";
    out << "  \"final_pose\": {\n";
    out << "    \"available\": " << boolText(mapSummaryHaveLast_) << ",\n";
    out << "    \"timestamp_ms\": " << lastMapTimestampMs_ << ",\n";
    out << "    \"x\": " << lastMapRobotX_ << ",\n";
    out << "    \"y\": " << lastMapRobotY_ << ",\n";
    out << "    \"yaw_deg\": " << lastMapRobotYawDeg_ << ",\n";
    out << "    \"distance_m\": " << lastMapRobotDistanceM_ << "\n";
    out << "  },\n";
    out << "  \"counts\": {\n";
    out << "    \"map_pose_rows\": " << mapPoseRows_ << ",\n";
    out << "    \"detections_on_map_events\": " << detectionsOnMapEvents_ << ",\n";
    out << "    \"trail_count\": " << lastTrailCount_ << "\n";
    out << "  }\n";
    out << "}\n";
}

void ToClientJsonLogger::writeTimeline(const RobotState& state)
{
    if (!timelineFile_.is_open()) {
        return;
    }

    writeClientRobotSummary(timelineFile_, state, false);
    timelineFile_ << "\n";
    timelineFile_.flush();
    ++timelineRows_;

    writeMapPoseTimeline(state);
}

void ToClientJsonLogger::writeLatest(const RobotState& state)
{
    if (latestPath_.empty()) {
        return;
    }

    const std::string tmpPath = latestPath_ + ".tmp";
    std::ofstream latest(tmpPath, std::ios::out | std::ios::trunc);
    if (!latest.is_open()) {
        return;
    }

    writeClientRobotSummary(latest, state, true);
    latest << "\n";
    latest.flush();
    latest.close();

    std::remove(latestPath_.c_str());
    std::rename(tmpPath.c_str(), latestPath_.c_str());
}

void ToClientJsonLogger::writeDetectionEvent(std::ostream& os,
                                             const std::string& eventType,
                                             const std::string& annotatedImagePathRel,
                                             const std::string& rawImagePathRel,
                                             const RobotState& s,
                                             int acceptedCount,
                                             int weakCount,
                                             int rejectedCount)
{
    os << std::fixed << std::setprecision(6);
    os << "{";
    os << "\"schema_version\":3,";
    os << "\"session_id\":" << jsonText(sessionId_) << ",";
    os << "\"event_type\":" << jsonText(eventType) << ",";
    os << "\"timestamp_ms\":" << s.timestampMs << ",";
    os << "\"timestamp_local\":" << jsonText(nowLocalString()) << ",";
    os << "\"image_path\":" << jsonText(annotatedImagePathRel) << ",";
    os << "\"annotated_image_path\":" << jsonText(annotatedImagePathRel) << ",";
    os << "\"raw_image_path\":" << jsonText(rawImagePathRel) << ",";
    os << "\"frame\":{";
    os << "\"width\":" << s.detections.frame.width << ",";
    os << "\"height\":" << s.detections.frame.height << ",";
    os << "\"channels\":" << s.detections.frame.channels << ",";
    os << "\"timestamp_ms\":" << s.detections.frame.timestampMs << "},";
    os << "\"camera_view\":";
    writeCameraServo(os, s.cameraServo, false, "");
    os << ",";
    const V32DetectionCounts v32Counts = countV32Detections(s.detections);
    os << "\"accepted_count\":" << acceptedCount << ",";
    os << "\"weak_count\":" << weakCount << ",";
    os << "\"rejected_count\":" << rejectedCount << ",";
    os << "\"heuristic_count\":" << v32Counts.heuristic << ",";
    os << "\"review_count\":" << v32Counts.review << ",";
    os << "\"color_corrected_count\":" << v32Counts.colorCorrected << ",";
    os << "\"noise_count\":" << v32Counts.noise << ",";
    os << "\"detections\":";
    writeDetectionArray(os, s.detections, false, "");
    os << ",";
    os << "\"map_pose\":";
    writeMapPoseObject(os, s, false, "");
    os << ",";
    os << "\"detections_on_map\":";
    writeMapDetectionProjectionArray(os, s);
    os << "}\n";
    os.flush();
}

void ToClientJsonLogger::writeDetectionsOnMapEvent(const std::string& eventType,
                                                   const std::string& annotatedImagePathRel,
                                                   const std::string& rawImagePathRel,
                                                   const RobotState& s,
                                                   int acceptedCount,
                                                   int weakCount,
                                                   int rejectedCount)
{
    if (!detectionsOnMapFile_.is_open()) {
        return;
    }

    detectionsOnMapFile_ << std::fixed << std::setprecision(6);
    detectionsOnMapFile_ << "{";
    detectionsOnMapFile_ << "\"schema_version\":2,";
    detectionsOnMapFile_ << "\"kind\":\"detections_on_map\",";
    detectionsOnMapFile_ << "\"session_id\":" << jsonText(sessionId_) << ",";
    detectionsOnMapFile_ << "\"event_type\":" << jsonText(eventType) << ",";
    detectionsOnMapFile_ << "\"timestamp_ms\":" << s.timestampMs << ",";
    detectionsOnMapFile_ << "\"timestamp_local\":" << jsonText(nowLocalString()) << ",";
    detectionsOnMapFile_ << "\"image_path\":" << jsonText(annotatedImagePathRel) << ",";
    detectionsOnMapFile_ << "\"annotated_image_path\":" << jsonText(annotatedImagePathRel) << ",";
    detectionsOnMapFile_ << "\"raw_image_path\":" << jsonText(rawImagePathRel) << ",";
    const V32DetectionCounts v32Counts = countV32Detections(s.detections);
    detectionsOnMapFile_ << "\"accepted_count\":" << acceptedCount << ",";
    detectionsOnMapFile_ << "\"weak_count\":" << weakCount << ",";
    detectionsOnMapFile_ << "\"rejected_count\":" << rejectedCount << ",";
    detectionsOnMapFile_ << "\"heuristic_count\":" << v32Counts.heuristic << ",";
    detectionsOnMapFile_ << "\"review_count\":" << v32Counts.review << ",";
    detectionsOnMapFile_ << "\"color_corrected_count\":" << v32Counts.colorCorrected << ",";
    detectionsOnMapFile_ << "\"noise_count\":" << v32Counts.noise << ",";
    detectionsOnMapFile_ << "\"map_pose\":";
    writeMapPoseObject(detectionsOnMapFile_, s, false, "");
    detectionsOnMapFile_ << ",";
    detectionsOnMapFile_ << "\"detections\":";
    writeMapDetectionProjectionArray(detectionsOnMapFile_, s);
    detectionsOnMapFile_ << "}\n";
    detectionsOnMapFile_.flush();
    ++detectionsOnMapEvents_;
}

void ToClientJsonLogger::writeMapDetectionProjectionArray(std::ostream& os,
                                                          const RobotState& s) const
{
    const double kPi = 3.14159265358979323846;
    const double kCameraHalfFovDeg = 31.0;
    const double kSingleTomatoDistanceM = 0.80;
    const double kBunchDistanceM = 1.20;

    const bool frameOk = s.detections.frame.width > 0 && s.detections.frame.height > 0;
    const bool mapPoseOk = s.ros2Map.loaded && s.ros2Map.valid && s.ros2Map.poseValid;

    double cameraPanLeftDeg = 0.0;
    if (s.cameraServo.valid || std::fabs(s.cameraServo.dirLeft) > 1e-6 || std::fabs(s.cameraServo.dirForward) > 1e-6) {
        cameraPanLeftDeg = std::atan2(s.cameraServo.dirLeft, s.cameraServo.dirForward) * 180.0 / kPi;
    }

    os << "[";
    bool first = true;
    for (const auto& d : s.detections.detections) {
        if (!d.valid || d.displaySuppressed || d.lostMaturityCompetition) {
            continue;
        }

        if (!first) {
            os << ",";
        }
        first = false;

        const double centerX = static_cast<double>(d.x) + 0.5 * static_cast<double>(d.w);
        const double centerY = static_cast<double>(d.y) + 0.5 * static_cast<double>(d.h);
        const double centerNormX = frameOk ? (centerX / static_cast<double>(s.detections.frame.width)) : 0.5;
        const double centerNormY = frameOk ? (centerY / static_cast<double>(s.detections.frame.height)) : 0.5;
        const double imageOffsetX = frameOk ? ((centerNormX - 0.5) * 2.0) : 0.0;

        // Display-map convention used by Ver30 GUI:
        // yaw=0 means up on screen, positive yaw is right/clockwise.
        // Camera dirLeft is positive to robot-left, so it subtracts from display yaw.
        const double bboxBearingDeg = imageOffsetX * kCameraHalfFovDeg;
        const double mapBearingDeg = s.ros2Map.robotYawDeg - cameraPanLeftDeg + bboxBearingDeg;
        const double mapBearingRad = mapBearingDeg * kPi / 180.0;

        const std::string labelLower = d.label;
        const bool likelyBunch =
            labelLower.find("bunch") != std::string::npos ||
            labelLower.find("cluster") != std::string::npos ||
            d.classId == 0 || d.classId == 3;
        const double projectionDistanceM = likelyBunch ? kBunchDistanceM : kSingleTomatoDistanceM;

        const double mapX = s.ros2Map.robotX + std::sin(mapBearingRad) * projectionDistanceM;
        const double mapY = s.ros2Map.robotY - std::cos(mapBearingRad) * projectionDistanceM;

        os << "{";
        os << "\"label\":" << jsonText(d.label) << ",";
        os << "\"class_id\":" << d.classId << ",";
        os << "\"confidence\":" << d.confidence << ",";
        os << "\"track_id\":" << d.trackId << ",";
        os << "\"weak\":" << boolText(d.weak) << ",";
        os << "\"source_type\":" << jsonText(d.sourceType) << ",";
        os << "\"policy_status\":" << jsonText(d.policyStatus) << ",";
        os << "\"heuristic\":" << boolText(d.heuristic) << ",";
        os << "\"review_candidate\":" << boolText(d.reviewCandidate) << ",";
        os << "\"color_corrected_by_policy\":" << boolText(d.colorCorrectedByPolicy) << ",";
        os << "\"cluster_promoted\":" << boolText(d.clusterPromoted) << ",";
        os << "\"bbox\":{";
        os << "\"x\":" << d.x << ",";
        os << "\"y\":" << d.y << ",";
        os << "\"w\":" << d.w << ",";
        os << "\"h\":" << d.h << "},";
        os << "\"image_center\":{";
        os << "\"x_norm\":" << centerNormX << ",";
        os << "\"y_norm\":" << centerNormY << "},";
        os << "\"map_projection\":{";
        os << "\"valid\":" << boolText(mapPoseOk) << ",";
        os << "\"method\":\"approx_robot_pose_camera_bearing_fixed_distance\",";
        os << "\"approximate\":true,";
        os << "\"projection_distance_m\":" << projectionDistanceM << ",";
        os << "\"camera_pan_left_deg\":" << cameraPanLeftDeg << ",";
        os << "\"bbox_bearing_deg\":" << bboxBearingDeg << ",";
        os << "\"map_bearing_deg\":" << mapBearingDeg << ",";
        if (mapPoseOk) {
            os << "\"x\":" << mapX << ",";
            os << "\"y\":" << mapY;
        } else {
            os << "\"x\":null,";
            os << "\"y\":null";
        }
        os << "}";
        os << "}";
    }
    os << "]";
}

void ToClientJsonLogger::writeRawCandidatesEvent(const std::string& eventType,
                                                   const std::string& rawImagePathRel,
                                                   const RobotState& s)
{
    if (!rawCandidatesFile_.is_open()) {
        return;
    }

    rawCandidatesFile_ << std::fixed << std::setprecision(6);
    rawCandidatesFile_ << "{";
    rawCandidatesFile_ << "\"schema_version\":4,";
    rawCandidatesFile_ << "\"session_id\":" << jsonText(sessionId_) << ",";
    rawCandidatesFile_ << "\"event_type\":" << jsonText(eventType) << ",";
    rawCandidatesFile_ << "\"timestamp_ms\":" << s.timestampMs << ",";
    rawCandidatesFile_ << "\"timestamp_local\":" << jsonText(nowLocalString()) << ",";
    rawCandidatesFile_ << "\"raw_image_path\":" << jsonText(rawImagePathRel) << ",";
    rawCandidatesFile_ << "\"raw_candidate_count\":" << s.detections.rawCandidates.size() << ",";
    rawCandidatesFile_ << "\"raw_candidates\":";
    writeDetectionVector(rawCandidatesFile_, s.detections.rawCandidates, false, "");
    rawCandidatesFile_ << "}\n";
    rawCandidatesFile_.flush();
    ++rawCandidateEvents_;
}

void ToClientJsonLogger::maybeWriteDetectionEvent(const RobotState& s)
{
    if (!eventsFile_.is_open() || !s.detections.valid || s.detections.frameBytes.empty()) {
        return;
    }

    int accepted = 0;
    int weak = 0;
    int rejected = 0;
    countDetections(s.detections, accepted, weak, rejected);

    const bool shouldSaveOk =
        accepted > 0 &&
        (lastOkImageMs_ == 0 || s.timestampMs < lastOkImageMs_ ||
         (s.timestampMs - lastOkImageMs_) >= cfg_.okImagePeriodMs);

    const bool shouldSaveWeak =
        accepted == 0 &&
        weak >= cfg_.weakImageMinCount &&
        (lastWeakImageMs_ == 0 || s.timestampMs < lastWeakImageMs_ ||
         (s.timestampMs - lastWeakImageMs_) >= cfg_.weakImagePeriodMs);

    if (!shouldSaveOk && !shouldSaveWeak) {
        return;
    }

    const std::string timePart = fileTimeString();
    std::ostringstream name;
    if (shouldSaveOk) {
        name << "ok_" << timePart << "_" << std::setw(6) << std::setfill('0') << (okImages_ + 1) << ".jpg";
        const std::string annotatedPath = (fs::path(okImagesDir_) / name.str()).string();
        const std::string rawPath = (fs::path(okRawImagesDir_) / name.str()).string();
        const bool wroteAnnotated = videoRecorder_.writeAnnotatedJpeg(annotatedPath, s.detections, cfg_.jpegQuality);
        const bool wroteRaw = videoRecorder_.writeRawJpeg(rawPath, s.detections, cfg_.jpegQuality);
        if (wroteAnnotated) {
            ++okImages_;
            ++detectionEvents_;
            lastOkImageMs_ = s.timestampMs;
            const std::string annotatedRel = makeRelativePath(sessionDir_, annotatedPath);
            const std::string rawRel = wroteRaw ? makeRelativePath(sessionDir_, rawPath) : "";
            writeDetectionEvent(eventsFile_, "accepted_detection_image",
                                annotatedRel, rawRel,
                                s, accepted, weak, rejected);
            writeDetectionsOnMapEvent("accepted_detection_image",
                                      annotatedRel, rawRel,
                                      s, accepted, weak, rejected);
            writeRawCandidatesEvent("accepted_detection_image", rawRel, s);
        }
        return;
    }

    name << "weak_" << timePart << "_" << std::setw(6) << std::setfill('0') << (weakImages_ + 1) << ".jpg";
    const std::string annotatedPath = (fs::path(weakImagesDir_) / name.str()).string();
    const std::string rawPath = (fs::path(weakRawImagesDir_) / name.str()).string();
    const bool wroteAnnotated = videoRecorder_.writeAnnotatedJpeg(annotatedPath, s.detections, cfg_.jpegQuality);
    const bool wroteRaw = videoRecorder_.writeRawJpeg(rawPath, s.detections, cfg_.jpegQuality);
    if (wroteAnnotated) {
        ++weakImages_;
        ++detectionEvents_;
        lastWeakImageMs_ = s.timestampMs;
        const std::string annotatedRel = makeRelativePath(sessionDir_, annotatedPath);
        const std::string rawRel = wroteRaw ? makeRelativePath(sessionDir_, rawPath) : "";
        writeDetectionEvent(eventsFile_, "weak_noise_image",
                            annotatedRel, rawRel,
                            s, accepted, weak, rejected);
        writeDetectionsOnMapEvent("weak_noise_image",
                                  annotatedRel, rawRel,
                                  s, accepted, weak, rejected);
        writeRawCandidatesEvent("weak_noise_image", rawRel, s);
    }
}

void ToClientJsonLogger::writeManifest(const std::string& stopReason,
                                       std::uint64_t stoppedAtMs)
{
    if (manifestPath_.empty()) {
        return;
    }

    std::ofstream out(manifestPath_, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    const auto videoStatus = videoRecorder_.getStatus();

    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"schema_version\": 3,\n";
    out << "  \"kind\": \"to_client_debug_session\",\n";
    out << "  \"session_id\": " << jsonText(sessionId_) << ",\n";
    out << "  \"session_dir\": " << jsonText(sessionDir_) << ",\n";
    out << "  \"started_at_ms\": " << startedAtMs_ << ",\n";
    out << "  \"started_at_local\": " << jsonText(startedAtLocal_) << ",\n";
    out << "  \"stopped_at_ms\": " << stoppedAtMs << ",\n";
    out << "  \"stopped_at_local\": " << jsonText(stoppedAtMs > 0 ? nowLocalString() : "") << ",\n";
    out << "  \"stop_reason\": " << jsonText(stopReason) << ",\n";
    out << "  \"paths\": {\n";
    out << "    \"latest\": " << jsonText(makeRelativePath(sessionDir_, latestPath_)) << ",\n";
    out << "    \"timeline\": " << jsonText(makeRelativePath(sessionDir_, timelinePath_)) << ",\n";
    out << "    \"detection_events\": " << jsonText(makeRelativePath(sessionDir_, eventsPath_)) << ",\n";
    out << "    \"raw_candidates\": " << jsonText(makeRelativePath(sessionDir_, rawCandidatesPath_)) << ",\n";
    out << "    \"map_pose_timeline\": " << jsonText(makeRelativePath(sessionDir_, mapPoseTimelinePath_)) << ",\n";
    out << "    \"map_overlay_summary\": " << jsonText(makeRelativePath(sessionDir_, mapOverlaySummaryPath_)) << ",\n";
    out << "    \"detections_on_map\": " << jsonText(makeRelativePath(sessionDir_, detectionsOnMapPath_)) << ",\n";
    out << "    \"ros2_map_assets_dir\": " << jsonText(makeRelativePath(sessionDir_, ros2MapAssetsDir_)) << ",\n";
    out << "    \"images_ok_dir\": " << jsonText(makeRelativePath(sessionDir_, okImagesDir_)) << ",\n";
    out << "    \"images_ok_raw_dir\": " << jsonText(makeRelativePath(sessionDir_, okRawImagesDir_)) << ",\n";
    out << "    \"images_weak_noise_dir\": " << jsonText(makeRelativePath(sessionDir_, weakImagesDir_)) << ",\n";
    out << "    \"images_weak_noise_raw_dir\": " << jsonText(makeRelativePath(sessionDir_, weakRawImagesDir_)) << ",\n";
    out << "    \"video\": " << jsonText(makeRelativePath(sessionDir_, videoPath_)) << ",\n";
    out << "    \"lidar_summary\": " << jsonText(makeRelativePath(sessionDir_, lidarPreview_.summaryPath())) << ",\n";
    out << "    \"lidar_preview\": " << jsonText(makeRelativePath(sessionDir_, lidarPreview_.previewPath())) << "\n";
    out << "  },\n";
    out << "  \"video\": {\n";
    out << "    \"active\": " << boolText(videoStatus.active) << ",\n";
    out << "    \"writer_open\": " << boolText(videoStatus.writerOpen) << ",\n";
    out << "    \"backend\": " << jsonText(videoStatus.backend) << ",\n";
    out << "    \"target_fps\": " << cfg_.videoFps << ",\n";
    out << "    \"max_width\": " << cfg_.videoMaxWidth << ",\n";
    out << "    \"max_height\": " << cfg_.videoMaxHeight << ",\n";
    out << "    \"bitrate_kbps\": " << cfg_.videoBitrateKbps << ",\n";
    out << "    \"max_bytes\": " << cfg_.videoMaxBytes << ",\n";
    out << "    \"frames_written\": " << videoStatus.framesWritten << ",\n";
    out << "    \"bytes_written\": " << videoStatus.bytesWritten << ",\n";
    out << "    \"max_size_reached\": " << boolText(videoStatus.maxSizeReached) << "\n";
    out << "  },\n";
    out << "  \"counts\": {\n";
    out << "    \"timeline_rows\": " << timelineRows_ << ",\n";
    out << "    \"detection_events\": " << detectionEvents_ << ",\n";
    out << "    \"raw_candidate_events\": " << rawCandidateEvents_ << ",\n";
    out << "    \"map_pose_rows\": " << mapPoseRows_ << ",\n";
    out << "    \"detections_on_map_events\": " << detectionsOnMapEvents_ << ",\n";
    out << "    \"ok_images\": " << okImages_ << ",\n";
    out << "    \"weak_noise_images\": " << weakImages_ << ",\n";
    out << "    \"lidar_summaries\": " << lidarPreview_.summariesWritten() << ",\n";
    out << "    \"lidar_preview_points\": " << lidarPreview_.previewPointCount() << "\n";
    out << "  }\n";
    out << "}\n";
}

void ToClientJsonLogger::log(const RobotState& s)
{
    if (!isEnabled()) {
        return;
    }

    const std::uint64_t t = s.timestampMs;

    videoRecorder_.updateFromSnapshot(s.detections, t);
    lidarPreview_.update(s, t);
    maybeWriteDetectionEvent(s);

    if (lastTimelineMs_ == 0 || t < lastTimelineMs_ || (t - lastTimelineMs_) >= cfg_.timelinePeriodMs) {
        lastTimelineMs_ = t;
        writeTimeline(s);
    }

    if (lastLatestMs_ == 0 || t < lastLatestMs_ || (t - lastLatestMs_) >= cfg_.latestPeriodMs) {
        lastLatestMs_ = t;
        writeLatest(s);
        writeManifest("running", 0);
    }
}

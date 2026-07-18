#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "core/SystemState.h"
#include "Debugging/toClient/ToClientLidarPreviewWriter.h"
#include "Debugging/toClient/ToClientVideoRecorder.h"

/*
 * RBV2_NJOrin_ver28 TO_CLIENT_JSON session logger.
 *
 * This replaces the old single huge JSONL stream with a compact session folder:
 *   session_manifest.json
 *   latest.json
 *   robot_timeline.jsonl
 *   detection_events.jsonl
 *   images_ok/*.jpg
 *   images_ok_raw/*.jpg
 *   images_weak_noise/*.jpg
 *   images_weak_noise_raw/*.jpg
 *   raw_candidates.jsonl
 *   videos/*.mp4
 *   lidar/lidar_summary.jsonl
 *   lidar/lidar_map_preview.json
 *
 * Images/video are saved as files; JSON stores only relative paths and metadata.
 */
class ToClientJsonLogger
{
public:
    struct Config
    {
        std::string baseDir = "Debugging/toClient";
        std::uint64_t timelinePeriodMs = 250;
        std::uint64_t latestPeriodMs = 250;
        std::uint64_t okImagePeriodMs = 1000;
        std::uint64_t weakImagePeriodMs = 1500;
        int weakImageMinCount = 3;
        int jpegQuality = 82;

        double videoFps = 10.0;
        int videoMaxWidth = 1280;
        int videoMaxHeight = 720;
        int videoBitrateKbps = 2500;
        std::uint64_t videoMaxBytes = 1024ULL * 1024ULL * 1024ULL;
    };

    ToClientJsonLogger() = default;
    ~ToClientJsonLogger();

    // Legacy API kept so older code still compiles. The jsonlPath argument is
    // converted to its parent folder and a session is opened there.
    bool start(const std::string& jsonlPath,
               const std::string& latestJsonPath = "");

    bool startSession(const std::string& baseDir,
                      std::uint64_t startedAtMs,
                      const Config& cfg);
    void stop(const std::string& reason = "manual_stop",
              std::uint64_t stoppedAtMs = 0);

    bool isEnabled() const;

    void log(const RobotState& state);

    const std::string& sessionDir() const;
    const std::string& sessionId() const;

private:
    static const char* boolText(bool v);
    static std::string jsonText(const std::string& value);
    static std::string nowLocalString();
    static std::string fileTimeString();
    static std::string basename(const std::string& path);
    static std::string parentPathOrBase(const std::string& path, const std::string& fallback);
    static std::string makeRelativePath(const std::string& base, const std::string& full);

    static const Detection* findBestDetection(const DetectionSnapshot& snapshot);
    static void countDetections(const DetectionSnapshot& snapshot,
                                int& acceptedCount,
                                int& weakCount,
                                int& rejectedCount);

    void writeTimeline(const RobotState& state);
    void writeMapPoseTimeline(const RobotState& state);
    void writeLatest(const RobotState& state);
    void writeMapPoseObject(std::ostream& os,
                            const RobotState& state,
                            bool pretty,
                            const char* indent) const;
    void writeMapOverlaySummary(const std::string& stopReason,
                                std::uint64_t stoppedAtMs);
    void copyRos2MapAssetsToSession();
    void maybeWriteDetectionEvent(const RobotState& state);
    void writeDetectionEvent(std::ostream& os,
                             const std::string& eventType,
                             const std::string& annotatedImagePathRel,
                             const std::string& rawImagePathRel,
                             const RobotState& state,
                             int acceptedCount,
                             int weakCount,
                             int rejectedCount);
    void writeDetectionsOnMapEvent(const std::string& eventType,
                                   const std::string& annotatedImagePathRel,
                                   const std::string& rawImagePathRel,
                                   const RobotState& state,
                                   int acceptedCount,
                                   int weakCount,
                                   int rejectedCount);
    void writeMapDetectionProjectionArray(std::ostream& os,
                                          const RobotState& state) const;
    void writeRawCandidatesEvent(const std::string& eventType,
                                 const std::string& rawImagePathRel,
                                 const RobotState& state);
    void writeDetectionArray(std::ostream& os,
                             const DetectionSnapshot& snapshot,
                             bool pretty,
                             const char* indent) const;
    void writeDetectionVector(std::ostream& os,
                              const std::vector<Detection>& detections,
                              bool pretty,
                              const char* indent) const;
    void writeCameraServo(std::ostream& os,
                          const CameraServoState& servo,
                          bool pretty,
                          const char* indent) const;
    void writeClientRobotSummary(std::ostream& os,
                                 const RobotState& state,
                                 bool pretty) const;
    void writeManifest(const std::string& stopReason = "running",
                       std::uint64_t stoppedAtMs = 0);

private:
    Config cfg_{};
    bool enabled_ = false;

    std::string sessionId_{};
    std::string sessionDir_{};
    std::string manifestPath_{};
    std::string latestPath_{};
    std::string timelinePath_{};
    std::string eventsPath_{};
    std::string rawCandidatesPath_{};
    std::string mapPoseTimelinePath_{};
    std::string mapOverlaySummaryPath_{};
    std::string detectionsOnMapPath_{};
    std::string ros2MapAssetsDir_{};
    std::string copiedMapSessionDir_{};
    std::string copiedMapYamlRel_{};
    std::string copiedMapPgmRel_{};
    std::string copiedLatestMapRel_{};
    std::string okImagesDir_{};
    std::string okRawImagesDir_{};
    std::string weakImagesDir_{};
    std::string weakRawImagesDir_{};
    std::string videosDir_{};
    std::string lidarDir_{};
    std::string videoPath_{};

    std::ofstream timelineFile_{};
    std::ofstream eventsFile_{};
    std::ofstream rawCandidatesFile_{};
    std::ofstream mapPoseTimelineFile_{};
    std::ofstream detectionsOnMapFile_{};

    ToClientVideoRecorder videoRecorder_{};
    ToClientLidarPreviewWriter lidarPreview_{};

    std::uint64_t startedAtMs_ = 0;
    std::string startedAtLocal_{};

    std::uint64_t lastTimelineMs_ = 0;
    std::uint64_t lastLatestMs_ = 0;
    std::uint64_t lastOkImageMs_ = 0;
    std::uint64_t lastWeakImageMs_ = 0;
    std::uint64_t timelineRows_ = 0;
    std::uint64_t detectionEvents_ = 0;
    std::uint64_t rawCandidateEvents_ = 0;
    std::uint64_t mapPoseRows_ = 0;
    std::uint64_t detectionsOnMapEvents_ = 0;
    std::uint64_t okImages_ = 0;
    std::uint64_t weakImages_ = 0;

    bool mapSummaryHaveFirst_ = false;
    bool mapSummaryHaveLast_ = false;
    bool mapAssetsCopied_ = false;
    std::string mapAssetsCopyError_{};

    std::uint64_t firstMapTimestampMs_ = 0;
    double firstMapRobotX_ = 0.0;
    double firstMapRobotY_ = 0.0;
    double firstMapRobotYawDeg_ = 0.0;
    double firstMapRobotDistanceM_ = 0.0;

    std::uint64_t lastMapTimestampMs_ = 0;
    double lastMapRobotX_ = 0.0;
    double lastMapRobotY_ = 0.0;
    double lastMapRobotYawDeg_ = 0.0;
    double lastMapRobotDistanceM_ = 0.0;

    bool lastMapLoaded_ = false;
    bool lastMapValid_ = false;
    bool lastMapManualStartSet_ = false;
    bool lastMapPoseValid_ = false;
    std::string lastMapSessionDir_{};
    std::string lastMapYaml_{};
    std::string lastMapPgm_{};
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
    double lastMapResolutionM_ = 0.05;
    double lastMapOriginX_ = 0.0;
    double lastMapOriginY_ = 0.0;
    double lastMapOriginYawRad_ = 0.0;
    double lastManualStartX_ = 0.0;
    double lastManualStartY_ = 0.0;
    double lastManualStartYawDeg_ = 0.0;
    std::size_t lastTrailCount_ = 0;
};

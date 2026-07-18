#pragma once

#include "core/PerceptionTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cv { class Mat; }

class ObjectDetector {
public:
    enum class Backend {
        TensorRTJetson
    };

    using Detection = ::Detection;
    using Frame = ::Frame;
    using Snapshot = ::DetectionSnapshot;

    struct Config {
        Backend backend = Backend::TensorRTJetson;

        std::string modelName = "best8s_seg_v43";
        std::string modelDir = "models";
        std::string onnxPath = "models/best8s_seg_v43.onnx";
        std::string enginePath = "models/best8s_seg_v43_fp16.engine";

        // Low raw threshold keeps weak/noise boxes for debugging.
        // Accepted detections are then filtered by classThresholds and stabilized by frame tracking.
        float rawConfThreshold = 0.15f;
        float confThreshold = 0.70f;
        float nmsThreshold = 0.45f;
        bool useLetterboxPreprocess = true;

        bool useFrameTracking = true;
        float trackIouThreshold = 0.35f;
        int trackMaxAge = 8;
        int trackMinHitsToShow = 3;
        bool trackClassAware = true;
        bool trackShowBestWhileVisible = true;
        int trackCameraMotionCooldownFrames = 8;

        bool drawWeakRejected = true;
        int maxWeakToKeep = 25;

        bool useRoiFilter = true;
        float roiXMin = 0.03f;
        float roiXMax = 0.97f;
        float roiYMin = 0.05f;
        float roiYMax = 0.95f;

        float minBoxAreaSingle = 250.0f;
        float minBoxAreaBunch = 2500.0f;

        // Segmentation mask and color filtering, adapted from the video notebook.
        bool useSegmentationMasks = true;
        float maskThreshold = 0.50f;
        float minMaskAreaSingle = 250.0f;
        float minMaskAreaBunch = 1800.0f;
        float minMaskDensitySingle = 0.12f;
        float minMaskDensityBunch = 0.06f;

        bool useColorFilter = true;
        float ripeMinRedRatio = 0.08f;
        float unripeMinGreenYellowRatio = 0.08f;

        // Ver28: sanity gate against high-confidence false positives that are
        // not tomato-colored at all. The detector still writes these candidates
        // to JSON as weak/noise, but they are not shown as strong ripe/unripe.
        bool useTomatoSanityGate = true;
        float ripeMinWarmRatio = 0.12f;
        float singleMinAnyTomatoColorRatio = 0.10f;
        float highConfidenceSanityThreshold = 0.88f;

        // Ver28: per-object maturity competition. It resolves ripe vs unripe
        // and eripe bunch vs unripe bunch without letting singles suppress bunches.
        bool useMaturityCompetition = true;
        float maturityCompetitionIouSingles = 0.72f;
        float maturityCompetitionIouBunches = 0.18f;
        bool hideMaturityCompetitionLosers = true;
        float maturityRipeWarmWeight = 0.22f;
        float maturityUnripeGreenWeight = 0.22f;
        float maturityBunchSupportWeight = 0.055f;
        float maturityScoreMargin = 0.05f;
        float classSwitchMargin = 0.12f;
        int classSwitchRequiredFrames = 4;

        bool useBunchConsistencyRules = true;
        float bunchMinRelativeToMaxSingleBox = 1.35f;
        int bunchMinTomatoesInside = 2;

        // Ver27: bunch support / cluster promotion.
        // Bunch detections often have low mask density on green greenhouse background,
        // but are still reliable when several strong single tomatoes are inside them.
        bool useBunchSupportPromotion = true;
        float bunchPromotionMinConfidence = 0.75f;
        float bunchPromotionMemberMinConfidence = 0.70f;
        int eripeBunchPromotionMinRipeSingles = 3;
        int unripeBunchPromotionMinUnripeSingles = 2;
        float bunchPromotionMinConfSum = 2.10f;
        bool correctUnripeBunchToRipeBunch = true;
        int unripeToRipeCorrectionMinRipeSingles = 3;
        float unripeToRipeCorrectionMinRipeConfSum = 2.30f;
        float unripeToRipeCorrectionMargin = 0.60f;
        bool refineBunchBboxFromMembers = true;
        float refinedBunchPaddingRatio = 0.08f;

        int maxDetectionsPerFrame = 18;

        // Part C: second pass on ROI crops.
        // If a group of single tomatoes is detected but a bunch is weak/missing,
        // crop that region, resize it to 640x640, and run the same engine again.
        bool useRoiSecondPass = true;
        int roiSecondPassEveryNFrames = 3;
        bool roiSecondPassOnlyIfNoAcceptedBunch = true;
        int roiSecondPassMinSingles = 2;
        int roiSecondPassMaxCrops = 2;
        float roiSecondPassPadding = 0.35f;
        float roiSecondPassMaxImageFraction = 0.70f;
        float roiGroupMaxCenterDistanceFactor = 4.0f;
        float roiSecondPassMinBunchConfidence = 0.45f;
        bool roiSecondPassUseWeakOverlap = true;
        int roiSecondPassMinWeakGroup = 3;
        float roiSecondPassMinWeakConfidence = 0.20f;
        float roiSecondPassWeakOverlapIou = 0.03f;

        bool drawMasksForSingles = true;
        bool drawMasksForBunches = false;
        bool displayPreferBunchOverSingles = true;
        float displayBunchSuppressIou = 0.10f;

        // Ver32 experimental runtime policy.
        // Default is OFF so policy_replay_lab can keep replaying the original robot policy.
        // app/amain.cpp enables it for the live robot camera.
        bool useV32ExperimentalRuntimePolicy = false;

        // Near-threshold review accepted as a visible model detection. It remains tagged as
        // policy_status=review in JSON so the client can separate it from original strong detections.
        bool v32PublishReviewAsAccepted = true;
        float v32ReviewUnripeMinConf = 0.80f;
        float v32ReviewRipeMinConf = 0.75f;
        float v32ReviewMinMaskDensity = 0.35f;
        float v32ReviewMinMaskArea = 1000.0f;
        float v32ReviewUnripeMinGreenYellow = 0.35f;
        float v32ReviewRipeMinWarm = 0.12f;
        float v32ReviewRipeMinRed = 0.08f;

        // Color-based class correction for single tomatoes.
        bool v32UseColorClassCorrection = true;
        float v32ColorCorrectionMinConf = 0.50f;
        float v32ColorCorrectionMinMaskDensity = 0.35f;
        float v32RipeToUnripeMinGreenYellow = 0.45f;
        float v32RipeToUnripeMaxRed = 0.06f;
        float v32RipeToUnripeMaxWarm = 0.12f;
        float v32UnripeToRipeMinWarm = 0.66f;
        float v32UnripeToRipeMaxGreenYellow = 0.35f;

        // Weak bunch anchor heuristic. Adds yellow policy-created bunch boxes.
        bool v32UseWeakBunchAnchorHeuristic = true;
        bool v32HeuristicsBypassFrameTracking = true;
        float v32HeuristicAnchorMinConf = 0.40f;
        float v32HeuristicAnchorStrongConf = 0.50f;
        float v32HeuristicChildMinConf = 0.50f;
        float v32HeuristicChildMinMaskDensity = 0.35f;
        int v32HeuristicMinChildrenStrongAnchor = 3;
        int v32HeuristicMinChildrenWeakAnchor = 4;
        float v32HeuristicMinWeightedStrongAnchor = 1.50f;
        float v32HeuristicMinWeightedWeakAnchor = 2.50f;
        float v32HeuristicAnchorPaddingRatio = 0.18f;
        float v32HeuristicUnionPaddingRatio = 0.08f;
        float v32HeuristicMaturityMargin = 0.60f;
        float v32HeuristicDuplicateIou = 0.45f;
        int v32HeuristicMaxPerFrame = 2;
        float v32HeuristicMaxBoxAreaFraction = 0.18f;
        float v32HeuristicMinAnchorMaskDensity = 0.02f;
        float v32HeuristicMinAnchorMaskArea = 300.0f;

        // Weak/noise split for display only. Old weak=true stays unchanged for compatibility.
        float v32NoiseMaxConfidence = 0.30f;

        std::unordered_map<int, float> classThresholds = {
            {0, 0.65f},  // eripe bunch
            {1, 0.80f},  // ripe
            {2, 0.85f},  // unripe
            {3, 0.70f}   // unripe bunch
        };

        // Number of real YOLO model classes in output0.
        // Do NOT include heuristic-only labels here.
        // The v43 TensorRT engine output is 4 box attrs + 4 class scores + 32 mask coeffs = 40 attrs.
        int modelClassCount = 4;

        int inputWidth = 640;
        int inputHeight = 640;

        bool useGpu = true;
        bool useFP16 = true;
        bool personOnly = false;

        int sensorId = 0;
        int cameraWidth = 1280;
        int cameraHeight = 720;
        int cameraFps = 30;

        // Digital zoom for live camera debugging.
        // 1.0 means no zoom. Larger values crop the center of the camera frame
        // and resize it back to the normal frame size before inference/display.
        float digitalZoom = 1.0f;
        float digitalZoomMin = 1.0f;
        float digitalZoomMax = 10.00f;
        float digitalZoomStep = 1.0f;
        std::vector<float> digitalZoomLevels = {
            1.00f, 1.15f, 1.30f, 1.50f, 1.75f, 2.00f,
            2.40f, 2.80f, 3.30f, 4.00f, 4.80f, 5.50f,
            6.50f, 8.00f, 10.00f
        };

        // Low-resolution source-space mask stored in Detection::mask.
        // 320x180 matches the 16:9 CSI frame and avoids drawing letterbox-space masks.
        int displayMaskWidth = 320;
        int displayMaskHeight = 180;

        bool showGui = false;
        std::string windowName = "Jetson YOLOv8 TensorRT";

        int cameraOpenSettleDelayMs = 250;
        int loopSleepMs = 5;

        // Dynamic class mapping for GUI / downstream logic
        std::unordered_map<int, std::string> classNames = {
            {0, "eripe bunch"},
            {1, "ripe"},
            {2, "unripe"},
            {3, "unripe bunch"}
        };
    };

public:
    ObjectDetector();
    explicit ObjectDetector(const Config& cfg);
    ~ObjectDetector();

    bool init();
    bool isReady() const;

    void start();
    void stop();
    void toggle();
    bool isRunning() const;

    const Config& config() const;
    Config& config();

    void setDigitalZoom(float zoom);
    void adjustDigitalZoom(float delta);
    float getDigitalZoom() const;
    void resetFrameTracking();
    void notifyCameraMotion();

    bool processFrame(const Frame& frame);
    bool captureAndProcessNextFrame();

    // Ver32 policy replay lab:
    // Run the exact TensorRT + postprocess + policy pipeline on an already-loaded BGR image.
    // This is used by policy_replay_lab and does not open the CSI camera or require start().
    bool processBgrForReplay(const cv::Mat& bgr, Snapshot& out);

    bool getLatestSnapshot(Snapshot& out) const;
    void clearLatestSnapshot();

    std::string getClassDisplayName(int classId, const std::string& fallbackLabel = "") const;
    const std::unordered_map<int, std::string>& getClassNameMap() const;

private:
    void workerLoop();
    void releaseCamera();
    bool openCameraOnce();

    bool inferDetectionsOnBgrInternal(const cv::Mat& bgr,
                                      std::vector<Detection>& rawDetections,
                                      std::string& err);
    std::vector<Detection> runRoiSecondPassInternal(const cv::Mat& bgr,
                                                    const std::vector<Detection>& acceptedFull,
                                                    const std::vector<Detection>& rejectedFull,
                                                    int frameCounter);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    Config cfg_;
    std::atomic_bool ready_{false};
    std::atomic_bool running_{false};

    mutable std::mutex mtx_;
    Snapshot latest_{};

    mutable std::mutex stateMutex_;
    std::thread worker_;
};


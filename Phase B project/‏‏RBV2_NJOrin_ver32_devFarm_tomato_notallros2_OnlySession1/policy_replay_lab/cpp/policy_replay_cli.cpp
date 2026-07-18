#include "perception/ObjectDetector.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Args {
    fs::path projectRoot = ".";
    fs::path sessionPath;
    fs::path outputDir;
    fs::path enginePath = "models/best8s_seg_v43_fp16.engine";
    std::string mode = "policy_only";
    std::vector<std::string> groups = {"images_ok_raw", "images_weak_noise_raw"};
    int maxImagesPerGroup = 0;
};

static std::string jsonEscape(const std::string& value) {
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

static std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool isImageFile(const fs::path& p) {
    const std::string e = lowerCopy(p.extension().string());
    return e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".bmp" || e == ".webp";
}

static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char c){ return !std::isspace(c); }));
        item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), item.end());
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static void printHelp() {
    std::cout <<
        "policy_replay_cli - replay RBV2 TensorRT detections on saved session images\n\n"
        "Usage:\n"
        "  ./policy_replay_lab/bin/policy_replay_cli \\\n"
        "    --project-root . \\\n"
        "    --session Debugging/toClient/sessions/session_20260630_103628 \\\n"
        "    --mode policy_only\n\n"
        "Options:\n"
        "  --project-root PATH     Project root. Default: .\n"
        "  --session PATH          Session folder. Required.\n"
        "  --output PATH           Output folder. Default: policy_replay_lab/outputs/<session_id>/cpp_replay\n"
        "  --engine PATH           TensorRT engine path. Default: models/best8s_seg_v43_fp16.engine\n"
        "  --mode NAME             policy_only or robot_like. Default: policy_only\n"
        "  --groups A,B            Image groups. Default: images_ok_raw,images_weak_noise_raw\n"
        "  --max-images N          Limit images per group. 0 means all.\n"
        "  --help                  Show this help.\n";
}

static bool parseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto needValue = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value after " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "--help" || a == "-h") {
            printHelp();
            return false;
        } else if (a == "--project-root") {
            const char* v = needValue(a); if (!v) return false; args.projectRoot = v;
        } else if (a == "--session") {
            const char* v = needValue(a); if (!v) return false; args.sessionPath = v;
        } else if (a == "--output") {
            const char* v = needValue(a); if (!v) return false; args.outputDir = v;
        } else if (a == "--engine") {
            const char* v = needValue(a); if (!v) return false; args.enginePath = v;
        } else if (a == "--mode") {
            const char* v = needValue(a); if (!v) return false; args.mode = v;
        } else if (a == "--groups") {
            const char* v = needValue(a); if (!v) return false; args.groups = splitComma(v);
        } else if (a == "--max-images") {
            const char* v = needValue(a); if (!v) return false; args.maxImagesPerGroup = std::max(0, std::atoi(v));
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return false;
        }
    }

    if (args.sessionPath.empty()) {
        std::cerr << "--session is required\n";
        return false;
    }
    if (args.mode != "policy_only" && args.mode != "robot_like") {
        std::cerr << "--mode must be policy_only or robot_like\n";
        return false;
    }
    if (args.groups.empty()) {
        args.groups = {"images_ok_raw", "images_weak_noise_raw"};
    }
    return true;
}

static fs::path absUnderProject(const fs::path& projectRoot, const fs::path& p) {
    if (p.is_absolute()) return fs::absolute(p).lexically_normal();
    return fs::absolute(projectRoot / p).lexically_normal();
}

static std::string safeRelative(const fs::path& base, const fs::path& p) {
    std::error_code ec;
    auto rel = fs::relative(p, base, ec);
    if (!ec) return rel.generic_string();
    return p.generic_string();
}

static std::vector<fs::path> listImagesSorted(const fs::path& dir) {
    std::vector<fs::path> out;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (ent.is_regular_file() && isImageFile(ent.path())) {
            out.push_back(ent.path());
        }
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().string() < b.filename().string();
    });
    return out;
}

static fs::path findGroupDir(const fs::path& sessionPath, const std::string& wanted) {
    fs::path exact = sessionPath / wanted;
    if (fs::exists(exact) && fs::is_directory(exact)) return exact;

    const std::string w = lowerCopy(wanted);
    for (const auto& ent : fs::recursive_directory_iterator(sessionPath)) {
        if (!ent.is_directory()) continue;
        const std::string name = lowerCopy(ent.path().filename().string());
        if (name == w) return ent.path();
    }

    // Fallback for slightly different names.
    const bool wantOk = w.find("ok") != std::string::npos;
    const bool wantWeak = w.find("weak") != std::string::npos || w.find("noise") != std::string::npos;
    const bool wantRaw = w.find("raw") != std::string::npos;
    for (const auto& ent : fs::recursive_directory_iterator(sessionPath)) {
        if (!ent.is_directory()) continue;
        const std::string name = lowerCopy(ent.path().filename().string());
        const bool hasOk = name.find("ok") != std::string::npos;
        const bool hasWeak = name.find("weak") != std::string::npos || name.find("noise") != std::string::npos;
        const bool hasRaw = name.find("raw") != std::string::npos;
        if ((wantOk == hasOk || !wantOk) && (wantWeak == hasWeak || !wantWeak) && (wantRaw == hasRaw || !wantRaw)) {
            if (!listImagesSorted(ent.path()).empty()) return ent.path();
        }
    }

    return fs::path();
}

static ObjectDetector::Config makeReplayConfig(const Args& args) {
    ObjectDetector::Config cfg;
    cfg.backend = ObjectDetector::Backend::TensorRTJetson;
    cfg.modelName = "best8s_seg_v43";
    cfg.modelDir = "models";
    cfg.onnxPath = "models/best8s_seg_v43.onnx";
    cfg.enginePath = args.enginePath.generic_string();
    cfg.useGpu = true;
    cfg.useFP16 = true;
    cfg.inputWidth = 640;
    cfg.inputHeight = 640;
    cfg.rawConfThreshold = 0.15f;
    cfg.confThreshold = 0.70f;
    cfg.nmsThreshold = 0.45f;
    cfg.personOnly = false;
    cfg.useRoiFilter = true;
    cfg.roiXMin = 0.03f;
    cfg.roiXMax = 0.97f;
    cfg.roiYMin = 0.05f;
    cfg.roiYMax = 0.95f;
    cfg.minBoxAreaSingle = 250.0f;
    cfg.minBoxAreaBunch = 2500.0f;
    cfg.classThresholds = {
        {0, 0.65f},
        {1, 0.80f},
        {2, 0.85f},
        {3, 0.70f}
    };
    cfg.modelClassCount = 4;
    cfg.classNames = {
        {0, "eripe bunch"},
        {1, "ripe"},
        {2, "unripe"},
        {3, "unripe bunch"}
    };
    cfg.drawWeakRejected = true;
    cfg.showGui = false;

    // Replay lab keeps the C++ detector on the original model policy.
    // The Python analysis layer applies experimental policies for offline comparison.
    cfg.useV32ExperimentalRuntimePolicy = false;

    if (args.mode == "robot_like") {
        cfg.useFrameTracking = true;
        cfg.trackIouThreshold = 0.35f;
        cfg.trackMaxAge = 8;
        cfg.trackMinHitsToShow = 3;
        cfg.trackClassAware = true;
        cfg.maxWeakToKeep = 25;
        cfg.displayPreferBunchOverSingles = true;
        cfg.roiSecondPassEveryNFrames = 3;
    } else {
        // Main analysis mode: see all policy results without display/tracking truncation.
        cfg.useFrameTracking = false;
        cfg.maxWeakToKeep = -1;
        cfg.maxDetectionsPerFrame = -1;
        cfg.displayPreferBunchOverSingles = false;
        cfg.roiSecondPassEveryNFrames = 1;
    }

    return cfg;
}

static void writeDetectionJson(std::ostream& os,
                               const std::string& sessionId,
                               const std::string& imageGroup,
                               const std::string& imagePathRelSession,
                               const std::string& imagePathAbs,
                               int frameId,
                               int detectionId,
                               const Detection& d,
                               const std::string& sourceKind) {
    const std::string status = (sourceKind == "raw_candidate") ? "raw_candidate" : (d.weak ? "weak" : "strong");
    os << std::fixed << std::setprecision(6);
    os << "{";
    os << "\"schema\":\"rbv2_policy_replay_cpp_v1\",";
    os << "\"session_id\":" << jsonEscape(sessionId) << ",";
    os << "\"image_group\":" << jsonEscape(imageGroup) << ",";
    os << "\"image_path\":" << jsonEscape(imagePathRelSession) << ",";
    os << "\"image_abs_path\":" << jsonEscape(imagePathAbs) << ",";
    os << "\"frame_id\":" << frameId << ",";
    os << "\"detection_id\":" << detectionId << ",";
    os << "\"source_type\":\"model\",";
    os << "\"source_kind\":" << jsonEscape(sourceKind) << ",";
    os << "\"class_id\":" << d.classId << ",";
    os << "\"class_name\":" << jsonEscape(d.label) << ",";
    os << "\"confidence\":" << d.confidence << ",";
    os << "\"old_status\":" << jsonEscape(status) << ",";
    os << "\"weak\":" << (d.weak ? "true" : "false") << ",";
    os << "\"old_reject_reason\":" << jsonEscape(d.rejectReason) << ",";
    os << "\"valid\":" << (d.valid ? "true" : "false") << ",";
    os << "\"roi_pass\":" << (d.roiPass ? "true" : "false") << ",";
    os << "\"roi_group_size\":" << d.roiGroupSize << ",";
    os << "\"roi_reason\":" << jsonEscape(d.roiReason) << ",";
    os << "\"roi_source_accepted_count\":" << d.roiSourceAcceptedCount << ",";
    os << "\"roi_source_weak_count\":" << d.roiSourceWeakCount << ",";
    os << "\"display_source\":" << jsonEscape(d.displaySource) << ",";
    os << "\"promotion_reason\":" << jsonEscape(d.promotionReason) << ",";
    os << "\"cluster_promoted\":" << (d.clusterPromoted ? "true" : "false") << ",";
    os << "\"class_corrected\":" << (d.classCorrected ? "true" : "false") << ",";
    os << "\"original_class_id\":" << d.originalClassId << ",";
    os << "\"original_label\":" << jsonEscape(d.originalLabel) << ",";
    os << "\"bbox_x\":" << d.x << ",";
    os << "\"bbox_y\":" << d.y << ",";
    os << "\"bbox_w\":" << d.w << ",";
    os << "\"bbox_h\":" << d.h << ",";
    os << "\"bbox\":{";
    os << "\"x\":" << d.x << ",\"y\":" << d.y << ",\"w\":" << d.w << ",\"h\":" << d.h << "},";
    os << "\"metrics\":{";
    os << "\"box_area\":" << d.boxArea << ",";
    os << "\"mask_area\":" << d.maskArea << ",";
    os << "\"mask_density\":" << d.maskDensity << ",";
    os << "\"red_ratio\":" << d.redRatio << ",";
    os << "\"orange_ratio\":" << d.orangeRatio << ",";
    os << "\"warm_ratio\":" << d.warmRatio << ",";
    os << "\"green_yellow_ratio\":" << d.greenYellowRatio << "},";
    os << "\"support\":{";
    os << "\"member_count\":" << d.supportMemberCount << ",";
    os << "\"ripe_count\":" << d.supportRipeCount << ",";
    os << "\"unripe_count\":" << d.supportUnripeCount << ",";
    os << "\"conf_sum\":" << d.supportConfSum << ",";
    os << "\"ripe_conf_sum\":" << d.supportRipeConfSum << ",";
    os << "\"unripe_conf_sum\":" << d.supportUnripeConfSum << ",";
    os << "\"score\":" << d.bunchSupportScore << "},";
    os << "\"maturity\":{";
    os << "\"winner\":" << (d.maturityCompetitionWinner ? "true" : "false") << ",";
    os << "\"lost\":" << (d.lostMaturityCompetition ? "true" : "false") << ",";
    os << "\"score\":" << d.maturityScore << ",";
    os << "\"score_ripe\":" << d.maturityScoreRipe << ",";
    os << "\"score_unripe\":" << d.maturityScoreUnripe << ",";
    os << "\"reason\":" << jsonEscape(d.maturityCompetitionReason) << "}";
    os << "}\n";
}

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        return 2;
    }

    args.projectRoot = fs::absolute(args.projectRoot).lexically_normal();
    args.sessionPath = absUnderProject(args.projectRoot, args.sessionPath);
    args.enginePath = absUnderProject(args.projectRoot, args.enginePath);

    const std::string sessionId = args.sessionPath.filename().string();
    if (args.outputDir.empty()) {
        args.outputDir = args.projectRoot / "policy_replay_lab" / "outputs" / sessionId / "cpp_replay";
    } else {
        args.outputDir = absUnderProject(args.projectRoot, args.outputDir);
    }

    if (!fs::exists(args.sessionPath) || !fs::is_directory(args.sessionPath)) {
        std::cerr << "session folder not found: " << args.sessionPath << "\n";
        return 1;
    }
    if (!fs::exists(args.enginePath) || !fs::is_regular_file(args.enginePath)) {
        std::cerr << "TensorRT engine not found: " << args.enginePath << "\n";
        std::cerr << "This replay tool does not intentionally build/convert a model. Fix --engine path.\n";
        return 1;
    }

    fs::create_directories(args.outputDir);
    const fs::path detJsonlPath = args.outputDir / "cpp_replay_detections.jsonl";
    const fs::path rawJsonlPath = args.outputDir / "cpp_replay_raw_candidates.jsonl";
    const fs::path summaryPath = args.outputDir / "cpp_replay_summary.json";

    std::ofstream detOut(detJsonlPath, std::ios::out | std::ios::trunc);
    std::ofstream rawOut(rawJsonlPath, std::ios::out | std::ios::trunc);
    if (!detOut.is_open() || !rawOut.is_open()) {
        std::cerr << "failed to open output JSONL files under: " << args.outputDir << "\n";
        return 1;
    }

    auto cfg = makeReplayConfig(args);
    cfg.enginePath = args.enginePath.generic_string();

    // Make relative paths inside ObjectDetector predictable even if the CLI is started elsewhere.
    fs::current_path(args.projectRoot);

    ObjectDetector detector(cfg);
    if (!detector.init()) {
        std::cerr << "detector.init() failed\n";
        return 1;
    }

    int frameId = 0;
    int totalImages = 0;
    int failedImages = 0;
    int totalFinalDetections = 0;
    int totalRawCandidates = 0;
    std::map<std::string, int> imagesByGroup;

    for (const auto& groupName : args.groups) {
        const fs::path groupDir = findGroupDir(args.sessionPath, groupName);
        if (groupDir.empty()) {
            std::cerr << "warning: image group not found: " << groupName << "\n";
            continue;
        }

        auto images = listImagesSorted(groupDir);
        if (args.maxImagesPerGroup > 0 && images.size() > static_cast<size_t>(args.maxImagesPerGroup)) {
            images.resize(static_cast<size_t>(args.maxImagesPerGroup));
        }

        std::cout << "[replay] group=" << groupName << " dir=" << groupDir
                  << " images=" << images.size() << "\n";

        for (const auto& imgPath : images) {
            frameId += 1;
            totalImages += 1;
            imagesByGroup[groupName] += 1;

            cv::Mat bgr = cv::imread(imgPath.string(), cv::IMREAD_COLOR);
            if (bgr.empty()) {
                std::cerr << "warning: failed to read image: " << imgPath << "\n";
                failedImages += 1;
                continue;
            }

            ObjectDetector::Snapshot snap;
            if (!detector.processBgrForReplay(bgr, snap)) {
                std::cerr << "warning: replay failed for image: " << imgPath << "\n";
                failedImages += 1;
                continue;
            }

            const std::string relSession = safeRelative(args.sessionPath, imgPath);
            const std::string absImage = fs::absolute(imgPath).lexically_normal().generic_string();

            int did = 0;
            for (const auto& d : snap.detections) {
                writeDetectionJson(detOut, sessionId, groupName, relSession, absImage, frameId, did++, d, "final_policy");
                totalFinalDetections += 1;
            }

            int rid = 0;
            for (const auto& d : snap.rawCandidates) {
                writeDetectionJson(rawOut, sessionId, groupName, relSession, absImage, frameId, rid++, d, "raw_candidate");
                totalRawCandidates += 1;
            }
        }
    }

    std::ofstream summary(summaryPath, std::ios::out | std::ios::trunc);
    summary << std::fixed << std::setprecision(6);
    summary << "{\n";
    summary << "  \"schema\": \"rbv2_policy_replay_cpp_summary_v1\",\n";
    summary << "  \"session_id\": " << jsonEscape(sessionId) << ",\n";
    summary << "  \"mode\": " << jsonEscape(args.mode) << ",\n";
    summary << "  \"project_root\": " << jsonEscape(args.projectRoot.generic_string()) << ",\n";
    summary << "  \"session_path\": " << jsonEscape(args.sessionPath.generic_string()) << ",\n";
    summary << "  \"engine_path\": " << jsonEscape(args.enginePath.generic_string()) << ",\n";
    summary << "  \"output_dir\": " << jsonEscape(args.outputDir.generic_string()) << ",\n";
    summary << "  \"images_total\": " << totalImages << ",\n";
    summary << "  \"images_failed\": " << failedImages << ",\n";
    summary << "  \"final_detections_total\": " << totalFinalDetections << ",\n";
    summary << "  \"raw_candidates_total\": " << totalRawCandidates << ",\n";
    summary << "  \"images_by_group\": {";
    bool first = true;
    for (const auto& kv : imagesByGroup) {
        if (!first) summary << ",";
        first = false;
        summary << "\n    " << jsonEscape(kv.first) << ": " << kv.second;
    }
    if (!imagesByGroup.empty()) summary << "\n  ";
    summary << "}\n";
    summary << "}\n";

    std::cout << "[replay] done\n";
    std::cout << "[replay] detections: " << detJsonlPath << "\n";
    std::cout << "[replay] raw candidates: " << rawJsonlPath << "\n";
    std::cout << "[replay] summary: " << summaryPath << "\n";
    return 0;
}

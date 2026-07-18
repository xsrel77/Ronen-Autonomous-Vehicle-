#include "perception/ObjectDetector.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <opencv2/opencv.hpp>

#include <cuda_runtime_api.h>
#include <NvInfer.h>
#include <NvOnnxParser.h>

namespace fs = std::filesystem;

class TrtLogger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity > Severity::kWARNING) return;
        std::cerr << "[TensorRT] " << msg << "\n";
    }
};

static TrtLogger gLogger;

static uint64_t nowMsLocal() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static int64_t volume(const nvinfer1::Dims& dims) {
    int64_t v = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        v *= static_cast<int64_t>(dims.d[i]);
    }
    return v;
}

static float iouXYWH(const ObjectDetector::Detection& a, const ObjectDetector::Detection& b) {
    const float ax1 = a.x;
    const float ay1 = a.y;
    const float ax2 = a.x + a.w;
    const float ay2 = a.y + a.h;

    const float bx1 = b.x;
    const float by1 = b.y;
    const float bx2 = b.x + b.w;
    const float by2 = b.y + b.h;

    const float ix1 = std::max(ax1, bx1);
    const float iy1 = std::max(ay1, by1);
    const float ix2 = std::min(ax2, bx2);
    const float iy2 = std::min(ay2, by2);

    const float iw = std::max(0.0f, ix2 - ix1);
    const float ih = std::max(0.0f, iy2 - iy1);
    const float inter = iw * ih;
    const float uni = a.w * a.h + b.w * b.h - inter;
    if (uni <= 0.0f) return 0.0f;
    return inter / uni;
}

static std::vector<ObjectDetector::Detection>
applyNms(std::vector<ObjectDetector::Detection> dets, float nmsThr) {
    std::sort(dets.begin(), dets.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });

    std::vector<ObjectDetector::Detection> out;
    std::vector<bool> removed(dets.size(), false);

    for (size_t i = 0; i < dets.size(); ++i) {
        if (removed[i]) continue;
        out.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (removed[j]) continue;
            if (dets[i].classId == dets[j].classId &&
                iouXYWH(dets[i], dets[j]) > nmsThr) {
                removed[j] = true;
            }
        }
    }
    return out;
}

struct FrameLevelTrack {
    int id = -1;
    ObjectDetector::Detection det{};
    float bestConfidence = 0.0f;
    std::string bestLabel;
    int stableClassId = -1;
    std::string stableLabel;
    float stableClassScore = 0.0f;
    int pendingClassId = -1;
    int pendingClassFrames = 0;
    int hits = 0;
    int age = 0;
    bool matched = false;
};

static std::string makeCsiPipeline(int sensorId, int captureW, int captureH, int fps) {
    std::ostringstream ss;
    ss
        << "nvarguscamerasrc sensor-id=" << sensorId << " ! "
        << "video/x-raw(memory:NVMM), width=(int)" << captureW
        << ", height=(int)" << captureH
        << ", format=(string)NV12, framerate=(fraction)" << fps << "/1 ! "
        << "nvvidconv flip-method=2 ! "
        << "video/x-raw, format=(string)BGRx ! "
        << "videoconvert ! "
        << "video/x-raw, format=(string)BGR ! "
        << "appsink drop=true sync=false max-buffers=1";
    return ss.str();
}

struct ObjectDetector::Impl {
    cv::VideoCapture cap;
    cv::Mat lastBgrFrame;

    nvinfer1::IRuntime* runtime = nullptr;
    nvinfer1::ICudaEngine* engine = nullptr;
    nvinfer1::IExecutionContext* context = nullptr;

    cudaStream_t stream = nullptr;

    struct OutputBinding {
        std::string name;
        nvinfer1::Dims dims{};
        void* device = nullptr;
        size_t bytes = 0;
        std::vector<float> host;
    };

    std::string inputName;
    nvinfer1::Dims inputDims{};

    void* dInput = nullptr;
    size_t inputBytes = 0;

    std::vector<float> hostInput;
    std::vector<OutputBinding> outputs;
    int detectionOutputIndex = -1;
    int protoOutputIndex = -1;

    std::vector<FrameLevelTrack> frameTracks;
    std::mutex frameTracksMutex;
    int nextFrameTrackId = 1;
    int frameCounter = 0;
    int cameraMotionCooldownUntilFrame = 0;

    bool cameraOpened = false;
    bool engineReady = false;
};

ObjectDetector::ObjectDetector()
    : impl_(std::make_unique<Impl>()), cfg_{} {
    cfg_.showGui = false;
}

ObjectDetector::ObjectDetector(const Config& cfg)
    : impl_(std::make_unique<Impl>()), cfg_(cfg) {
    cfg_.showGui = false;
}

ObjectDetector::~ObjectDetector() {
    stop();

    if (impl_->dInput) cudaFree(impl_->dInput);
    for (auto& out : impl_->outputs) {
        if (out.device) cudaFree(out.device);
        out.device = nullptr;
    }
    if (impl_->stream) cudaStreamDestroy(impl_->stream);

    delete impl_->context;
    delete impl_->engine;
    delete impl_->runtime;
}

static bool readBinaryFile(const std::string& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    out.resize(sz);
    f.read(out.data(), static_cast<std::streamsize>(sz));
    return true;
}

static bool writeBinaryFile(const std::string& path, const void* data, size_t sz) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(sz));
    return true;
}

static bool ensureParentDir(const std::string& filePath) {
    std::error_code ec;
    fs::create_directories(fs::path(filePath).parent_path(), ec);
    return true;
}

static bool buildEngineFromOnnx(const ObjectDetector::Config& cfg,
                                std::string& err,
                                std::vector<char>& serializedOut) {
    if (!fs::exists(cfg.onnxPath)) {
        err = "ONNX file not found: " + cfg.onnxPath;
        return false;
    }

    auto* builder = nvinfer1::createInferBuilder(gLogger);
    if (!builder) {
        err = "createInferBuilder failed";
        return false;
    }

    auto* network = builder->createNetworkV2(0);
    if (!network) {
        delete builder;
        err = "createNetworkV2 failed";
        return false;
    }

    auto* parser = nvonnxparser::createParser(*network, gLogger);
    if (!parser) {
        delete network;
        delete builder;
        err = "createParser failed";
        return false;
    }

    if (!parser->parseFromFile(cfg.onnxPath.c_str(),
                               static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
        delete parser;
        delete network;
        delete builder;
        err = "ONNX parse failed";
        return false;
    }

    auto* config = builder->createBuilderConfig();
    if (!config) {
        delete parser;
        delete network;
        delete builder;
        err = "createBuilderConfig failed";
        return false;
    }

    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30);

    if (cfg.useFP16 && builder->platformHasFastFp16()) {
        config->setFlag(nvinfer1::BuilderFlag::kFP16);
    }

    auto* plan = builder->buildSerializedNetwork(*network, *config);
    if (!plan) {
        delete config;
        delete parser;
        delete network;
        delete builder;
        err = "buildSerializedNetwork failed";
        return false;
    }

    serializedOut.resize(plan->size());
    std::memcpy(serializedOut.data(), plan->data(), plan->size());

    delete plan;
    delete config;
    delete parser;
    delete network;
    delete builder;
    return true;
}

bool ObjectDetector::init() {
    try {
        if (ready_.load()) {
            return true;
        }

        if (!fs::exists(cfg_.enginePath)) {
            std::cout << "[detector] TensorRT engine not found, building from ONNX: "
                      << cfg_.onnxPath << "\n";

            std::vector<char> serialized;
            std::string err;
            if (!buildEngineFromOnnx(cfg_, err, serialized)) {
                std::cerr << "[detector] build engine failed: " << err << "\n";
                return false;
            }

            ensureParentDir(cfg_.enginePath);
            if (!writeBinaryFile(cfg_.enginePath, serialized.data(), serialized.size())) {
                std::cerr << "[detector] failed to save engine: " << cfg_.enginePath << "\n";
                return false;
            }

            std::cout << "[detector] engine saved to: " << cfg_.enginePath << "\n";
        }

        std::vector<char> engineData;
        if (!readBinaryFile(cfg_.enginePath, engineData)) {
            std::cerr << "[detector] failed to read engine: " << cfg_.enginePath << "\n";
            return false;
        }

        impl_->runtime = nvinfer1::createInferRuntime(gLogger);
        if (!impl_->runtime) {
            std::cerr << "[detector] createInferRuntime failed\n";
            return false;
        }

        impl_->engine = impl_->runtime->deserializeCudaEngine(engineData.data(), engineData.size());
        if (!impl_->engine) {
            std::cerr << "[detector] deserializeCudaEngine failed\n";
            return false;
        }

        impl_->context = impl_->engine->createExecutionContext();
        if (!impl_->context) {
            std::cerr << "[detector] createExecutionContext failed\n";
            return false;
        }

        impl_->outputs.clear();
        impl_->detectionOutputIndex = -1;
        impl_->protoOutputIndex = -1;

        const int nb = impl_->engine->getNbIOTensors();
        for (int i = 0; i < nb; ++i) {
            const char* name = impl_->engine->getIOTensorName(i);
            const auto mode = impl_->engine->getTensorIOMode(name);
            if (mode == nvinfer1::TensorIOMode::kINPUT) {
                impl_->inputName = name;
            } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
                ObjectDetector::Impl::OutputBinding out;
                out.name = name;
                impl_->outputs.push_back(std::move(out));
            }
        }

        if (impl_->inputName.empty() || impl_->outputs.empty()) {
            std::cerr << "[detector] failed to identify input/output tensors\n";
            return false;
        }

        const nvinfer1::Dims4 inShape{1, 3, cfg_.inputHeight, cfg_.inputWidth};
        if (!impl_->context->setInputShape(impl_->inputName.c_str(), inShape)) {
            std::cerr << "[detector] setInputShape failed\n";
            return false;
        }

        impl_->inputDims = impl_->context->getTensorShape(impl_->inputName.c_str());
        const int64_t inVol = volume(impl_->inputDims);

        if (inVol <= 0) {
            std::cerr << "[detector] invalid input tensor volume\n";
            return false;
        }

        impl_->inputBytes = static_cast<size_t>(inVol) * sizeof(float);
        impl_->hostInput.resize(static_cast<size_t>(inVol));

        if (cudaMalloc(&impl_->dInput, impl_->inputBytes) != cudaSuccess) {
            std::cerr << "[detector] cudaMalloc input failed\n";
            return false;
        }

        for (size_t i = 0; i < impl_->outputs.size(); ++i) {
            auto& out = impl_->outputs[i];
            out.dims = impl_->context->getTensorShape(out.name.c_str());
            const int64_t outVol = volume(out.dims);
            if (outVol <= 0) {
                std::cerr << "[detector] invalid output tensor volume for " << out.name << "\n";
                return false;
            }

            out.bytes = static_cast<size_t>(outVol) * sizeof(float);
            out.host.resize(static_cast<size_t>(outVol));

            if (cudaMalloc(&out.device, out.bytes) != cudaSuccess) {
                std::cerr << "[detector] cudaMalloc output failed for " << out.name << "\n";
                return false;
            }
        }

        if (cudaStreamCreate(&impl_->stream) != cudaSuccess) {
            std::cerr << "[detector] cudaStreamCreate failed\n";
            return false;
        }

        impl_->context->setTensorAddress(impl_->inputName.c_str(), impl_->dInput);
        for (auto& out : impl_->outputs) {
            impl_->context->setTensorAddress(out.name.c_str(), out.device);
        }

        // YOLOv8-seg has two outputs:
        //   output0: detections/classes/mask coefficients, e.g. 1x40x8400
        //   output1: mask prototypes, e.g. 1x32x160x160
        // Part B uses output0 + output1 for segmentation masks and color filtering.
        for (size_t i = 0; i < impl_->outputs.size(); ++i) {
            const auto& out = impl_->outputs[i];
            const auto& dims = out.dims;

            if (out.name == "output0") {
                impl_->detectionOutputIndex = static_cast<int>(i);
            } else if (out.name == "output1") {
                impl_->protoOutputIndex = static_cast<int>(i);
            }

            if (dims.nbDims >= 2) {
                const int last = dims.d[dims.nbDims - 1];
                const int prev = dims.d[dims.nbDims - 2];
                if (impl_->detectionOutputIndex < 0 && (last == 8400 || prev == 8400)) {
                    impl_->detectionOutputIndex = static_cast<int>(i);
                }
            }

            if (dims.nbDims >= 3) {
                const int last = dims.d[dims.nbDims - 1];
                const int prev = dims.d[dims.nbDims - 2];
                const int maybeChannels = dims.d[dims.nbDims - 3];
                if (impl_->protoOutputIndex < 0 && maybeChannels == 32 && last <= 320 && prev <= 320) {
                    impl_->protoOutputIndex = static_cast<int>(i);
                }
            }
        }

        if (impl_->detectionOutputIndex < 0) {
            std::cerr << "[detector] failed to identify YOLO detection output tensor\n";
            return false;
        }

        if (cfg_.useSegmentationMasks && impl_->protoOutputIndex < 0) {
            std::cerr << "[detector] warning: YOLO mask prototype output was not found; "
                      << "falling back to box-only filtering\n";
        }

        impl_->engineReady = true;
        ready_.store(true);

        std::cout << "[detector] ready. input=" << impl_->inputName << " outputs=";
        for (const auto& out : impl_->outputs) {
            std::cout << out.name << " ";
        }
        std::cout << "detectionOutput=" << impl_->outputs[impl_->detectionOutputIndex].name;
        if (impl_->protoOutputIndex >= 0) {
            std::cout << " protoOutput=" << impl_->outputs[impl_->protoOutputIndex].name;
        }
        std::cout << "\n";

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[detector] init exception: " << e.what() << "\n";
        return false;
    }
}

bool ObjectDetector::isReady() const {
    return ready_.load();
}

void ObjectDetector::releaseCamera() {
    if (impl_->cap.isOpened()) {
        impl_->cap.release();
    }
    impl_->cameraOpened = false;
}

bool ObjectDetector::openCameraOnce() {
    const std::string pipeline = makeCsiPipeline(
        cfg_.sensorId, cfg_.cameraWidth, cfg_.cameraHeight, cfg_.cameraFps
    );

    std::cout << "[detector] opening CSI camera on CAM" << cfg_.sensorId
              << " via pipeline:\n" << pipeline << "\n";

    releaseCamera();

    if (!impl_->cap.open(pipeline, cv::CAP_GSTREAMER) || !impl_->cap.isOpened()) {
        releaseCamera();
        std::cerr << "[detector] failed to open CSI camera on sensor-id="
                  << cfg_.sensorId << "\n";
        return false;
    }

    impl_->cameraOpened = true;

    if (cfg_.cameraOpenSettleDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.cameraOpenSettleDelayMs));
    }

    return true;
}

void ObjectDetector::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);

    if (running_.load()) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    if (!ready_.load()) {
        if (!init()) return;
    }

    clearLatestSnapshot();
    {
        std::lock_guard<std::mutex> trackLock(impl_->frameTracksMutex);
        impl_->frameTracks.clear();
        impl_->nextFrameTrackId = 1;
        impl_->cameraMotionCooldownUntilFrame = 0;
    }
    impl_->frameCounter = 0;
    releaseCamera();

    if (!openCameraOnce()) {
        return;
    }

    running_.store(true);
    worker_ = std::thread(&ObjectDetector::workerLoop, this);
}

void ObjectDetector::stop() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);

        if (!running_.load() && !worker_.joinable()) {
            releaseCamera();
            clearLatestSnapshot();
            return;
        }

        running_.store(false);
        releaseCamera();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    releaseCamera();
    clearLatestSnapshot();
}

void ObjectDetector::toggle() {
    if (running_.load()) stop();
    else start();
}

bool ObjectDetector::isRunning() const {
    return running_.load();
}

const ObjectDetector::Config& ObjectDetector::config() const {
    return cfg_;
}

ObjectDetector::Config& ObjectDetector::config() {
    return cfg_;
}

void ObjectDetector::setDigitalZoom(float zoom) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        const float clamped = std::clamp(zoom, cfg_.digitalZoomMin, cfg_.digitalZoomMax);
        changed = std::fabs(clamped - cfg_.digitalZoom) > 0.001f;
        cfg_.digitalZoom = clamped;
    }
    if (changed) {
        notifyCameraMotion();
    }
}

void ObjectDetector::adjustDigitalZoom(float delta) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);

        if (!cfg_.digitalZoomLevels.empty()) {
            std::vector<float> levels = cfg_.digitalZoomLevels;
            std::sort(levels.begin(), levels.end());
            levels.erase(std::unique(levels.begin(), levels.end(),
                                     [](float a, float b) { return std::fabs(a - b) < 0.001f; }),
                         levels.end());

            int nearest = 0;
            float bestDist = std::fabs(levels[0] - cfg_.digitalZoom);
            for (int i = 1; i < static_cast<int>(levels.size()); ++i) {
                const float dist = std::fabs(levels[static_cast<size_t>(i)] - cfg_.digitalZoom);
                if (dist < bestDist) {
                    bestDist = dist;
                    nearest = i;
                }
            }

            int next = nearest;
            if (delta > 0.0f) {
                next = std::min(nearest + 1, static_cast<int>(levels.size()) - 1);
            } else if (delta < 0.0f) {
                next = std::max(nearest - 1, 0);
            }

            const float z = std::clamp(levels[static_cast<size_t>(next)],
                                       cfg_.digitalZoomMin,
                                       cfg_.digitalZoomMax);
            changed = std::fabs(z - cfg_.digitalZoom) > 0.001f;
            cfg_.digitalZoom = z;
        } else {
            const float z = std::clamp(cfg_.digitalZoom + delta,
                                       cfg_.digitalZoomMin,
                                       cfg_.digitalZoomMax);
            changed = std::fabs(z - cfg_.digitalZoom) > 0.001f;
            cfg_.digitalZoom = z;
        }
    }
    if (changed) {
        notifyCameraMotion();
    }
}

float ObjectDetector::getDigitalZoom() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return cfg_.digitalZoom;
}

void ObjectDetector::resetFrameTracking() {
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->frameTracksMutex);
    impl_->frameTracks.clear();
}

void ObjectDetector::notifyCameraMotion() {
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->frameTracksMutex);
    impl_->frameTracks.clear();
    impl_->cameraMotionCooldownUntilFrame = impl_->frameCounter +
        std::max(0, cfg_.trackCameraMotionCooldownFrames);
}

std::string ObjectDetector::getClassDisplayName(int classId, const std::string& fallbackLabel) const {
    auto it = cfg_.classNames.find(classId);
    if (it != cfg_.classNames.end() && !it->second.empty()) {
        return it->second;
    }
    return fallbackLabel;
}

const std::unordered_map<int, std::string>& ObjectDetector::getClassNameMap() const {
    return cfg_.classNames;
}

struct LetterboxInfo {
    bool enabled = false;
    float gain = 1.0f;
    float padX = 0.0f;
    float padY = 0.0f;
    int resizedW = 0;
    int resizedH = 0;
    int inputW = 0;
    int inputH = 0;
    int srcW = 0;
    int srcH = 0;
};

static cv::Mat makeLetterboxImage(const cv::Mat& bgr,
                                  int inputW,
                                  int inputH,
                                  bool useLetterbox,
                                  LetterboxInfo& info) {
    info = LetterboxInfo{};
    info.enabled = useLetterbox;
    info.inputW = inputW;
    info.inputH = inputH;
    info.srcW = bgr.cols;
    info.srcH = bgr.rows;

    if (!useLetterbox) {
        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size(inputW, inputH), 0.0, 0.0, cv::INTER_LINEAR);
        info.gain = static_cast<float>(inputW) / static_cast<float>(std::max(1, bgr.cols));
        info.resizedW = inputW;
        info.resizedH = inputH;
        return resized;
    }

    const float gain = std::min(static_cast<float>(inputW) / static_cast<float>(std::max(1, bgr.cols)),
                                static_cast<float>(inputH) / static_cast<float>(std::max(1, bgr.rows)));
    const int newW = std::max(1, static_cast<int>(std::round(static_cast<float>(bgr.cols) * gain)));
    const int newH = std::max(1, static_cast<int>(std::round(static_cast<float>(bgr.rows) * gain)));
    const int padLeft = (inputW - newW) / 2;
    const int padTop = (inputH - newH) / 2;

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(newW, newH), 0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat canvas(inputH, inputW, bgr.type(), cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(padLeft, padTop, newW, newH)));

    info.gain = gain;
    info.padX = static_cast<float>(padLeft);
    info.padY = static_cast<float>(padTop);
    info.resizedW = newW;
    info.resizedH = newH;
    return canvas;
}

static void preprocessToCHW(const cv::Mat& bgr,
                            int inputW,
                            int inputH,
                            bool useLetterbox,
                            std::vector<float>& out,
                            LetterboxInfo& info) {
    cv::Mat networkBgr = makeLetterboxImage(bgr, inputW, inputH, useLetterbox, info);

    cv::Mat rgb;
    cv::cvtColor(networkBgr, rgb, cv::COLOR_BGR2RGB);

    cv::Mat f32;
    rgb.convertTo(f32, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> chans(3);
    cv::split(f32, chans);

    const int plane = inputW * inputH;
    out.resize(static_cast<size_t>(3 * plane));
    for (int c = 0; c < 3; ++c) {
        std::memcpy(out.data() + c * plane, chans[c].data, plane * sizeof(float));
    }
}

static cv::Mat applyDigitalZoomToFrame(const cv::Mat& frame, float zoom) {
    if (frame.empty() || zoom <= 1.001f) {
        return frame;
    }

    zoom = std::max(1.0f, zoom);
    const int srcW = frame.cols;
    const int srcH = frame.rows;
    if (srcW <= 1 || srcH <= 1) {
        return frame;
    }

    int cropW = static_cast<int>(std::round(static_cast<float>(srcW) / zoom));
    int cropH = static_cast<int>(std::round(static_cast<float>(srcH) / zoom));
    cropW = std::clamp(cropW, 1, srcW);
    cropH = std::clamp(cropH, 1, srcH);

    const int x = std::max(0, (srcW - cropW) / 2);
    const int y = std::max(0, (srcH - cropH) / 2);

    cv::Rect roi(x, y, cropW, cropH);
    cv::Mat cropped = frame(roi);
    cv::Mat zoomed;
    cv::resize(cropped, zoomed, cv::Size(srcW, srcH), 0.0, 0.0, cv::INTER_LINEAR);
    return zoomed;
}

static bool isBunchClassId(int classId) {
    return classId == 0 || classId == 3;
}

static bool isSingleClassId(int classId) {
    return classId == 1 || classId == 2;
}

static float classThresholdFor(const ObjectDetector::Config& cfg, int classId) {
    auto it = cfg.classThresholds.find(classId);
    if (it != cfg.classThresholds.end()) {
        return std::max(cfg.confThreshold, it->second);
    }
    return cfg.confThreshold;
}

static bool insideRoiFilter(const ObjectDetector::Detection& det,
                            int srcW,
                            int srcH,
                            const ObjectDetector::Config& cfg) {
    if (!cfg.useRoiFilter) return true;
    const float cx = det.x + det.w * 0.5f;
    const float cy = det.y + det.h * 0.5f;
    return cx >= cfg.roiXMin * srcW && cx <= cfg.roiXMax * srcW &&
           cy >= cfg.roiYMin * srcH && cy <= cfg.roiYMax * srcH;
}

static std::vector<ObjectDetector::Detection>
postprocessYoloV8Raw(const std::vector<float>& out,
                     const nvinfer1::Dims& dims,
                     int srcW,
                     int srcH,
                     int inputW,
                     int inputH,
                     const LetterboxInfo& lb,
                     float rawConfThr,
                     float nmsThr,
                     bool personOnly,
                     int modelClassCount,
                     const std::unordered_map<int, std::string>& classNames) {
    std::vector<ObjectDetector::Detection> dets;

    int attrs = 0;
    int boxes = 0;
    bool transposed = false;

    std::vector<int> d;
    for (int i = 0; i < dims.nbDims; ++i) d.push_back(dims.d[i]);

    if (d.size() == 3) {
        int a = d[0], b = d[1], c = d[2];
        if (a == 1) {
            if (b >= 6 && c > 10) {
                attrs = b; boxes = c; transposed = false;
            } else if (c >= 6 && b > 10) {
                attrs = c; boxes = b; transposed = true;
            }
        } else {
            if (a >= 6 && b > 10) {
                attrs = a; boxes = b; transposed = false;
            } else if (b >= 6 && a > 10) {
                attrs = b; boxes = a; transposed = true;
            }
        }
    } else if (d.size() == 2) {
        int a = d[0], b = d[1];
        if (a >= 6 && b > 10) {
            attrs = a; boxes = b; transposed = false;
        } else if (b >= 6 && a > 10) {
            attrs = b; boxes = a; transposed = true;
        }
    }

    // Important: decode only the real YOLO classes.
    // Heuristic-only labels must not be counted as model classes, otherwise the
    // first mask coefficient is interpreted as a fifth class score and all masks shift.
    const int numClasses = std::max(1, modelClassCount);
    const int firstClassAttr = 4;
    const int firstMaskCoeffAttr = firstClassAttr + numClasses;
    const int maskCoeffCount = attrs - firstMaskCoeffAttr;

    if (attrs < firstMaskCoeffAttr || boxes <= 0 || numClasses <= 0) {
        return dets;
    }

    auto at = [&](int boxIdx, int attrIdx) -> float {
        if (!transposed) {
            return out[static_cast<size_t>(attrIdx) * boxes + boxIdx];
        }
        return out[static_cast<size_t>(boxIdx) * attrs + attrIdx];
    };

    const float directSx = static_cast<float>(srcW) / static_cast<float>(std::max(1, inputW));
    const float directSy = static_cast<float>(srcH) / static_cast<float>(std::max(1, inputH));
    const float gain = lb.enabled ? std::max(1e-6f, lb.gain) : 1.0f;

    for (int i = 0; i < boxes; ++i) {
        const float cx = at(i, 0);
        const float cy = at(i, 1);
        const float w  = at(i, 2);
        const float h  = at(i, 3);

        int bestClass = -1;
        float bestScore = 0.0f;

        if (personOnly) {
            bestClass = 0;
            bestScore = at(i, firstClassAttr);
        } else {
            for (int cls = firstClassAttr; cls < firstMaskCoeffAttr; ++cls) {
                const float score = at(i, cls);
                if (score > bestScore) {
                    bestScore = score;
                    bestClass = cls - firstClassAttr;
                }
            }
        }

        if (bestScore < rawConfThr) continue;

        const float x1Net = cx - w * 0.5f;
        const float y1Net = cy - h * 0.5f;
        const float x2Net = cx + w * 0.5f;
        const float y2Net = cy + h * 0.5f;

        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        if (lb.enabled) {
            x1 = (x1Net - lb.padX) / gain;
            y1 = (y1Net - lb.padY) / gain;
            x2 = (x2Net - lb.padX) / gain;
            y2 = (y2Net - lb.padY) / gain;
        } else {
            x1 = x1Net * directSx;
            y1 = y1Net * directSy;
            x2 = x2Net * directSx;
            y2 = y2Net * directSy;
        }

        x1 = std::max(0.0f, std::min(static_cast<float>(srcW - 1), x1));
        y1 = std::max(0.0f, std::min(static_cast<float>(srcH - 1), y1));
        x2 = std::max(0.0f, std::min(static_cast<float>(srcW), x2));
        y2 = std::max(0.0f, std::min(static_cast<float>(srcH), y2));
        if (x2 <= x1 + 1.0f || y2 <= y1 + 1.0f) continue;

        ObjectDetector::Detection det;
        det.x = x1;
        det.y = y1;
        det.w = x2 - x1;
        det.h = y2 - y1;
        det.confidence = bestScore;
        det.currentConfidence = bestScore;
        det.bestConfidence = bestScore;
        det.classId = bestClass;

        auto it = classNames.find(bestClass);
        det.label = (it != classNames.end()) ? it->second : std::to_string(bestClass);
        det.valid = true;
        det.boxArea = det.w * det.h;

        if (maskCoeffCount > 0) {
            det.maskCoeffs.resize(static_cast<size_t>(maskCoeffCount));
            for (int k = 0; k < maskCoeffCount; ++k) {
                det.maskCoeffs[static_cast<size_t>(k)] = at(i, firstMaskCoeffAttr + k);
            }
        }

        dets.push_back(std::move(det));
    }

    // NMS is intentionally per-class. A ripe tomato must not suppress an overlapping bunch candidate.
    return applyNms(std::move(dets), nmsThr);
}

static bool parseProtoShape(const nvinfer1::Dims& dims,
                            int& channels,
                            int& protoH,
                            int& protoW,
                            size_t& offset) {
    channels = protoH = protoW = 0;
    offset = 0;

    if (dims.nbDims == 4) {
        // Expected: 1x32x160x160
        channels = dims.d[1];
        protoH = dims.d[2];
        protoW = dims.d[3];
    } else if (dims.nbDims == 3) {
        // Expected: 32x160x160
        channels = dims.d[0];
        protoH = dims.d[1];
        protoW = dims.d[2];
    } else {
        return false;
    }

    return channels > 0 && protoH > 0 && protoW > 0;
}

static float sigmoidFast(float x) {
    if (x >= 40.0f) return 1.0f;
    if (x <= -40.0f) return 0.0f;
    return 1.0f / (1.0f + std::exp(-x));
}

static bool isRipeClassId(int classId) {
    return classId == 0 || classId == 1;
}

static bool isUnripeClassId(int classId) {
    return classId == 2 || classId == 3;
}

static bool sameMaturityCompetitionGroup(int a, int b) {
    if (a == b) return true;
    return (isSingleClassId(a) && isSingleClassId(b)) ||
           (isBunchClassId(a) && isBunchClassId(b));
}

static bool isMaturityCompetingPair(int a, int b) {
    if (a == b) return false;
    return sameMaturityCompetitionGroup(a, b);
}

static float tomatoWarmRatio(const ObjectDetector::Detection& d) {
    return std::max(d.warmRatio, d.redRatio + d.orangeRatio);
}

static int ripeEquivalentClassId(int classId) {
    return isBunchClassId(classId) ? 0 : 1;
}

static int unripeEquivalentClassId(int classId) {
    return isBunchClassId(classId) ? 3 : 2;
}

static int tomatoCentersInsideBox(const std::vector<ObjectDetector::Detection>& singles,
                                  const ObjectDetector::Detection& bunch) {
    int count = 0;
    const float x1 = bunch.x;
    const float y1 = bunch.y;
    const float x2 = bunch.x + bunch.w;
    const float y2 = bunch.y + bunch.h;

    for (const auto& s : singles) {
        const float cx = s.x + s.w * 0.5f;
        const float cy = s.y + s.h * 0.5f;
        if (cx >= x1 && cx <= x2 && cy >= y1 && cy <= y2) {
            count += 1;
        }
    }

    return count;
}

static void buildMasksAndColorStats(std::vector<ObjectDetector::Detection>& dets,
                                    const std::vector<float>& proto,
                                    const nvinfer1::Dims& protoDims,
                                    const cv::Mat& bgr,
                                    const ObjectDetector::Config& cfg,
                                    const LetterboxInfo& lb,
                                    int srcW,
                                    int srcH) {
    int channels = 0;
    int protoH = 0;
    int protoW = 0;
    size_t protoOffset = 0;
    if (!cfg.useSegmentationMasks ||
        !parseProtoShape(protoDims, channels, protoH, protoW, protoOffset) ||
        proto.empty()) {
        for (auto& d : dets) {
            d.mask.clear();
            d.maskWidth = 0;
            d.maskHeight = 0;
            d.maskArea = 0.0f;
            d.maskDensity = 0.0f;
            d.redRatio = 0.0f;
            d.orangeRatio = 0.0f;
            d.warmRatio = 0.0f;
            d.greenYellowRatio = 0.0f;
        }
        return;
    }

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    const float protoToInputX = static_cast<float>(cfg.inputWidth) / static_cast<float>(protoW);
    const float protoToInputY = static_cast<float>(cfg.inputHeight) / static_cast<float>(protoH);
    const float gain = lb.enabled ? std::max(1e-6f, lb.gain) : 1.0f;
    const float areaScaleToSrc = lb.enabled
        ? (protoToInputX * protoToInputY) / (gain * gain)
        : (static_cast<float>(srcW) / static_cast<float>(protoW)) *
          (static_cast<float>(srcH) / static_cast<float>(protoH));

    auto srcToInputX = [&](float x) -> float {
        return lb.enabled ? x * gain + lb.padX : x * static_cast<float>(cfg.inputWidth) / static_cast<float>(std::max(1, srcW));
    };
    auto srcToInputY = [&](float y) -> float {
        return lb.enabled ? y * gain + lb.padY : y * static_cast<float>(cfg.inputHeight) / static_cast<float>(std::max(1, srcH));
    };
    auto inputToSrcX = [&](float x) -> float {
        return lb.enabled ? (x - lb.padX) / gain : x * static_cast<float>(srcW) / static_cast<float>(std::max(1, cfg.inputWidth));
    };
    auto inputToSrcY = [&](float y) -> float {
        return lb.enabled ? (y - lb.padY) / gain : y * static_cast<float>(srcH) / static_cast<float>(std::max(1, cfg.inputHeight));
    };

    const int displayMaskW = std::max(16, cfg.displayMaskWidth > 0 ? cfg.displayMaskWidth : protoW);
    const int displayMaskH = std::max(16, cfg.displayMaskHeight > 0 ? cfg.displayMaskHeight :
        std::max(1, static_cast<int>(std::round(static_cast<double>(displayMaskW) *
                                                static_cast<double>(srcH) /
                                                static_cast<double>(std::max(1, srcW))))));

    for (auto& det : dets) {
        det.mask.assign(static_cast<size_t>(displayMaskW * displayMaskH), 0);
        det.maskWidth = displayMaskW;
        det.maskHeight = displayMaskH;
        det.maskArea = 0.0f;
        det.maskDensity = 0.0f;
        det.redRatio = 0.0f;
        det.greenYellowRatio = 0.0f;

        if (det.maskCoeffs.size() < static_cast<size_t>(channels)) {
            det.mask.clear();
            det.maskWidth = 0;
            det.maskHeight = 0;
            continue;
        }

        const float x1Input = srcToInputX(det.x);
        const float y1Input = srcToInputY(det.y);
        const float x2Input = srcToInputX(det.x + det.w);
        const float y2Input = srcToInputY(det.y + det.h);

        int x1 = static_cast<int>(std::floor(x1Input / protoToInputX));
        int y1 = static_cast<int>(std::floor(y1Input / protoToInputY));
        int x2 = static_cast<int>(std::ceil(x2Input / protoToInputX));
        int y2 = static_cast<int>(std::ceil(y2Input / protoToInputY));

        x1 = std::max(0, std::min(protoW - 1, x1));
        y1 = std::max(0, std::min(protoH - 1, y1));
        x2 = std::max(0, std::min(protoW, x2));
        y2 = std::max(0, std::min(protoH, y2));

        if (x2 <= x1 || y2 <= y1) {
            det.mask.clear();
            det.maskWidth = 0;
            det.maskHeight = 0;
            continue;
        }

        int validColorPixels = 0;
        int redPixels = 0;
        int orangePixels = 0;
        int warmPixels = 0;
        int greenYellowPixels = 0;
        int maskAreaProto = 0;

        const int plane = protoW * protoH;
        for (int y = y1; y < y2; ++y) {
            for (int x = x1; x < x2; ++x) {
                const int pix = y * protoW + x;
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c) {
                    const size_t idx = protoOffset + static_cast<size_t>(c * plane + pix);
                    sum += det.maskCoeffs[static_cast<size_t>(c)] * proto[idx];
                }

                if (sigmoidFast(sum) <= cfg.maskThreshold) continue;

                maskAreaProto += 1;

                const float inputX = (static_cast<float>(x) + 0.5f) * protoToInputX;
                const float inputY = (static_cast<float>(y) + 0.5f) * protoToInputY;
                const int sx = static_cast<int>(std::round(inputToSrcX(inputX)));
                const int sy = static_cast<int>(std::round(inputToSrcY(inputY)));
                if (sx < 0 || sx >= srcW || sy < 0 || sy >= srcH) {
                    continue;
                }

                const int mxDisplay = std::clamp(
                    static_cast<int>(std::floor((static_cast<float>(sx) + 0.5f) *
                                                static_cast<float>(displayMaskW) /
                                                static_cast<float>(std::max(1, srcW)))),
                    0, displayMaskW - 1);
                const int myDisplay = std::clamp(
                    static_cast<int>(std::floor((static_cast<float>(sy) + 0.5f) *
                                                static_cast<float>(displayMaskH) /
                                                static_cast<float>(std::max(1, srcH)))),
                    0, displayMaskH - 1);
                det.mask[static_cast<size_t>(myDisplay) * static_cast<size_t>(displayMaskW) +
                         static_cast<size_t>(mxDisplay)] = 255;

                const cv::Vec3b hsvPixel = hsv.at<cv::Vec3b>(sy, sx);
                const int h = hsvPixel[0];
                const int s = hsvPixel[1];
                const int v = hsvPixel[2];

                if (s > 35 && v > 35) {
                    validColorPixels += 1;
                    const bool redHue = ((h <= 10) || (h >= 170)) && s > 50 && v > 50;
                    const bool orangeHue = (h > 10 && h < 25) && s > 45 && v > 45;
                    const bool greenYellowHue = (h >= 20 && h <= 90);
                    if (redHue) {
                        redPixels += 1;
                    }
                    if (orangeHue) {
                        orangePixels += 1;
                    }
                    if (redHue || orangeHue) {
                        warmPixels += 1;
                    }
                    if (greenYellowHue) {
                        greenYellowPixels += 1;
                    }
                }
            }
        }

        const float boxAreaProto = std::max(1.0f, static_cast<float>((x2 - x1) * (y2 - y1)));
        det.maskArea = static_cast<float>(maskAreaProto) * areaScaleToSrc;
        det.maskDensity = static_cast<float>(maskAreaProto) / boxAreaProto;
        det.boxArea = det.w * det.h;

        if (validColorPixels > 0) {
            det.redRatio = static_cast<float>(redPixels) / static_cast<float>(validColorPixels);
            det.orangeRatio = static_cast<float>(orangePixels) / static_cast<float>(validColorPixels);
            det.warmRatio = static_cast<float>(warmPixels) / static_cast<float>(validColorPixels);
            det.greenYellowRatio = static_cast<float>(greenYellowPixels) / static_cast<float>(validColorPixels);
        }
    }
}

static void clearInternalMaskCoeffs(std::vector<ObjectDetector::Detection>& dets) {
    for (auto& d : dets) {
        d.maskCoeffs.clear();
    }
}

bool ObjectDetector::inferDetectionsOnBgrInternal(const cv::Mat& bgr,
                                                   std::vector<ObjectDetector::Detection>& rawDetections,
                                                   std::string& err) {
    auto& impl = *impl_;
    const auto& cfg = cfg_;
    rawDetections.clear();
    err.clear();

    if (bgr.empty() || bgr.channels() != 3) {
        err = "invalid BGR frame for inference";
        return false;
    }

    LetterboxInfo letterbox;
    preprocessToCHW(bgr, cfg.inputWidth, cfg.inputHeight, cfg.useLetterboxPreprocess, impl.hostInput, letterbox);

    if (cudaMemcpyAsync(impl.dInput, impl.hostInput.data(), impl.inputBytes,
                        cudaMemcpyHostToDevice, impl.stream) != cudaSuccess) {
        err = "cudaMemcpyAsync H2D failed";
        return false;
    }

    if (!impl.context->enqueueV3(impl.stream)) {
        err = "enqueueV3 failed";
        return false;
    }

    for (auto& out : impl.outputs) {
        if (cudaMemcpyAsync(out.host.data(), out.device, out.bytes,
                            cudaMemcpyDeviceToHost, impl.stream) != cudaSuccess) {
            err = "cudaMemcpyAsync D2H failed for " + out.name;
            return false;
        }
    }

    if (cudaStreamSynchronize(impl.stream) != cudaSuccess) {
        err = "cudaStreamSynchronize failed";
        return false;
    }

    auto& detectionOutput = impl.outputs[static_cast<size_t>(impl.detectionOutputIndex)];
    rawDetections = postprocessYoloV8Raw(
        detectionOutput.host,
        detectionOutput.dims,
        bgr.cols,
        bgr.rows,
        cfg.inputWidth,
        cfg.inputHeight,
        letterbox,
        cfg.rawConfThreshold,
        cfg.nmsThreshold,
        cfg.personOnly,
        cfg.modelClassCount,
        cfg.classNames
    );

    if (cfg.useSegmentationMasks && impl.protoOutputIndex >= 0) {
        auto& protoOutput = impl.outputs[static_cast<size_t>(impl.protoOutputIndex)];
        buildMasksAndColorStats(rawDetections, protoOutput.host, protoOutput.dims,
                                bgr, cfg, letterbox, bgr.cols, bgr.rows);
    }

    return true;
}

struct RoiSecondPassCandidate {
    cv::Rect rect;
    int acceptedCount = 0;
    int weakCount = 0;
    int groupSize = 0;
    float score = 0.0f;
    std::string reason;
};

static cv::Rect clampRectToImage(const cv::Rect& r, int w, int h) {
    const int x1 = std::max(0, r.x);
    const int y1 = std::max(0, r.y);
    const int x2 = std::min(w, r.x + r.width);
    const int y2 = std::min(h, r.y + r.height);
    if (x2 <= x1 || y2 <= y1) return cv::Rect();
    return cv::Rect(x1, y1, x2 - x1, y2 - y1);
}

static cv::Rect expandedRectFromDetections(const std::vector<ObjectDetector::Detection>& dets,
                                           const std::vector<int>& idxs,
                                           const ObjectDetector::Config& cfg,
                                           int srcW,
                                           int srcH) {
    if (idxs.empty()) return cv::Rect();

    float x1 = static_cast<float>(srcW);
    float y1 = static_cast<float>(srcH);
    float x2 = 0.0f;
    float y2 = 0.0f;

    for (int idx : idxs) {
        const auto& d = dets[static_cast<size_t>(idx)];
        x1 = std::min(x1, d.x);
        y1 = std::min(y1, d.y);
        x2 = std::max(x2, d.x + d.w);
        y2 = std::max(y2, d.y + d.h);
    }

    const float bw = std::max(1.0f, x2 - x1);
    const float bh = std::max(1.0f, y2 - y1);
    const float pad = std::max(bw, bh) * cfg.roiSecondPassPadding;

    x1 -= pad;
    y1 -= pad;
    x2 += pad;
    y2 += pad;

    const int ix1 = static_cast<int>(std::floor(x1));
    const int iy1 = static_cast<int>(std::floor(y1));
    const int ix2 = static_cast<int>(std::ceil(x2));
    const int iy2 = static_cast<int>(std::ceil(y2));
    return clampRectToImage(cv::Rect(ix1, iy1, ix2 - ix1, iy2 - iy1), srcW, srcH);
}

static float centerDistance(const ObjectDetector::Detection& a, const ObjectDetector::Detection& b) {
    const float ax = a.x + a.w * 0.5f;
    const float ay = a.y + a.h * 0.5f;
    const float bx = b.x + b.w * 0.5f;
    const float by = b.y + b.h * 0.5f;
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

static bool areRoiGroupNeighbors(const ObjectDetector::Detection& a,
                                 const ObjectDetector::Detection& b,
                                 const ObjectDetector::Config& cfg) {
    const float iou = iouXYWH(a, b);
    if (iou >= cfg.roiSecondPassWeakOverlapIou) {
        return true;
    }

    const float si = std::max(a.w, a.h);
    const float sj = std::max(b.w, b.h);
    const float scale = std::max(20.0f, 0.5f * (si + sj));
    const float maxDist = scale * cfg.roiGroupMaxCenterDistanceFactor;
    return centerDistance(a, b) <= maxDist;
}

static std::vector<RoiSecondPassCandidate>
buildRoiSecondPassCandidates(const std::vector<ObjectDetector::Detection>& accepted,
                             const std::vector<ObjectDetector::Detection>& rejected,
                             const ObjectDetector::Config& cfg,
                             int srcW,
                             int srcH) {
    std::vector<ObjectDetector::Detection> sources;
    std::vector<int> isWeak;

    for (const auto& d : accepted) {
        if (!d.valid || d.weak || !isSingleClassId(d.classId)) continue;
        sources.push_back(d);
        isWeak.push_back(0);
    }

    if (cfg.roiSecondPassUseWeakOverlap) {
        for (const auto& d : rejected) {
            if (d.confidence < cfg.roiSecondPassMinWeakConfidence) continue;
            if (!isSingleClassId(d.classId) && !isBunchClassId(d.classId)) continue;
            sources.push_back(d);
            isWeak.push_back(1);
        }
    }

    if (sources.empty()) {
        return {};
    }

    const int n = static_cast<int>(sources.size());
    std::vector<std::vector<int>> adj(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (areRoiGroupNeighbors(sources[static_cast<size_t>(i)],
                                     sources[static_cast<size_t>(j)], cfg)) {
                adj[static_cast<size_t>(i)].push_back(j);
                adj[static_cast<size_t>(j)].push_back(i);
            }
        }
    }

    std::vector<int> visited(static_cast<size_t>(n), 0);
    std::vector<RoiSecondPassCandidate> candidates;
    const float maxArea = static_cast<float>(srcW * srcH) * cfg.roiSecondPassMaxImageFraction;

    for (int start = 0; start < n; ++start) {
        if (visited[static_cast<size_t>(start)]) continue;

        std::vector<int> stack{start};
        std::vector<int> comp;
        visited[static_cast<size_t>(start)] = 1;

        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            comp.push_back(cur);

            for (int nxt : adj[static_cast<size_t>(cur)]) {
                if (visited[static_cast<size_t>(nxt)]) continue;
                visited[static_cast<size_t>(nxt)] = 1;
                stack.push_back(nxt);
            }
        }

        int acceptedCount = 0;
        int weakCount = 0;
        float score = 0.0f;
        for (int idx : comp) {
            if (isWeak[static_cast<size_t>(idx)]) ++weakCount;
            else ++acceptedCount;
            score += sources[static_cast<size_t>(idx)].confidence;
        }

        const bool acceptedGroupOk = acceptedCount >= cfg.roiSecondPassMinSingles;
        const bool weakGroupOk = cfg.roiSecondPassUseWeakOverlap && weakCount >= cfg.roiSecondPassMinWeakGroup;
        if (!acceptedGroupOk && !weakGroupOk) continue;

        cv::Rect roi = expandedRectFromDetections(sources, comp, cfg, srcW, srcH);
        if (roi.empty()) continue;
        if (static_cast<float>(roi.area()) > maxArea) continue;
        if (roi.width < 40 || roi.height < 40) continue;

        RoiSecondPassCandidate cand;
        cand.rect = roi;
        cand.acceptedCount = acceptedCount;
        cand.weakCount = weakCount;
        cand.groupSize = static_cast<int>(comp.size());
        cand.score = score / std::max(1, cand.groupSize);
        cand.reason = weakGroupOk ? "weak_overlap_group" : "accepted_singles_group";
        candidates.push_back(std::move(cand));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  if (a.weakCount != b.weakCount) return a.weakCount > b.weakCount;
                  if (a.acceptedCount != b.acceptedCount) return a.acceptedCount > b.acceptedCount;
                  return a.score > b.score;
              });

    if (cfg.roiSecondPassMaxCrops >= 0 &&
        candidates.size() > static_cast<size_t>(cfg.roiSecondPassMaxCrops)) {
        candidates.resize(static_cast<size_t>(cfg.roiSecondPassMaxCrops));
    }

    return candidates;
}

std::vector<ObjectDetector::Detection>
ObjectDetector::runRoiSecondPassInternal(const cv::Mat& bgr,
                                         const std::vector<ObjectDetector::Detection>& acceptedFull,
                                         const std::vector<ObjectDetector::Detection>& rejectedFull,
                                         int frameCounter) {
    std::vector<ObjectDetector::Detection> roiDetections;
    const auto& cfg = cfg_;

    if (!cfg.useRoiSecondPass) return roiDetections;
    if (cfg.roiSecondPassEveryNFrames > 1 &&
        (frameCounter % cfg.roiSecondPassEveryNFrames) != 0) {
        return roiDetections;
    }

    bool hasAcceptedBunch = false;
    for (const auto& d : acceptedFull) {
        if (d.valid && !d.weak && isBunchClassId(d.classId)) {
            hasAcceptedBunch = true;
            break;
        }
    }
    if (cfg.roiSecondPassOnlyIfNoAcceptedBunch && hasAcceptedBunch) {
        return roiDetections;
    }

    const auto candidates = buildRoiSecondPassCandidates(acceptedFull, rejectedFull, cfg, bgr.cols, bgr.rows);
    if (candidates.empty()) return roiDetections;

    for (const auto& cand : candidates) {
        if (cand.rect.empty()) continue;

        cv::Mat crop = bgr(cand.rect).clone();
        if (crop.empty()) continue;

        std::vector<ObjectDetector::Detection> cropRaw;
        std::string err;
        if (!inferDetectionsOnBgrInternal(crop, cropRaw, err)) {
            std::cerr << "[detector] ROI second pass failed: " << err << "\n";
            continue;
        }

        for (auto det : cropRaw) {
            if (!isBunchClassId(det.classId)) continue;
            if (det.confidence < cfg.roiSecondPassMinBunchConfidence) continue;

            det.x += static_cast<float>(cand.rect.x);
            det.y += static_cast<float>(cand.rect.y);
            det.roiPass = true;
            det.roiGroupSize = cand.groupSize;
            det.roiSourceAcceptedCount = cand.acceptedCount;
            det.roiSourceWeakCount = cand.weakCount;
            det.roiPaddingRatio = cfg.roiSecondPassPadding;
            det.roiReason = cand.reason;
            det.boxArea = det.w * det.h;
            det.valid = true;
            det.currentConfidence = det.confidence;
            det.bestConfidence = det.confidence;

            roiDetections.push_back(std::move(det));
        }
    }

    return roiDetections;
}

static std::string joinRejectReasons(const std::vector<std::string>& reasons) {
    if (reasons.empty()) return std::string{};
    std::ostringstream reason;
    reason << "weak/noise";
    for (const auto& r : reasons) reason << " " << r;
    return reason.str();
}

struct BunchSupportInfo {
    int memberCount = 0;
    int ripeCount = 0;
    int unripeCount = 0;
    float confSum = 0.0f;
    float ripeConfSum = 0.0f;
    float unripeConfSum = 0.0f;
    std::vector<ObjectDetector::Detection> ripeMembers;
    std::vector<ObjectDetector::Detection> unripeMembers;
};

static BunchSupportInfo computeBunchSupport(const std::vector<ObjectDetector::Detection>& singles,
                                            const ObjectDetector::Detection& bunch,
                                            float minMemberConf) {
    BunchSupportInfo info;
    const float x1 = bunch.x;
    const float y1 = bunch.y;
    const float x2 = bunch.x + bunch.w;
    const float y2 = bunch.y + bunch.h;

    for (const auto& s : singles) {
        if (!s.valid || s.weak || !isSingleClassId(s.classId)) continue;
        if (s.confidence < minMemberConf) continue;

        const float cx = s.x + s.w * 0.5f;
        const float cy = s.y + s.h * 0.5f;
        if (cx < x1 || cx > x2 || cy < y1 || cy > y2) continue;

        info.memberCount += 1;
        info.confSum += s.confidence;
        if (s.classId == 1) {
            info.ripeCount += 1;
            info.ripeConfSum += s.confidence;
            info.ripeMembers.push_back(s);
        } else if (s.classId == 2) {
            info.unripeCount += 1;
            info.unripeConfSum += s.confidence;
            info.unripeMembers.push_back(s);
        }
    }

    return info;
}

static void attachBunchSupportMetadata(ObjectDetector::Detection& d,
                                       const BunchSupportInfo& support,
                                       const std::string& source) {
    d.supportMemberCount = support.memberCount;
    d.supportRipeCount = support.ripeCount;
    d.supportUnripeCount = support.unripeCount;
    d.supportConfSum = support.confSum;
    d.supportRipeConfSum = support.ripeConfSum;
    d.supportUnripeConfSum = support.unripeConfSum;
    d.bunchSupportScore = support.memberCount > 0
        ? support.confSum / static_cast<float>(support.memberCount)
        : 0.0f;
    if (!source.empty()) d.displaySource = source;
}

static void rememberOriginalBbox(ObjectDetector::Detection& d) {
    if (d.hasOriginalBbox) return;
    d.hasOriginalBbox = true;
    d.originalX = d.x;
    d.originalY = d.y;
    d.originalW = d.w;
    d.originalH = d.h;
}

static bool refineBunchBoxFromMembers(ObjectDetector::Detection& bunch,
                                      const std::vector<ObjectDetector::Detection>& members,
                                      const ObjectDetector::Config& cfg,
                                      int srcW,
                                      int srcH) {
    if (!cfg.refineBunchBboxFromMembers || members.empty() || srcW <= 0 || srcH <= 0) {
        return false;
    }

    float x1 = 1e9f;
    float y1 = 1e9f;
    float x2 = -1e9f;
    float y2 = -1e9f;
    for (const auto& m : members) {
        x1 = std::min(x1, m.x);
        y1 = std::min(y1, m.y);
        x2 = std::max(x2, m.x + m.w);
        y2 = std::max(y2, m.y + m.h);
    }

    if (x2 <= x1 + 1.0f || y2 <= y1 + 1.0f) return false;

    const float padX = (x2 - x1) * std::max(0.0f, cfg.refinedBunchPaddingRatio);
    const float padY = (y2 - y1) * std::max(0.0f, cfg.refinedBunchPaddingRatio);
    x1 = std::clamp(x1 - padX, 0.0f, static_cast<float>(srcW - 1));
    y1 = std::clamp(y1 - padY, 0.0f, static_cast<float>(srcH - 1));
    x2 = std::clamp(x2 + padX, 0.0f, static_cast<float>(srcW));
    y2 = std::clamp(y2 + padY, 0.0f, static_cast<float>(srcH));

    if (x2 <= x1 + 1.0f || y2 <= y1 + 1.0f) return false;

    rememberOriginalBbox(bunch);
    bunch.refinedX = x1;
    bunch.refinedY = y1;
    bunch.refinedW = x2 - x1;
    bunch.refinedH = y2 - y1;
    bunch.hasRefinedBbox = true;

    // Use the refined box for GUI/video/tracking. The original model box is still exported in JSON.
    bunch.x = bunch.refinedX;
    bunch.y = bunch.refinedY;
    bunch.w = bunch.refinedW;
    bunch.h = bunch.refinedH;
    bunch.boxArea = bunch.w * bunch.h;
    return true;
}

static void relabelDetection(ObjectDetector::Detection& d,
                             int newClassId,
                             const std::unordered_map<int, std::string>& classNames) {
    if (!d.classCorrected) {
        d.originalClassId = d.classId;
        d.originalLabel = d.label;
    }
    d.classId = newClassId;
    auto it = classNames.find(newClassId);
    d.label = (it != classNames.end()) ? it->second : std::to_string(newClassId);
    d.classCorrected = true;
}


static float maturityScoreFor(const ObjectDetector::Detection& d,
                              int asClassId,
                              const ObjectDetector::Config& cfg) {
    const float conf = d.confidence;
    const float warm = tomatoWarmRatio(d);
    const float greenYellow = d.greenYellowRatio;
    if (asClassId == 1) {
        return conf + cfg.maturityRipeWarmWeight * warm + 0.03f * d.maskDensity;
    }
    if (asClassId == 2) {
        return conf + cfg.maturityUnripeGreenWeight * greenYellow - 0.04f * warm + 0.03f * d.maskDensity;
    }
    if (asClassId == 0) {
        return conf + cfg.maturityRipeWarmWeight * warm +
               cfg.maturityBunchSupportWeight * d.supportRipeConfSum +
               0.035f * static_cast<float>(d.supportRipeCount) -
               0.025f * d.supportUnripeConfSum;
    }
    if (asClassId == 3) {
        return conf + cfg.maturityUnripeGreenWeight * greenYellow +
               cfg.maturityBunchSupportWeight * d.supportUnripeConfSum +
               0.035f * static_cast<float>(d.supportUnripeCount) -
               0.025f * d.supportRipeConfSum;
    }
    return conf;
}

static void stampMaturityScores(ObjectDetector::Detection& d,
                                const ObjectDetector::Config& cfg) {
    if (isSingleClassId(d.classId)) {
        d.maturityScoreRipe = maturityScoreFor(d, 1, cfg);
        d.maturityScoreUnripe = maturityScoreFor(d, 2, cfg);
        d.maturityScore = (d.classId == 1) ? d.maturityScoreRipe : d.maturityScoreUnripe;
    } else if (isBunchClassId(d.classId)) {
        d.maturityScoreRipe = maturityScoreFor(d, 0, cfg);
        d.maturityScoreUnripe = maturityScoreFor(d, 3, cfg);
        d.maturityScore = (d.classId == 0) ? d.maturityScoreRipe : d.maturityScoreUnripe;
    } else {
        d.maturityScore = d.confidence;
    }
}

static bool failsTomatoSanityGate(const ObjectDetector::Detection& d,
                                  const ObjectDetector::Config& cfg) {
    if (!cfg.useTomatoSanityGate || !isSingleClassId(d.classId)) return false;
    if (d.maskWidth <= 0 || d.maskHeight <= 0) return false;

    const float warm = tomatoWarmRatio(d);
    const float anyTomatoColor = std::max(warm, d.greenYellowRatio);

    if (d.classId == 1) {
        // Reject strong-looking false positives that have almost no red/orange tomato color.
        if (warm < cfg.ripeMinWarmRatio && anyTomatoColor < cfg.singleMinAnyTomatoColorRatio) {
            return true;
        }
        if (d.confidence >= cfg.highConfidenceSanityThreshold && warm < cfg.ripeMinWarmRatio) {
            return true;
        }
    } else if (d.classId == 2) {
        if (d.greenYellowRatio < cfg.unripeMinGreenYellowRatio && anyTomatoColor < cfg.singleMinAnyTomatoColorRatio) {
            return true;
        }
    }
    return false;
}

static float competitionOverlapFor(const ObjectDetector::Detection& a,
                                   const ObjectDetector::Detection& b) {
    if (!sameMaturityCompetitionGroup(a.classId, b.classId)) return 0.0f;
    return iouXYWH(a, b);
}

static bool shouldCompeteMaturity(const ObjectDetector::Detection& a,
                                  const ObjectDetector::Detection& b,
                                  const ObjectDetector::Config& cfg) {
    if (!isMaturityCompetingPair(a.classId, b.classId)) return false;
    const float iou = competitionOverlapFor(a, b);
    if (isSingleClassId(a.classId) && isSingleClassId(b.classId)) {
        return iou >= cfg.maturityCompetitionIouSingles;
    }
    if (isBunchClassId(a.classId) && isBunchClassId(b.classId)) {
        // Bunch boxes can be refined or model-sized, so use a lower threshold.
        return iou >= cfg.maturityCompetitionIouBunches;
    }
    return false;
}

static void applyMaturityCompetition(std::vector<ObjectDetector::Detection>& accepted,
                                     std::vector<ObjectDetector::Detection>& rejected,
                                     const ObjectDetector::Config& cfg) {
    if (!cfg.useMaturityCompetition || accepted.size() < 2) {
        for (auto& d : accepted) stampMaturityScores(d, cfg);
        return;
    }

    std::vector<ObjectDetector::Detection> winners;
    winners.reserve(accepted.size());

    auto markLoser = [&](ObjectDetector::Detection loser,
                         const ObjectDetector::Detection& winner,
                         float loserScore,
                         float winnerScore) {
        loser.valid = false;
        loser.weak = true;
        loser.lostMaturityCompetition = true;
        loser.displaySuppressed = cfg.hideMaturityCompetitionLosers;
        loser.maturityCompetitionWinner = false;
        loser.maturityScore = loserScore;
        loser.rejectReason = "lost_maturity_competition winner=" + winner.label;
        std::ostringstream oss;
        oss << "loser_score=" << std::fixed << std::setprecision(2) << loserScore
            << " winner_score=" << winnerScore;
        loser.maturityCompetitionReason = oss.str();
        rejected.push_back(std::move(loser));
    };

    std::sort(accepted.begin(), accepted.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });

    for (auto cand : accepted) {
        stampMaturityScores(cand, cfg);
        bool inserted = false;
        for (auto& win : winners) {
            if (!shouldCompeteMaturity(cand, win, cfg)) continue;
            stampMaturityScores(win, cfg);
            const bool candClearlyWins = cand.maturityScore > win.maturityScore + cfg.maturityScoreMargin;
            const bool winClearlyWins = win.maturityScore >= cand.maturityScore + cfg.maturityScoreMargin;

            if (candClearlyWins || (!winClearlyWins && cand.confidence > win.confidence)) {
                auto oldWinner = win;
                const float oldWinnerScore = oldWinner.maturityScore;
                const float candScore = cand.maturityScore;
                cand.maturityCompetitionWinner = true;
                cand.maturityCompetitionReason = "won_maturity_competition";
                win = cand;
                markLoser(std::move(oldWinner), win, oldWinnerScore, candScore);
            } else {
                win.maturityCompetitionWinner = true;
                win.maturityCompetitionReason = "won_maturity_competition";
                const float candScore = cand.maturityScore;
                const float winScore = win.maturityScore;
                markLoser(std::move(cand), win, candScore, winScore);
            }
            inserted = true;
            break;
        }
        if (!inserted) {
            cand.maturityCompetitionWinner = true;
            winners.push_back(std::move(cand));
        }
    }

    accepted = std::move(winners);
}

static void splitAcceptedRejected(const std::vector<ObjectDetector::Detection>& raw,
                                  const ObjectDetector::Config& cfg,
                                  int srcW,
                                  int srcH,
                                  std::vector<ObjectDetector::Detection>& accepted,
                                  std::vector<ObjectDetector::Detection>& rejected) {
    accepted.clear();
    rejected.clear();

    struct BunchCandidate {
        ObjectDetector::Detection det;
        std::vector<std::string> baseReasons;
    };

    std::vector<ObjectDetector::Detection> prelimSingles;
    std::vector<BunchCandidate> bunchCandidates;

    for (auto det : raw) {
        std::vector<std::string> reasons;
        const float neededConf = classThresholdFor(cfg, det.classId);

        if (det.confidence < neededConf) {
            std::ostringstream oss;
            oss << "conf " << std::fixed << std::setprecision(2) << det.confidence
                << "<" << neededConf;
            reasons.push_back(oss.str());
        }

        if (!insideRoiFilter(det, srcW, srcH, cfg)) {
            reasons.push_back("roi");
        }

        if (isBunchClassId(det.classId)) {
            if (det.maskWidth > 0 && det.maskHeight > 0) {
                if (det.maskArea < cfg.minMaskAreaBunch) reasons.push_back("small_bunch_mask");
                if (det.maskDensity < cfg.minMaskDensityBunch) reasons.push_back("low_bunch_density");
            }
            if (det.boxArea < cfg.minBoxAreaBunch) reasons.push_back("small_bunch_box");
        } else {
            if (det.maskWidth > 0 && det.maskHeight > 0) {
                if (det.maskArea < cfg.minMaskAreaSingle) reasons.push_back("small_single_mask");
                if (det.maskDensity < cfg.minMaskDensitySingle) reasons.push_back("low_single_density");
            } else if (det.boxArea < cfg.minBoxAreaSingle) {
                reasons.push_back("small_single_box");
            }
        }

        if (cfg.useColorFilter && det.maskWidth > 0 && det.maskHeight > 0) {
            std::ostringstream oss;
            if (isRipeClassId(det.classId) && det.redRatio < cfg.ripeMinRedRatio &&
                (!isSingleClassId(det.classId) || tomatoWarmRatio(det) < cfg.ripeMinWarmRatio)) {
                oss << "bad_color red=" << std::fixed << std::setprecision(2) << det.redRatio
                    << " warm=" << tomatoWarmRatio(det);
                reasons.push_back(oss.str());
            } else if (isUnripeClassId(det.classId) &&
                       det.greenYellowRatio < cfg.unripeMinGreenYellowRatio) {
                oss << "bad_color gy=" << std::fixed << std::setprecision(2) << det.greenYellowRatio;
                reasons.push_back(oss.str());
            }
        }

        stampMaturityScores(det, cfg);
        if (failsTomatoSanityGate(det, cfg)) {
            std::ostringstream oss;
            oss << "tomato_sanity_failed warm=" << std::fixed << std::setprecision(2) << tomatoWarmRatio(det)
                << " red=" << det.redRatio
                << " gy=" << det.greenYellowRatio;
            reasons.push_back(oss.str());
        }

        if (isBunchClassId(det.classId)) {
            bunchCandidates.push_back(BunchCandidate{det, reasons});
            continue;
        }

        if (reasons.empty()) {
            det.weak = false;
            det.rejectReason.clear();
            if (isSingleClassId(det.classId)) prelimSingles.push_back(det);
            else accepted.push_back(det);
        } else {
            det.weak = true;
            det.rejectReason = joinRejectReasons(reasons);
            rejected.push_back(det);
        }
    }

    float maxSingleBoxArea = 0.0f;
    for (const auto& s : prelimSingles) {
        maxSingleBoxArea = std::max(maxSingleBoxArea, s.boxArea);
    }

    std::vector<ObjectDetector::Detection> finalBunches;
    for (auto cand : bunchCandidates) {
        auto b = cand.det;
        if (b.originalClassId < 0) {
            b.originalClassId = b.classId;
            b.originalLabel = b.label;
        }

        auto support = computeBunchSupport(prelimSingles, b, cfg.bunchPromotionMemberMinConfidence);
        attachBunchSupportMetadata(b, support, "model");

        const bool ripeSupportStrong =
            support.ripeCount >= cfg.eripeBunchPromotionMinRipeSingles &&
            support.ripeConfSum >= cfg.bunchPromotionMinConfSum;
        const bool unripeSupportStrong =
            support.unripeCount >= cfg.unripeBunchPromotionMinUnripeSingles &&
            support.unripeConfSum >= std::max(1.20f, cfg.bunchPromotionMinConfSum * 0.65f);

        bool correctedUnripeToRipe = false;
        if (cfg.correctUnripeBunchToRipeBunch && b.classId == 3 &&
            b.confidence >= cfg.bunchPromotionMinConfidence &&
            support.ripeCount >= cfg.unripeToRipeCorrectionMinRipeSingles &&
            support.ripeConfSum >= cfg.unripeToRipeCorrectionMinRipeConfSum &&
            support.ripeConfSum >= support.unripeConfSum + cfg.unripeToRipeCorrectionMargin) {
            relabelDetection(b, 0, cfg.classNames);
            correctedUnripeToRipe = true;
            b.promotionReason = "class_corrected_unripe_bunch_to_eripe_bunch_supported_by_ripe_singles";
        }

        bool supportedPromotion = false;
        if (cfg.useBunchSupportPromotion && b.confidence >= cfg.bunchPromotionMinConfidence) {
            if (b.classId == 0 && ripeSupportStrong) {
                supportedPromotion = true;
                if (b.promotionReason.empty()) {
                    b.promotionReason = "supported_by_ripe_singles";
                }
            } else if (b.classId == 3 && unripeSupportStrong) {
                supportedPromotion = true;
                if (b.promotionReason.empty()) {
                    b.promotionReason = "supported_by_unripe_singles";
                }
            }
        }
        if (correctedUnripeToRipe) {
            supportedPromotion = true;
        }

        std::vector<std::string> reasons = cand.baseReasons;
        if (cfg.useBunchConsistencyRules && !supportedPromotion) {
            if (maxSingleBoxArea > 0.0f &&
                b.boxArea < maxSingleBoxArea * cfg.bunchMinRelativeToMaxSingleBox) {
                reasons.push_back("bunch_not_larger");
            }

            const int inside = tomatoCentersInsideBox(prelimSingles, b);
            if (inside < cfg.bunchMinTomatoesInside) {
                std::ostringstream oss;
                oss << "tomatoes_inside " << inside << "<" << cfg.bunchMinTomatoesInside;
                reasons.push_back(oss.str());
            }
        }

        if (supportedPromotion) {
            b.clusterPromoted = true;
            b.weak = false;
            b.valid = true;
            b.rejectReason.clear();
            b.displaySource = "model_supported_by_singles";

            const auto& members = (b.classId == 0 && !support.ripeMembers.empty())
                ? support.ripeMembers
                : ((b.classId == 3 && !support.unripeMembers.empty()) ? support.unripeMembers : prelimSingles);
            refineBunchBoxFromMembers(b, members, cfg, srcW, srcH);
            finalBunches.push_back(b);
            continue;
        }

        if (reasons.empty()) {
            b.weak = false;
            b.rejectReason.clear();
            if (b.displaySource.empty()) b.displaySource = "model_rules";
            finalBunches.push_back(b);
        } else {
            b.weak = true;
            b.rejectReason = joinRejectReasons(reasons);
            if (!b.promotionReason.empty()) {
                b.rejectReason += " promotion_failed=" + b.promotionReason;
            }
            rejected.push_back(b);
        }
    }

    accepted.reserve(prelimSingles.size() + finalBunches.size());
    accepted.insert(accepted.end(), prelimSingles.begin(), prelimSingles.end());
    accepted.insert(accepted.end(), finalBunches.begin(), finalBunches.end());

    std::sort(accepted.begin(), accepted.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });

    if (cfg.maxDetectionsPerFrame > 0 &&
        accepted.size() > static_cast<size_t>(cfg.maxDetectionsPerFrame)) {
        accepted.resize(static_cast<size_t>(cfg.maxDetectionsPerFrame));
    }

    std::sort(rejected.begin(), rejected.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });

    if (cfg.maxWeakToKeep >= 0 && rejected.size() > static_cast<size_t>(cfg.maxWeakToKeep)) {
        rejected.resize(static_cast<size_t>(cfg.maxWeakToKeep));
    }
}



static bool textContains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static bool isV32NoiseLike(const ObjectDetector::Detection& d,
                           const ObjectDetector::Config& cfg) {
    if (d.confidence <= cfg.v32NoiseMaxConfidence) return true;
    if (textContains(d.rejectReason, "tomato_sanity_failed")) return true;
    if (textContains(d.rejectReason, "small_single_mask") ||
        textContains(d.rejectReason, "low_single_density")) return true;
    return false;
}

static void markV32DefaultStatuses(std::vector<ObjectDetector::Detection>& accepted,
                                   std::vector<ObjectDetector::Detection>& rejected,
                                   const ObjectDetector::Config& cfg) {
    for (auto& d : accepted) {
        if (d.sourceType.empty()) d.sourceType = "model";
        if (d.policyStatus.empty()) d.policyStatus = "strong";
    }
    for (auto& d : rejected) {
        if (d.sourceType.empty()) d.sourceType = "model";
        d.policyStatus = isV32NoiseLike(d, cfg) ? "noise" : "weak";
    }
}

static bool maybeV32ColorCorrectSingle(ObjectDetector::Detection& d,
                                       const ObjectDetector::Config& cfg) {
    if (!cfg.v32UseColorClassCorrection) return false;
    if (!isSingleClassId(d.classId)) return false;
    if (d.confidence < cfg.v32ColorCorrectionMinConf) return false;
    if (d.maskWidth <= 0 || d.maskHeight <= 0) return false;
    if (d.maskDensity < cfg.v32ColorCorrectionMinMaskDensity) return false;

    const float warm = tomatoWarmRatio(d);
    if (d.classId == 2 &&
        warm >= cfg.v32UnripeToRipeMinWarm &&
        d.greenYellowRatio <= cfg.v32UnripeToRipeMaxGreenYellow) {
        relabelDetection(d, 1, cfg.classNames);
        d.colorCorrectedByPolicy = true;
        d.correctionReason = "unripe_to_ripe_by_mask_color";
        d.colorCorrectionScore = warm - d.greenYellowRatio;
        d.policyStatus = "color_corrected";
        d.displaySource = "v32_color_correction";
        d.promotionReason = d.correctionReason;
        return true;
    }

    if (d.classId == 1 &&
        d.greenYellowRatio >= cfg.v32RipeToUnripeMinGreenYellow &&
        d.redRatio <= cfg.v32RipeToUnripeMaxRed &&
        warm <= cfg.v32RipeToUnripeMaxWarm) {
        relabelDetection(d, 2, cfg.classNames);
        d.colorCorrectedByPolicy = true;
        d.correctionReason = "ripe_to_unripe_by_mask_color";
        d.colorCorrectionScore = d.greenYellowRatio - warm;
        d.policyStatus = "color_corrected";
        d.displaySource = "v32_color_correction";
        d.promotionReason = d.correctionReason;
        return true;
    }

    return false;
}

static bool v32IsRegularReviewCandidate(const ObjectDetector::Detection& d,
                                         const ObjectDetector::Config& cfg,
                                         std::string& reasonOut) {
    if (!cfg.v32PublishReviewAsAccepted) return false;
    if (!isSingleClassId(d.classId)) return false;
    if (d.policyStatus == "noise") return false;
    if (textContains(d.rejectReason, "roi") ||
        textContains(d.rejectReason, "tomato_sanity_failed") ||
        textContains(d.rejectReason, "bad_color")) return false;
    if (d.maskWidth <= 0 || d.maskHeight <= 0) return false;
    if (d.maskDensity < cfg.v32ReviewMinMaskDensity) return false;
    if (d.maskArea < cfg.v32ReviewMinMaskArea) return false;

    if (d.classId == 2 &&
        d.confidence >= cfg.v32ReviewUnripeMinConf &&
        d.greenYellowRatio >= cfg.v32ReviewUnripeMinGreenYellow) {
        reasonOut = "near_threshold_unripe_color_mask_support";
        return true;
    }

    if (d.classId == 1 &&
        d.confidence >= cfg.v32ReviewRipeMinConf &&
        (d.redRatio >= cfg.v32ReviewRipeMinRed || tomatoWarmRatio(d) >= cfg.v32ReviewRipeMinWarm)) {
        reasonOut = "near_threshold_ripe_color_mask_support";
        return true;
    }

    return false;
}

static bool v32CenterInsideExpandedBox(const ObjectDetector::Detection& child,
                                       const ObjectDetector::Detection& anchor,
                                       float padRatio) {
    const float padX = anchor.w * padRatio;
    const float padY = anchor.h * padRatio;
    const float x1 = anchor.x - padX;
    const float y1 = anchor.y - padY;
    const float x2 = anchor.x + anchor.w + padX;
    const float y2 = anchor.y + anchor.h + padY;
    const float cx = child.x + child.w * 0.5f;
    const float cy = child.y + child.h * 0.5f;
    return cx >= x1 && cx <= x2 && cy >= y1 && cy <= y2;
}

static float v32ChildWeight(const ObjectDetector::Detection& d) {
    if (!isSingleClassId(d.classId)) return 0.0f;
    if (!d.weak && d.valid) return 1.0f;
    if (d.classId == 2 && d.confidence >= 0.80f) return 0.70f;
    if (d.classId == 1 && d.confidence >= 0.75f) return 0.70f;
    if (d.confidence >= 0.50f) return 0.50f;
    return 0.0f;
}

static bool v32GoodHeuristicChild(const ObjectDetector::Detection& d,
                                  const ObjectDetector::Config& cfg) {
    if (!isSingleClassId(d.classId)) return false;
    if (d.policyStatus == "noise") return false;
    if (d.confidence < cfg.v32HeuristicChildMinConf && d.weak) return false;
    if (textContains(d.rejectReason, "roi") || textContains(d.rejectReason, "tomato_sanity_failed")) return false;
    if (d.maskWidth > 0 && d.maskHeight > 0 && d.maskDensity < cfg.v32HeuristicChildMinMaskDensity) return false;
    return v32ChildWeight(d) > 0.0f;
}

static ObjectDetector::Detection v32MakeHeuristicBunch(const ObjectDetector::Detection& anchor,
                                                       const std::vector<ObjectDetector::Detection>& children,
                                                       const std::vector<float>& weights,
                                                       float ripeScore,
                                                       float unripeScore,
                                                       float weightedCount,
                                                       const ObjectDetector::Config& cfg,
                                                       int srcW,
                                                       int srcH) {
    ObjectDetector::Detection h;
    h.valid = true;
    h.weak = false;
    h.heuristic = true;
    h.sourceType = "heuristic";
    h.policyStatus = "heuristic";
    h.heuristicType = "weak_bunch_anchor_with_child_support_v32";
    h.displaySource = "v32_weak_bunch_anchor_heuristic";
    h.promotionReason = h.heuristicType;
    h.confidence = 0.0f;  // not model confidence; anchor confidence is exported separately.
    h.currentConfidence = 0.0f;
    h.bestConfidence = 0.0f;
    h.anchorBunchClass = anchor.label;
    h.anchorBunchConfidence = anchor.confidence;
    h.childCount = static_cast<int>(children.size());
    h.weightedChildCount = weightedCount;
    h.ripeEvidenceScore = ripeScore;
    h.unripeEvidenceScore = unripeScore;
    h.heuristicScore = anchor.confidence + weightedCount + std::max(ripeScore, unripeScore);
    h.weakChildCount = 0;
    h.strongChildCount = 0;
    for (const auto& c : children) {
        if (c.weak) ++h.weakChildCount;
        else ++h.strongChildCount;
    }

    const float margin = cfg.v32HeuristicMaturityMargin;
    if (ripeScore >= unripeScore + margin) {
        h.classId = 0;
        h.label = "HEURISTIC ripe bunch";
        h.dominantMaturity = "ripe";
    } else if (unripeScore >= ripeScore + margin) {
        h.classId = 3;
        h.label = "HEURISTIC unripe bunch";
        h.dominantMaturity = "unripe";
    } else {
        h.classId = 4;
        h.label = "HEURISTIC mixed bunch";
        h.dominantMaturity = "mixed";
    }

    float x1 = anchor.x;
    float y1 = anchor.y;
    float x2 = anchor.x + anchor.w;
    float y2 = anchor.y + anchor.h;
    for (const auto& c : children) {
        x1 = std::min(x1, c.x);
        y1 = std::min(y1, c.y);
        x2 = std::max(x2, c.x + c.w);
        y2 = std::max(y2, c.y + c.h);
    }

    const float pad = cfg.v32HeuristicUnionPaddingRatio * std::max(x2 - x1, y2 - y1);
    x1 = std::max(0.0f, x1 - pad);
    y1 = std::max(0.0f, y1 - pad);
    x2 = std::min(static_cast<float>(std::max(1, srcW)), x2 + pad);
    y2 = std::min(static_cast<float>(std::max(1, srcH)), y2 + pad);

    h.x = x1;
    h.y = y1;
    h.w = std::max(1.0f, x2 - x1);
    h.h = std::max(1.0f, y2 - y1);
    h.boxArea = h.w * h.h;
    h.maskArea = 0.0f;
    h.maskDensity = 0.0f;
    return h;
}

static bool v32GoodHeuristicAnchor(const ObjectDetector::Detection& d,
                                   const ObjectDetector::Config& cfg,
                                   int srcW,
                                   int srcH) {
    if (!isBunchClassId(d.classId)) return false;
    if (d.confidence < cfg.v32HeuristicAnchorMinConf) return false;
    if (textContains(d.rejectReason, "roi")) return false;
    if (textContains(d.rejectReason, "tomato_sanity_failed")) return false;

    const float frameArea = static_cast<float>(std::max(1, srcW) * std::max(1, srcH));
    if (cfg.v32HeuristicMaxBoxAreaFraction > 0.0f && frameArea > 0.0f) {
        const float frac = (d.w * d.h) / frameArea;
        if (frac > cfg.v32HeuristicMaxBoxAreaFraction) return false;
    }

    // A weak bunch anchor may have imperfect segmentation, but it should not be almost empty.
    if (d.maskWidth > 0 && d.maskHeight > 0) {
        if (d.maskDensity < cfg.v32HeuristicMinAnchorMaskDensity) return false;
        if (d.maskArea < cfg.v32HeuristicMinAnchorMaskArea) return false;
    }
    return true;
}

static void v32LimitHeuristics(std::vector<ObjectDetector::Detection>& heuristicOverlays,
                               const ObjectDetector::Config& cfg) {
    if (cfg.v32HeuristicMaxPerFrame <= 0) return;
    std::sort(heuristicOverlays.begin(), heuristicOverlays.end(),
              [](const auto& a, const auto& b) { return a.heuristicScore > b.heuristicScore; });
    if (heuristicOverlays.size() > static_cast<size_t>(cfg.v32HeuristicMaxPerFrame)) {
        heuristicOverlays.resize(static_cast<size_t>(cfg.v32HeuristicMaxPerFrame));
    }
}

static void v32BuildWeakBunchHeuristics(const std::vector<ObjectDetector::Detection>& accepted,
                                        const std::vector<ObjectDetector::Detection>& rejected,
                                        std::vector<ObjectDetector::Detection>& heuristicOverlays,
                                        const ObjectDetector::Config& cfg,
                                        int srcW,
                                        int srcH) {
    if (!cfg.v32UseWeakBunchAnchorHeuristic) return;

    std::vector<ObjectDetector::Detection> children;
    for (const auto& d : accepted) {
        if (v32GoodHeuristicChild(d, cfg)) children.push_back(d);
    }
    for (const auto& d : rejected) {
        if (v32GoodHeuristicChild(d, cfg)) children.push_back(d);
    }
    if (children.empty()) return;

    std::vector<ObjectDetector::Detection> anchors;
    for (const auto& d : rejected) {
        if (!v32GoodHeuristicAnchor(d, cfg, srcW, srcH)) continue;
        anchors.push_back(d);
    }
    std::sort(anchors.begin(), anchors.end(), [](const auto& a, const auto& b) {
        return a.confidence > b.confidence;
    });

    for (const auto& a : anchors) {
        bool overlapsAcceptedBunch = false;
        for (const auto& b : accepted) {
            if (!b.valid || b.weak || !isBunchClassId(b.classId)) continue;
            if (iouXYWH(a, b) >= cfg.v32HeuristicDuplicateIou) {
                overlapsAcceptedBunch = true;
                break;
            }
        }
        if (overlapsAcceptedBunch) continue;

        std::vector<ObjectDetector::Detection> supportChildren;
        std::vector<float> weights;
        float weighted = 0.0f;
        float ripeScore = 0.0f;
        float unripeScore = 0.0f;

        for (const auto& c : children) {
            if (!v32CenterInsideExpandedBox(c, a, cfg.v32HeuristicAnchorPaddingRatio) && iouXYWH(c, a) < 0.01f) {
                continue;
            }
            const float w = v32ChildWeight(c);
            if (w <= 0.0f) continue;
            supportChildren.push_back(c);
            weights.push_back(w);
            weighted += w;

            const float warm = tomatoWarmRatio(c);
            if (c.classId == 1) ripeScore += w;
            if (c.classId == 2) unripeScore += w;
            if (c.redRatio >= cfg.ripeMinRedRatio || warm >= cfg.ripeMinWarmRatio) ripeScore += 0.50f * w;
            if (c.greenYellowRatio >= cfg.v32ReviewUnripeMinGreenYellow) unripeScore += 0.50f * w;
        }

        const bool strongAnchor = a.confidence >= cfg.v32HeuristicAnchorStrongConf;
        const int minChildren = strongAnchor ? cfg.v32HeuristicMinChildrenStrongAnchor : cfg.v32HeuristicMinChildrenWeakAnchor;
        const float minWeighted = strongAnchor ? cfg.v32HeuristicMinWeightedStrongAnchor : cfg.v32HeuristicMinWeightedWeakAnchor;
        if (static_cast<int>(supportChildren.size()) < minChildren || weighted < minWeighted) {
            continue;
        }

        ObjectDetector::Detection h = v32MakeHeuristicBunch(a, supportChildren, weights,
                                                            ripeScore, unripeScore, weighted,
                                                            cfg, srcW, srcH);
        bool duplicate = false;
        for (const auto& existing : heuristicOverlays) {
            if (iouXYWH(h, existing) >= cfg.v32HeuristicDuplicateIou) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            heuristicOverlays.push_back(std::move(h));
        }
    }
}

static void applyV32ExperimentalRuntimePolicy(std::vector<ObjectDetector::Detection>& accepted,
                                              std::vector<ObjectDetector::Detection>& rejected,
                                              std::vector<ObjectDetector::Detection>& heuristicOverlays,
                                              const ObjectDetector::Config& cfg,
                                              int srcW,
                                              int srcH) {
    if (!cfg.useV32ExperimentalRuntimePolicy) return;

    markV32DefaultStatuses(accepted, rejected, cfg);

    for (auto& d : accepted) {
        if (maybeV32ColorCorrectSingle(d, cfg)) {
            d.weak = false;
            d.valid = true;
            d.rejectReason.clear();
        }
    }

    std::vector<ObjectDetector::Detection> keptRejected;
    keptRejected.reserve(rejected.size());
    for (auto d : rejected) {
        if (maybeV32ColorCorrectSingle(d, cfg)) {
            d.weak = false;
            d.valid = true;
            d.rejectReason.clear();
            accepted.push_back(std::move(d));
            continue;
        }

        std::string reviewReason;
        if (v32IsRegularReviewCandidate(d, cfg, reviewReason)) {
            d.weak = false;
            d.valid = true;
            d.rejectReason.clear();
            d.policyStatus = "review";
            d.reviewCandidate = true;
            d.reviewReason = reviewReason;
            d.displaySource = "v32_review_candidate";
            d.promotionReason = reviewReason;
            accepted.push_back(std::move(d));
            continue;
        }

        keptRejected.push_back(std::move(d));
    }
    rejected = std::move(keptRejected);
    markV32DefaultStatuses(accepted, rejected, cfg);

    v32BuildWeakBunchHeuristics(accepted, rejected, heuristicOverlays, cfg, srcW, srcH);
    v32LimitHeuristics(heuristicOverlays, cfg);
}

static std::vector<ObjectDetector::Detection>
preferBunchesForDisplay(std::vector<ObjectDetector::Detection> detections,
                         const ObjectDetector::Config& cfg) {
    if (!cfg.displayPreferBunchOverSingles) {
        return detections;
    }

    std::vector<ObjectDetector::Detection> out;
    out.reserve(detections.size());
    for (const auto& d : detections) {
        if (!d.valid || d.weak || !isSingleClassId(d.classId)) {
            out.push_back(d);
            continue;
        }

        bool suppressedByBunch = false;
        for (const auto& b : detections) {
            if (!b.valid || b.weak || !isBunchClassId(b.classId)) continue;
            const float cx = d.x + d.w * 0.5f;
            const float cy = d.y + d.h * 0.5f;
            const bool centerInside = cx >= b.x && cx <= b.x + b.w && cy >= b.y && cy <= b.y + b.h;
            const bool overlaps = iouXYWH(d, b) >= cfg.displayBunchSuppressIou;
            if (centerInside || overlaps) {
                suppressedByBunch = true;
                break;
            }
        }

        if (!suppressedByBunch) {
            out.push_back(d);
        }
    }
    return out;
}

static void applyTrackClassLock(FrameLevelTrack& track,
                                ObjectDetector::Detection& det,
                                const ObjectDetector::Config& cfg) {
    if (track.stableClassId < 0) {
        track.stableClassId = det.classId;
        track.stableLabel = det.label;
        track.stableClassScore = det.maturityScore > 0.0f ? det.maturityScore : det.confidence;
        track.pendingClassId = -1;
        track.pendingClassFrames = 0;
        return;
    }

    const float detScore = det.maturityScore > 0.0f ? det.maturityScore : det.confidence;
    if (det.classId == track.stableClassId) {
        track.pendingClassId = -1;
        track.pendingClassFrames = 0;
        track.stableClassScore = std::max(track.stableClassScore * 0.92f, detScore);
        track.stableLabel = det.label;
        return;
    }

    if (!sameMaturityCompetitionGroup(track.stableClassId, det.classId)) {
        track.stableClassId = det.classId;
        track.stableLabel = det.label;
        track.stableClassScore = detScore;
        track.pendingClassId = -1;
        track.pendingClassFrames = 0;
        return;
    }

    if (track.pendingClassId == det.classId) {
        track.pendingClassFrames += 1;
    } else {
        track.pendingClassId = det.classId;
        track.pendingClassFrames = 1;
    }

    const bool strongSwitch = detScore >= track.stableClassScore + cfg.classSwitchMargin;
    const bool persistentSwitch = track.pendingClassFrames >= cfg.classSwitchRequiredFrames;
    if (strongSwitch || persistentSwitch) {
        track.stableClassId = det.classId;
        track.stableLabel = det.label;
        track.stableClassScore = detScore;
        track.pendingClassId = -1;
        track.pendingClassFrames = 0;
        det.classLocked = false;
        det.switchCandidateFrames = 0;
        return;
    }

    // Keep geometry from the current frame, but lock the class/label to the stable track class.
    det.classLocked = true;
    det.switchCandidateFrames = track.pendingClassFrames;
    det.originalClassId = det.originalClassId < 0 ? det.classId : det.originalClassId;
    det.originalLabel = det.originalLabel.empty() ? det.label : det.originalLabel;
    det.classId = track.stableClassId;
    det.label = track.stableLabel.empty() ? det.label : track.stableLabel;
    det.maturityCompetitionReason = "track_class_hysteresis_locked";
}

static std::vector<ObjectDetector::Detection>
updateFrameTracks(std::vector<FrameLevelTrack>& tracks,
                  int& nextTrackId,
                  const std::vector<ObjectDetector::Detection>& accepted,
                  const ObjectDetector::Config& cfg,
                  bool cameraMotionCooldownActive) {
    for (auto& t : tracks) {
        t.age += 1;
        t.matched = false;
    }

    for (auto det : accepted) {
        FrameLevelTrack* bestTrack = nullptr;
        float bestIou = 0.0f;

        for (auto& t : tracks) {
            if (t.matched) continue;
            if (cfg.trackClassAware && !sameMaturityCompetitionGroup(t.det.classId, det.classId)) continue;

            const float val = iouXYWH(t.det, det);
            if (val > bestIou) {
                bestIou = val;
                bestTrack = &t;
            }
        }

        if (bestTrack && bestIou >= cfg.trackIouThreshold) {
            applyTrackClassLock(*bestTrack, det, cfg);
            bestTrack->det = det;
            bestTrack->age = 0;
            bestTrack->matched = true;
            bestTrack->hits += 1;
            if (det.confidence > bestTrack->bestConfidence) {
                bestTrack->bestConfidence = det.confidence;
            }
            if (bestTrack->stableLabel.empty()) {
                bestTrack->stableLabel = det.label;
            }
            bestTrack->bestLabel = bestTrack->stableLabel;
        } else {
            FrameLevelTrack t;
            t.id = nextTrackId++;
            stampMaturityScores(det, cfg);
            t.det = det;
            t.bestConfidence = det.confidence;
            t.bestLabel = det.label;
            t.stableClassId = det.classId;
            t.stableLabel = det.label;
            t.stableClassScore = det.maturityScore > 0.0f ? det.maturityScore : det.confidence;
            t.hits = 1;
            t.age = 0;
            t.matched = true;
            tracks.push_back(t);
        }
    }

    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [&](const auto& t) { return t.age > cfg.trackMaxAge; }),
                 tracks.end());

    std::vector<ObjectDetector::Detection> visible;
    for (const auto& t : tracks) {
        // Draw only fresh geometry from the current frame.
        // Stable/best confidence and the locked maturity label are preserved,
        // but stale bbox/mask is not drawn.
        if (t.age != 0) continue;

        const bool enoughHits = t.hits >= cfg.trackMinHitsToShow;
        if (!enoughHits && !cameraMotionCooldownActive) continue;

        auto d = t.det;
        d.trackId = t.id;
        d.trackHits = t.hits;
        d.trackAge = t.age;
        d.currentConfidence = t.det.confidence;
        d.bestConfidence = std::max(t.bestConfidence, t.det.confidence);
        d.confidence = d.bestConfidence;
        d.label = t.stableLabel.empty() ? d.label : t.stableLabel;
        d.classId = t.stableClassId >= 0 ? t.stableClassId : d.classId;
        d.trackStableBest = true;
        d.weak = false;
        d.valid = true;
        visible.push_back(d);
    }

    std::sort(visible.begin(), visible.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
    return visible;
}

bool ObjectDetector::processFrame(const Frame& frame) {
    if (!ready_.load() || !running_.load() || !impl_->engineReady) return false;
    if (!frame.data || frame.width <= 0 || frame.height <= 0 || frame.channels != 3) return false;

    cv::Mat bgr(frame.height, frame.width, CV_8UC3,
                const_cast<uint8_t*>(frame.data),
                frame.strideBytes > 0 ? frame.strideBytes : frame.width * 3);

    impl_->frameCounter += 1;

    std::vector<ObjectDetector::Detection> rawDetections;
    std::string inferErr;
    if (!inferDetectionsOnBgrInternal(bgr, rawDetections, inferErr)) {
        std::cerr << "[detector] " << inferErr << "\n";
        clearLatestSnapshot();
        running_.store(false);
        return false;
    }

    std::vector<ObjectDetector::Detection> rawCandidatesForDebug = rawDetections;

    // First filtering pass on the full frame.
    // Part C uses accepted singles and weak-overlap regions to decide whether a second ROI inference is useful.
    std::vector<ObjectDetector::Detection> acceptedFull;
    std::vector<ObjectDetector::Detection> rejectedFull;
    splitAcceptedRejected(rawDetections, cfg_, frame.width, frame.height, acceptedFull, rejectedFull);
    acceptedFull = applyNms(std::move(acceptedFull), cfg_.nmsThreshold);

    // Part C: run the same engine a second time on small crop(s) when several
    // single tomatoes are close together but no strong bunch was accepted yet.
    auto roiRaw = runRoiSecondPassInternal(bgr, acceptedFull, rejectedFull, impl_->frameCounter);
    if (!roiRaw.empty()) {
        rawDetections.insert(rawDetections.end(), roiRaw.begin(), roiRaw.end());
        rawCandidatesForDebug.insert(rawCandidatesForDebug.end(), roiRaw.begin(), roiRaw.end());
    }

    std::vector<ObjectDetector::Detection> accepted;
    std::vector<ObjectDetector::Detection> rejected;
    std::vector<ObjectDetector::Detection> v32HeuristicOverlays;
    splitAcceptedRejected(rawDetections, cfg_, frame.width, frame.height, accepted, rejected);
    accepted = applyNms(std::move(accepted), cfg_.nmsThreshold);
    applyV32ExperimentalRuntimePolicy(accepted, rejected, v32HeuristicOverlays, cfg_, frame.width, frame.height);
    accepted = applyNms(std::move(accepted), cfg_.nmsThreshold);
    applyMaturityCompetition(accepted, rejected, cfg_);

    std::vector<ObjectDetector::Detection> detections;
    if (cfg_.useFrameTracking) {
        std::lock_guard<std::mutex> trackLock(impl_->frameTracksMutex);
        const bool cameraMotionCooldownActive =
            impl_->frameCounter <= impl_->cameraMotionCooldownUntilFrame;
        detections = updateFrameTracks(impl_->frameTracks,
                                       impl_->nextFrameTrackId,
                                       accepted,
                                       cfg_,
                                       cameraMotionCooldownActive);
    } else {
        detections = std::move(accepted);
    }

    detections = preferBunchesForDisplay(std::move(detections), cfg_);

    if (cfg_.useV32ExperimentalRuntimePolicy && cfg_.v32HeuristicsBypassFrameTracking) {
        detections.insert(detections.end(), v32HeuristicOverlays.begin(), v32HeuristicOverlays.end());
    }

    if (cfg_.drawWeakRejected) {
        detections.insert(detections.end(), rejected.begin(), rejected.end());
    }

    clearInternalMaskCoeffs(detections);
    clearInternalMaskCoeffs(rawCandidatesForDebug);
    for (auto& d : rawCandidatesForDebug) {
        d.mask.clear();
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);

        latest_.frame.width = frame.width;
        latest_.frame.height = frame.height;
        latest_.frame.channels = frame.channels;
        latest_.frame.strideBytes = frame.strideBytes;
        latest_.frame.timestampMs = frame.timestampMs;
        latest_.frame.data = nullptr;

        const size_t totalBytes =
            static_cast<size_t>(frame.height) * static_cast<size_t>(frame.strideBytes);
        latest_.frameBytes.assign(frame.data, frame.data + totalBytes);

        latest_.detections = std::move(detections);
        latest_.rawCandidates = std::move(rawCandidatesForDebug);
        latest_.valid = true;
    }

    return true;
}


bool ObjectDetector::processBgrForReplay(const cv::Mat& bgr, Snapshot& out) {
    out = Snapshot{};

    if (bgr.empty() || bgr.channels() != 3) {
        std::cerr << "[detector] replay image must be non-empty BGR CV_8UC3\n";
        return false;
    }

    if (!ready_.load()) {
        if (!init()) {
            std::cerr << "[detector] replay init failed\n";
            return false;
        }
    }

    if (!impl_->engineReady) {
        std::cerr << "[detector] replay TensorRT engine is not ready\n";
        return false;
    }

    impl_->frameCounter += 1;

    std::vector<ObjectDetector::Detection> rawDetections;
    std::string inferErr;
    if (!inferDetectionsOnBgrInternal(bgr, rawDetections, inferErr)) {
        std::cerr << "[detector] replay inference failed: " << inferErr << "\n";
        return false;
    }

    std::vector<ObjectDetector::Detection> rawCandidatesForDebug = rawDetections;

    std::vector<ObjectDetector::Detection> acceptedFull;
    std::vector<ObjectDetector::Detection> rejectedFull;
    splitAcceptedRejected(rawDetections, cfg_, bgr.cols, bgr.rows, acceptedFull, rejectedFull);
    acceptedFull = applyNms(std::move(acceptedFull), cfg_.nmsThreshold);

    auto roiRaw = runRoiSecondPassInternal(bgr, acceptedFull, rejectedFull, impl_->frameCounter);
    if (!roiRaw.empty()) {
        rawDetections.insert(rawDetections.end(), roiRaw.begin(), roiRaw.end());
        rawCandidatesForDebug.insert(rawCandidatesForDebug.end(), roiRaw.begin(), roiRaw.end());
    }

    std::vector<ObjectDetector::Detection> accepted;
    std::vector<ObjectDetector::Detection> rejected;
    std::vector<ObjectDetector::Detection> v32HeuristicOverlays;
    splitAcceptedRejected(rawDetections, cfg_, bgr.cols, bgr.rows, accepted, rejected);
    accepted = applyNms(std::move(accepted), cfg_.nmsThreshold);
    applyV32ExperimentalRuntimePolicy(accepted, rejected, v32HeuristicOverlays, cfg_, bgr.cols, bgr.rows);
    accepted = applyNms(std::move(accepted), cfg_.nmsThreshold);
    applyMaturityCompetition(accepted, rejected, cfg_);

    std::vector<ObjectDetector::Detection> detections;
    if (cfg_.useFrameTracking) {
        std::lock_guard<std::mutex> trackLock(impl_->frameTracksMutex);
        const bool cameraMotionCooldownActive =
            impl_->frameCounter <= impl_->cameraMotionCooldownUntilFrame;
        detections = updateFrameTracks(impl_->frameTracks,
                                       impl_->nextFrameTrackId,
                                       accepted,
                                       cfg_,
                                       cameraMotionCooldownActive);
    } else {
        detections = std::move(accepted);
    }

    detections = preferBunchesForDisplay(std::move(detections), cfg_);

    if (cfg_.useV32ExperimentalRuntimePolicy && cfg_.v32HeuristicsBypassFrameTracking) {
        detections.insert(detections.end(), v32HeuristicOverlays.begin(), v32HeuristicOverlays.end());
    }

    if (cfg_.drawWeakRejected) {
        detections.insert(detections.end(), rejected.begin(), rejected.end());
    }

    clearInternalMaskCoeffs(detections);
    clearInternalMaskCoeffs(rawCandidatesForDebug);
    for (auto& d : rawCandidatesForDebug) {
        d.mask.clear();
    }

    out.frame.width = bgr.cols;
    out.frame.height = bgr.rows;
    out.frame.channels = bgr.channels();
    out.frame.strideBytes = static_cast<int>(bgr.step);
    out.frame.timestampMs = nowMsLocal();
    out.frame.data = nullptr;

    const size_t totalBytes = static_cast<size_t>(bgr.rows) * static_cast<size_t>(bgr.step);
    out.frameBytes.assign(bgr.data, bgr.data + totalBytes);
    out.frame.data = out.frameBytes.empty() ? nullptr : out.frameBytes.data();
    out.detections = std::move(detections);
    out.rawCandidates = std::move(rawCandidatesForDebug);
    out.valid = true;
    return true;
}

bool ObjectDetector::captureAndProcessNextFrame() {
    if (!running_.load()) return false;
    if (!impl_->engineReady) return false;
    if (!impl_->cameraOpened || !impl_->cap.isOpened()) return false;

    cv::Mat frame;
    if (!impl_->cap.read(frame) || frame.empty()) {
        std::cerr << "[detector] failed to read frame from CSI camera -> stopping detector worker\n";
        clearLatestSnapshot();
        releaseCamera();
        running_.store(false);
        return false;
    }

    float zoom = 1.0f;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        zoom = cfg_.digitalZoom;
    }

    cv::Mat cameraFrame = applyDigitalZoomToFrame(frame, zoom);
    impl_->lastBgrFrame = cameraFrame.clone();

    Frame f;
    f.data = impl_->lastBgrFrame.data;
    f.width = impl_->lastBgrFrame.cols;
    f.height = impl_->lastBgrFrame.rows;
    f.channels = impl_->lastBgrFrame.channels();
    f.strideBytes = static_cast<int>(impl_->lastBgrFrame.step);
    f.timestampMs = nowMsLocal();

    return processFrame(f);
}

bool ObjectDetector::getLatestSnapshot(Snapshot& out) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!latest_.valid) return false;
    out = latest_;
    out.frame.data = out.frameBytes.empty() ? nullptr : out.frameBytes.data();
    return true;
}

void ObjectDetector::clearLatestSnapshot() {
    std::lock_guard<std::mutex> lock(mtx_);
    latest_ = Snapshot{};
}

void ObjectDetector::workerLoop() {
    while (running_.load()) {
        if (!captureAndProcessNextFrame()) {
            break;
        }

        if (cfg_.loopSleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.loopSleepMs));
        }
    }

    releaseCamera();
}


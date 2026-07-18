#include <iostream>
#include <cstdint>
#include <unistd.h>




#include "hardware/RaspbotBoard.h"
#include "hardware/ServoCamera.h"
#include "hardware/MotorDCfb.h"
#include "hardware/MotorDCLR.h"
#include "control/DriveController.h"
#include "control/TestDriveOp_g.h"
#include "orchestration/JoystickController.h"
#include "perception/ObjectDetector.h"
#include "perception/TargetTracker.h"
#include "Debugging/DebugFlags.h"




static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}




// ---- Generic drive submenu for one MotorDCfb ----
static void menu_drive_motor(MotorDCfb& m, const char* title)
{
    int rawSpeed   = 120;
    int duty       = 40;
    int periodMs   = 20;
    int durationMs = 450;




    static bool micro = false;




    bool back = false;
    while (!back) {
        std::cout << "\n=== " << title << " ===\n"
                  << "Settings: rawSpeed=" << rawSpeed
                  << " duty=" << duty << "% period=" << periodMs
                  << "ms duration=" << durationMs << "ms\n"
                  << "Options:\n"
                  << "  f - forward test\n"
                  << "  b - backward test\n"
                  << "  u - micro (PWM) mode for next tests\n"
                  << "  r - raw (continuous) mode for next tests\n"
                  << "  + / -  rawSpeed +/-10\n"
                  << "  ] / [  duty +/-1%\n"
                  << "  } / {  period +/-20ms\n"
                  << "  d / a  duration +/-200ms\n"
                  << "  x - stop\n"
                  << "  q - back\n"
                  << "Choose: ";




        char c;
        std::cin >> c;
        if (!std::cin) return;




        switch (c) {
            case 'q': case 'Q': back = true; m.stop(); break;
            case 'x': case 'X': m.stop(); break;
            case 'u': case 'U': micro = true;  std::cout << "Mode=micro\n"; break;
            case 'r': case 'R': micro = false; std::cout << "Mode=raw\n"; break;




            case '+': rawSpeed = clampi(rawSpeed + 10, 0, 255); break;
            case '-': rawSpeed = clampi(rawSpeed - 10, 0, 255); break;
            case ']': duty = clampi(duty + 1, 1, 100); break;
            case '[': duty = clampi(duty - 1, 1, 100); break;
            case '}': periodMs = clampi(periodMs + 20, 20, 2000); break;
            case '{': periodMs = clampi(periodMs - 20, 20, 2000); break;
            case 'd': durationMs = clampi(durationMs + 200, 50, 20000); break;
            case 'a': durationMs = clampi(durationMs - 200, 50, 20000); break;




            case 'f': case 'F':
                m.testOnce(rawSpeed, true, micro, duty, periodMs, durationMs);
                break;




            case 'b': case 'B':
                m.testOnce(rawSpeed, false, micro, duty, periodMs, durationMs);
                break;




            default:
                std::cout << "Unknown.\n";
                break;
        }
    }
}




// ---- Steering submenu ----
static void menu_steering(MotorDCLR& lr)
{
    int rawSpeed   = 40;
    int duty       = 20;
    int periodMs   = 120;
    int durationMs = 400;




    static bool micro = false;




    bool back = false;
    while (!back) {
        std::cout << "\n=== Steering Motor (channel 2) ===\n"
                  << "Mapping: + = RIGHT, - = LEFT (f=right, b=left)\n"
                  << "Settings: rawSpeed=" << rawSpeed
                  << " duty=" << duty << "% period=" << periodMs
                  << "ms duration=" << durationMs << "ms\n"
                  << "Options:\n"
                  << "  f - RIGHT test\n"
                  << "  b - LEFT test\n"
                  << "  r - raw (continuous) mode for next tests\n"
                  << "  u - micro (PWM) mode for next tests\n"
                  << "  + / -  rawSpeed +/-10\n"
                  << "  ] / [  duty +/-1%\n"
                  << "  } / {  period +/-20ms\n"
                  << "  d / a  duration +/-200ms\n"
                  << "  x - stop\n"
                  << "  q - back\n"
                  << "Choose: ";




        char c;
        std::cin >> c;
        if (!std::cin) return;




        switch (c) {
            case 'q': case 'Q': back = true; lr.stop(); break;
            case 'x': case 'X': lr.stop(); break;
            case 'u': case 'U': micro = true; std::cout << "Mode=micro\n"; break;
            case 'r': case 'R': micro = false; std::cout << "Mode=raw\n"; break;




            case '+': rawSpeed = clampi(rawSpeed + 10, 0, 255); break;
            case '-': rawSpeed = clampi(rawSpeed - 10, 0, 255); break;
            case ']': duty = clampi(duty + 1, 1, 100); break;
            case '[': duty = clampi(duty - 1, 1, 100); break;
            case '}': periodMs = clampi(periodMs + 20, 20, 2000); break;
            case '{': periodMs = clampi(periodMs - 20, 20, 2000); break;
            case 'd': durationMs = clampi(durationMs + 200, 50, 20000); break;
            case 'a': durationMs = clampi(durationMs - 200, 50, 20000); break;




            case 'f': case 'F':
                lr.testOnce(rawSpeed, true, micro, duty, periodMs, durationMs);
                break;




            case 'b': case 'B':
                lr.testOnce(rawSpeed, false, micro, duty, periodMs, durationMs);
                break;




            default:
                std::cout << "Unknown.\n";
                break;
        }
    }
}




// ---- Main menu d ----
static void menu_d(MotorDCfb& rearDrive, MotorDCfb& frontDrive, MotorDCLR& steering)
{
    bool back = false;
    while (!back) {
        std::cout << "\n=== DC Motors (d) ===\n"
                  << "Choose motor group:\n"
                  << "  1 - Rear drive motor   (channel 1)\n"
                  << "  2 - Front drive motor  (channel 3)\n"
                  << "  3 - Steering motor     (channel 2)\n"
                  << "  x - stop all\n"
                  << "  q - back\n"
                  << "Choose: ";




        char c;
        std::cin >> c;
        if (!std::cin) return;




        if (c == 'q' || c == 'Q') {
            rearDrive.stop();
            frontDrive.stop();
            steering.stop();
            back = true;
        } else if (c == 'x' || c == 'X') {
            rearDrive.stop();
            frontDrive.stop();
            steering.stop();
        } else if (c == '1') {
            menu_drive_motor(rearDrive, "Rear Drive Motor (channel 1)");
        } else if (c == '2') {
            menu_drive_motor(frontDrive, "Front Drive Motor (channel 3)");
        } else if (c == '3') {
            menu_steering(steering);
        } else {
            std::cout << "Unknown.\n";
        }
    }
}




static void menu_debugging(DebugFlags& debugFlags)
{
    bool back = false;




    while (!back) {
        std::cout << "\n=== Debugging Menu ===\n"
                  << "Current flags:\n"
                  << "  1 - TEST_YAW        : " << (debugFlags.testYaw ? "ON" : "OFF") << "\n"
                  << "  2 - TEST_NAV_ODOM   : " << (debugFlags.testNavOdom ? "ON" : "OFF") << "\n"
                  << "  3 - TEST_NAV_LIDAR  : " << (debugFlags.testNavLidar ? "ON" : "OFF") << "\n"
                  << "  4 - TO_CLIENT_JSON   : " << (debugFlags.toClient ? "ON" : "OFF") << "\n"
                  << "\nOptions:\n"
                  << "  1 - toggle TEST_YAW\n"
                  << "  2 - toggle TEST_NAV_ODOM\n"
                  << "  3 - toggle TEST_NAV_LIDAR\n"
                  << "  4 - toggle TO_CLIENT_JSON\n"
                  << "  0 - back\n"
                  << "Choose: ";




        char c;
        std::cin >> c;
        if (!std::cin) return;




        switch (c) {
            case '1':
                debugFlags.testYaw = !debugFlags.testYaw;
                std::cout << "[debug] TEST_YAW -> "
                          << (debugFlags.testYaw ? "ON" : "OFF") << "\n";
                break;




            case '2':
                debugFlags.testNavOdom = !debugFlags.testNavOdom;
                std::cout << "[debug] TEST_NAV_ODOM -> "
                          << (debugFlags.testNavOdom ? "ON" : "OFF") << "\n";
                break;




            case '3':
                debugFlags.testNavLidar = !debugFlags.testNavLidar;
                std::cout << "[debug] TEST_NAV_LIDAR -> "
                          << (debugFlags.testNavLidar ? "ON" : "OFF") << "\n";
                break;




            case '4':
                debugFlags.toClient = !debugFlags.toClient;
                std::cout << "[debug] TO_CLIENT_JSON -> "
                          << (debugFlags.toClient ? "ON" : "OFF") << "\n";
                break;




            case '0':
                back = true;
                break;




            default:
                std::cout << "Unknown.\n";
                break;
        }
    }
}




int main()
{
    std::cout << "Raspbot V2 (Jetson) - dual drive + steering + DriveController + TensorRT YOLOv8s\n";




    DebugFlags debugFlags{};




    RaspbotBoard board("/dev/i2c-7", 0x2B);
    if (!board.openBus()) {
        return 1;
    }




    std::cout << "I2C opened on /dev/i2c-7, address 0x2B\n";




    ServoCamera cam(board, 1, 2, 80, 105);




    MotorDCfb rearDrive(board, 1, true);
    MotorDCfb frontDrive(board, 3, false);
    MotorDCLR steering(board, 2);




    DriveController::Config driveCfg;
    driveCfg.speedMin = 0;
    driveCfg.speedMax = 255;
    driveCfg.fbUseMicro = false;
    driveCfg.fbForceMaxWhileMoving = false;
    driveCfg.fbMinDrivePower = 0;
    driveCfg.fbSoftStopEnabled = false;
    driveCfg.fbRampStep = 16;
    driveCfg.fbRampTickMs = 20;




    DriveController drive(rearDrive, frontDrive, steering, driveCfg);
    drive.resetSpeeds();




    ObjectDetector::Config detectorCfg;
    detectorCfg.backend = ObjectDetector::Backend::TensorRTJetson;
    detectorCfg.modelName = "best8s_seg_v43";
    detectorCfg.modelDir = "models";
    detectorCfg.onnxPath = "models/best8s_seg_v43.onnx";
    detectorCfg.enginePath = "models/best8s_seg_v43_fp16.engine";
    detectorCfg.useGpu = true;
    detectorCfg.useFP16 = true;
    detectorCfg.inputWidth = 640;
    detectorCfg.inputHeight = 640;
    detectorCfg.rawConfThreshold = 0.15f;
    detectorCfg.confThreshold = 0.70f;
    detectorCfg.nmsThreshold = 0.45f;
    detectorCfg.personOnly = false;
    detectorCfg.useFrameTracking = true;
    detectorCfg.trackIouThreshold = 0.35f;
    detectorCfg.trackMaxAge = 8;
    detectorCfg.trackMinHitsToShow = 3;
    detectorCfg.trackClassAware = true;
    detectorCfg.drawWeakRejected = true;
    detectorCfg.maxWeakToKeep = 25;
    detectorCfg.useRoiFilter = true;
    detectorCfg.roiXMin = 0.03f;
    detectorCfg.roiXMax = 0.97f;
    detectorCfg.roiYMin = 0.05f;
    detectorCfg.roiYMax = 0.95f;
    detectorCfg.minBoxAreaSingle = 250.0f;
    detectorCfg.minBoxAreaBunch = 2500.0f;
    detectorCfg.classThresholds = {
        {0, 0.65f},
        {1, 0.80f},
        {2, 0.85f},
        {3, 0.70f}
    };
    detectorCfg.modelClassCount = 4;
    detectorCfg.classNames = {
        {0, "eripe bunch"},
        {1, "ripe"},
        {2, "unripe"},
        {3, "unripe bunch"}
    };

    // Ver32 experimental runtime policy.
    // Adds Review / ColorCorrected / yellow Heuristics overlays in live CAMERA GUI and L1 toClient debugging.
    // It is additive: old fields remain available for the Next.js client.
    detectorCfg.useV32ExperimentalRuntimePolicy = true;
    detectorCfg.v32PublishReviewAsAccepted = true;
    detectorCfg.v32UseColorClassCorrection = true;
    detectorCfg.v32UseWeakBunchAnchorHeuristic = true;
    detectorCfg.v32HeuristicsBypassFrameTracking = true;

    detectorCfg.v32ReviewUnripeMinConf = 0.80f;
    detectorCfg.v32ReviewRipeMinConf = 0.75f;
    detectorCfg.v32ReviewMinMaskDensity = 0.35f;
    detectorCfg.v32ReviewMinMaskArea = 1000.0f;
    detectorCfg.v32ReviewUnripeMinGreenYellow = 0.35f;
    detectorCfg.v32ReviewRipeMinRed = 0.08f;
    detectorCfg.v32ReviewRipeMinWarm = 0.12f;

    detectorCfg.v32ColorCorrectionMinConf = 0.50f;
    detectorCfg.v32ColorCorrectionMinMaskDensity = 0.35f;
    detectorCfg.v32RipeToUnripeMinGreenYellow = 0.45f;
    detectorCfg.v32RipeToUnripeMaxRed = 0.06f;
    detectorCfg.v32RipeToUnripeMaxWarm = 0.12f;
    detectorCfg.v32UnripeToRipeMinWarm = 0.66f;
    detectorCfg.v32UnripeToRipeMaxGreenYellow = 0.35f;

    detectorCfg.v32HeuristicAnchorMinConf = 0.55f;
    detectorCfg.v32HeuristicAnchorStrongConf = 0.70f;
    detectorCfg.v32HeuristicChildMinConf = 0.55f;
    detectorCfg.v32HeuristicChildMinMaskDensity = 0.35f;
    detectorCfg.v32HeuristicMinChildrenStrongAnchor = 4;
    detectorCfg.v32HeuristicMinChildrenWeakAnchor = 5;
    detectorCfg.v32HeuristicMinWeightedStrongAnchor = 2.60f;
    detectorCfg.v32HeuristicMinWeightedWeakAnchor = 3.40f;
    detectorCfg.v32HeuristicAnchorPaddingRatio = 0.10f;
    detectorCfg.v32HeuristicUnionPaddingRatio = 0.04f;
    detectorCfg.v32HeuristicMaturityMargin = 0.60f;
    detectorCfg.v32HeuristicDuplicateIou = 0.30f;
    detectorCfg.v32HeuristicMaxPerFrame = 2;
    detectorCfg.v32HeuristicMaxBoxAreaFraction = 0.18f;
    detectorCfg.v32HeuristicMinAnchorMaskDensity = 0.02f;
    detectorCfg.v32HeuristicMinAnchorMaskArea = 300.0f;



    detectorCfg.sensorId = 0;
    detectorCfg.cameraWidth = 1280;
    detectorCfg.cameraHeight = 720;
    detectorCfg.cameraFps = 30;




    detectorCfg.showGui = true;
    detectorCfg.windowName = "Jetson YOLOv8 TensorRT";




    ObjectDetector detector(detectorCfg);




    TargetTracker::Config trackerCfg;
    trackerCfg.minConfidence = 0.50f;
    trackerCfg.personOnly = false;
    trackerCfg.personLabel = "ripe";
    trackerCfg.targetXRatio = 0.50f;
    trackerCfg.targetYRatio = 0.33f;
    trackerCfg.deadbandX = 25;
    trackerCfg.deadbandY = 20;
    trackerCfg.jitterThresholdPx = 12;
    trackerCfg.kpPan = 0.025f;
    trackerCfg.kpTilt = 0.025f;
    trackerCfg.maxStepPan = 5;
    trackerCfg.maxStepTilt = 5;




    trackerCfg.invertPan = true;
    trackerCfg.invertTilt = false;




    trackerCfg.usePanLimits = false;
    trackerCfg.useTiltLimits = false;
    trackerCfg.autoCenterOnLost = false;
    trackerCfg.minUpdateIntervalMs = 0;




    TargetTracker tracker(cam, trackerCfg);




    bool quit_all = false;
    while (!quit_all) {
        std::cout << "\n=== Menu ===\n"
                  << "  c - camera interactive calibration (WASD)\n"
                  << "  t - tiny movement test (±10°)\n"
                  << "  d - DC motors (rear drive / front drive / steering)\n"
                  << "  g - drive mode (keyboard WASD + 1/2/3/4 speeds)\n"
                  << "  j - drive mode (joystick drive + keyboard shortcuts + tomato detector)\n"
                  << "  y - debugging options\n"
                  << "  b - buzzer 1s (quick I2C ACK)\n"
                  << "  q - quit\n"
                  << "Choose: ";




        char c;
        std::cin >> c;
        if (!std::cin) break;




        if (c == 'q' || c == 'Q') {
            quit_all = true;
        } else if (c == 'b' || c == 'B') {
            board.setBuzzer(true);
            sleep(1);
            board.setBuzzer(false);
        } else if (c == 'c' || c == 'C') {
            cam.runInteractive();
        } else if (c == 't' || c == 'T') {
            cam.runMovementTest(10, 600);
        } else if (c == 'd' || c == 'D') {
            menu_d(rearDrive, frontDrive, steering);
        } else if (c == 'g' || c == 'G') {
            RunTestDriveOp_g(drive);
        } else if (c == 'y' || c == 'Y') {
            menu_debugging(debugFlags);
        } else if (c == 'j' || c == 'J') {
            JoystickController joy(&tracker, &detector, &board);




            joy.setDebugFlags(debugFlags);




            if (!joy.init()) {
                std::cout << "[joy] init failed\n";
            } else {
                joy.setTrackingPump([&]() {
                    detector.captureAndProcessNextFrame();
                });




                quit_all = joy.run(drive);
                joy.close();
            }
        } else {
            std::cout << "Unknown.\n";
        }
    }




    tracker.stop();
    detector.stop();
    drive.stopAll();
    board.closeBus();




    std::cout << "Exit.\n";
    return 0;
}
















#pragma once




#include "core/LidarTypes.h"
#include "core/SystemState.h"




#include <cstdint>
#include <unordered_set>
#include <vector>




class DevFarmScanMatcher {
public:
    struct Result {
        bool attempted = false;
        bool accepted = false;
        bool mapReady = false;




        double score = 0.0;
        double baseScore = 0.0;
        double improvement = 0.0;




        double dxM = 0.0;
        double dyM = 0.0;
        double dYawDeg = 0.0;




        std::size_t usedPoints = 0;
        std::size_t mapCells = 0;
    };




public:
    DevFarmScanMatcher() = default;




    void reset();




    Result match(const LidarSnapshot& snapshot,
                 const OdomState& odom,
                 std::uint64_t nowMs) const;


    Result matchPose(const LidarSnapshot& snapshot,
                     double poseXM,
                     double poseYM,
                     double poseYawDeg,
                     std::uint64_t nowMs) const;




    void addMapPoint(double xM, double yM);
    std::size_t mapCellsCount() const;




private:
    bool isCellOccupiedNear(int ix, int iy) const;
    static long long makeKey(int ix, int iy);
    static int toCell(double meters);
    static double normalizeAngleDeg(double angleDeg);




private:
    std::unordered_set<long long> occupiedCells_{};




    static constexpr double kGridResolutionM = 0.05;      // 5cm matching grid, not saved map resolution.
    static constexpr std::size_t kMinMapCellsForMatch = 160;
    static constexpr std::size_t kMaxMatchPoints = 180;
    static constexpr std::size_t kMatchPointStride = 8;
    static constexpr double kMinDistanceM = 0.18;
    static constexpr double kMaxDistanceM = 6.00;




    static constexpr double kAcceptScore = 0.30;
    static constexpr double kAcceptImprovement = 0.035;
    static constexpr double kStrongAcceptScore = 0.50;
};
















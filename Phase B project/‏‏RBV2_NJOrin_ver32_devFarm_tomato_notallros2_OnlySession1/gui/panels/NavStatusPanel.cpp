#include "gui/panels/NavStatusPanel.h"


#include <SDL2/SDL.h>
#include <string>
#include <cmath>


namespace {


static std::string poseSourceToString(NavPoseSource s) {
    switch (s) {
        case NavPoseSource::YawOnly: return "YAW";
        case NavPoseSource::YawCmd:  return "YAW CMD";
        case NavPoseSource::YawOdom: return "YAW ODOM";
        case NavPoseSource::Slam:    return "SLAM";
        case NavPoseSource::None:
        default: return "NONE";
    }
}


static std::string motionStateToString(NavMotionState s) {
    switch (s) {
        case NavMotionState::Forward: return "FWD";
        case NavMotionState::Reverse: return "REV";
        case NavMotionState::Stop:
        default: return "STOP";
    }
}


static std::string turnStateToString(NavTurnState s) {
    switch (s) {
        case NavTurnState::Left: return "LEFT";
        case NavTurnState::Right: return "RIGHT";
        case NavTurnState::Straight:
        default: return "STRAIGHT";
    }
}


static std::string odomPoseSourceToString(OdomPoseSource s) {
    switch (s) {
        case OdomPoseSource::CmdYawRate:          return "CMD";
        case OdomPoseSource::ImuYawRate:          return "IMU";
        case OdomPoseSource::ImuYawRateCmdLinear: return "IMU+CMD";
        case OdomPoseSource::Encoder:             return "ENC";
        case OdomPoseSource::Fused:               return "FUSED";
        case OdomPoseSource::None:
        default: return "NONE";
    }
}


static std::string yn(bool v) {
    return v ? "YES" : "NO";
}


static std::string cmdIntString(double v)
{
    const int iv = static_cast<int>(std::lround(v));
    return std::to_string(iv);
}


static std::string distString(const PanelPaintContext& ctx, double meters)
{
    if (meters < 0.0) {
        return "N/A";
    }
    return ctx.trimNumber(meters, 2, 6) + "m";
}


static std::string sectorName(int idx)
{
    switch (idx) {
        case 0: return "F";
        case 1: return "FL";
        case 2: return "L";
        case 3: return "RL";
        case 4: return "REAR";
        case 5: return "RR";
        case 6: return "R";
        case 7: return "FR";
        default: return "NONE";
    }
}


static std::string sideName(int sign)
{
    if (sign < 0) return "LEFT";
    if (sign > 0) return "RIGHT";
    return "NONE";
}


} // namespace


void NavStatusPanel::draw(const SDL_Rect& rect,
                          const RobotState& rs,
                          const PanelPaintContext& ctx) const {
    SDL_SetRenderDrawColor(ctx.renderer, 10, 10, 14, 255);
    SDL_RenderFillRect(ctx.renderer, &rect);


    SDL_SetRenderDrawColor(ctx.renderer, 80, 80, 96, 255);
    SDL_RenderDrawRect(ctx.renderer, &rect);


    const int pad = 8;
    const int lineH = 15;
    const int headerGap = 17;


    const int innerX = rect.x + pad;
    const int innerY = rect.y + pad;
    const int innerW = rect.w - (pad * 2);
    const int innerH = rect.h - (pad * 2);


    const int gap = 10;
    const int navW = static_cast<int>(innerW * 0.22);
    const int odomW = static_cast<int>(innerW * 0.22);
    const int lidarTotalW = innerW - navW - odomW - (gap * 3);
    const int lidarSubW = (lidarTotalW - gap) / 2;


    const SDL_Rect navCol{ innerX, innerY, navW, innerH };
    const SDL_Rect odomCol{ navCol.x + navCol.w + gap, innerY, odomW, innerH };
    const SDL_Rect lidarACol{ odomCol.x + odomCol.w + gap, innerY, lidarSubW, innerH };
    const SDL_Rect lidarBCol{ lidarACol.x + lidarACol.w + gap, innerY, lidarSubW, innerH };


    SDL_SetRenderDrawColor(ctx.renderer, 60, 60, 72, 255);
    SDL_RenderDrawLine(ctx.renderer,
                       navCol.x + navCol.w + (gap / 2), rect.y + 6,
                       navCol.x + navCol.w + (gap / 2), rect.y + rect.h - 6);
    SDL_RenderDrawLine(ctx.renderer,
                       odomCol.x + odomCol.w + (gap / 2), rect.y + 6,
                       odomCol.x + odomCol.w + (gap / 2), rect.y + rect.h - 6);
    SDL_RenderDrawLine(ctx.renderer,
                       lidarACol.x + lidarACol.w + (gap / 2), rect.y + 6,
                       lidarACol.x + lidarACol.w + (gap / 2), rect.y + rect.h - 6);


    int x = navCol.x;
    int y = navCol.y;


    ctx.drawText(x, y, "NAV", SDL_Color{120, 180, 255, 255}, 2);
    y += headerGap;


    ctx.drawText(x, y, "SRC: " + poseSourceToString(rs.nav.poseSource),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "MOT: " + motionStateToString(rs.nav.motionState),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "TURN: " + turnStateToString(rs.nav.turnState),
                 SDL_Color{255, 210, 80, 255}, 2); y += lineH;
    ctx.drawText(x, y, "POSE: " + std::string(rs.nav.estimatedPose ? "EST CMD" : "N/A"),
                 rs.nav.estimatedPose ? SDL_Color{120, 255, 170, 255}
                                      : SDL_Color{255, 170, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "AUTO: " + std::string(rs.nav.localAutoEnabled ? "ON" : "OFF"),
                 rs.nav.localAutoEnabled ? SDL_Color{120, 255, 170, 255}
                                         : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "ACTIVE: " + yn(rs.nav.localAutoActive),
                 rs.nav.localAutoActive ? SDL_Color{120, 255, 170, 255}
                                        : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "REACHED: " + yn(rs.nav.localAutoGoalReached),
                 rs.nav.localAutoGoalReached ? SDL_Color{255, 210, 80, 255}
                                             : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "BLOCK: " + yn(rs.nav.localAutoBlockedByEStop),
                 rs.nav.localAutoBlockedByEStop ? SDL_Color{255, 120, 120, 255}
                                                : SDL_Color{220, 220, 225, 255}, 2); y += lineH;


    ctx.drawText(x, y, "IMU NAV: " + yn(rs.navGuard.imuAvailableForNav),
                 rs.navGuard.imuAvailableForNav ? SDL_Color{120, 255, 170, 255}
                                                : SDL_Color{255, 120, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "NFRSH: " + yn(rs.navGuard.navPoseFresh),
                 rs.navGuard.navPoseFresh ? SDL_Color{120, 255, 170, 255}
                                          : SDL_Color{255, 170, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "GDEG: " + yn(rs.navGuard.navDegraded),
                 rs.navGuard.navDegraded ? SDL_Color{255, 120, 120, 255}
                                         : SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "GFRZ: " + yn(rs.navGuard.navFrozen),
                 rs.navGuard.navFrozen ? SDL_Color{255, 120, 120, 255}
                                       : SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "GSTOP: " + yn(rs.navGuard.safeStopTriggered),
                 rs.navGuard.safeStopTriggered ? SDL_Color{255, 210, 80, 255}
                                               : SDL_Color{180, 180, 190, 255}, 2); y += lineH;


    ctx.drawText(x, y, "X: " + ctx.trimNumber(rs.nav.xMeters, 2, 7),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "Y: " + ctx.trimNumber(rs.nav.yMeters, 2, 7),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "YAW: " + ctx.trimNumber(rs.nav.yawRelativeDeg, 1, 7),
                 SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "GX: " + ctx.trimNumber(rs.nav.goalXMeters, 2, 7),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "GY: " + ctx.trimNumber(rs.nav.goalYMeters, 2, 7),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "G D: " + ctx.trimNumber(rs.nav.goalDistanceMeters, 2, 7),
                 SDL_Color{255, 210, 80, 255}, 2); y += lineH;
    ctx.drawText(x, y, "H ERR: " + ctx.trimNumber(rs.nav.headingErrorDeg, 1, 7),
                 SDL_Color{120, 180, 255, 255}, 2);


    x = odomCol.x;
    y = odomCol.y;


    ctx.drawText(x, y, "ODOM", SDL_Color{120, 255, 170, 255}, 2);
    y += headerGap;


    ctx.drawText(x, y, "SRC: " + odomPoseSourceToString(rs.odom.poseSource),
                 SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "VALID: " + yn(rs.odom.valid),
                 rs.odom.valid ? SDL_Color{120, 255, 170, 255}
                               : SDL_Color{255, 120, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "YAW OK: " + yn(rs.odom.yawValid),
                 rs.odom.yawValid ? SDL_Color{120, 255, 170, 255}
                                  : SDL_Color{255, 170, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "REF: " + yn(rs.odom.referenceInitialized),
                 rs.odom.referenceInitialized ? SDL_Color{120, 255, 170, 255}
                                              : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "ACT: " + yn(rs.odom.integrationActive),
                 rs.odom.integrationActive ? SDL_Color{120, 255, 170, 255}
                                           : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "FRSH: " + yn(rs.odom.isFresh),
                 rs.odom.isFresh ? SDL_Color{120, 255, 170, 255}
                                 : SDL_Color{255, 170, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "STAL: " + yn(rs.odom.isStale),
                 rs.odom.isStale ? SDL_Color{255, 120, 120, 255}
                                 : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "X: " + ctx.trimNumber(rs.odom.xMeters, 2, 6),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "Y: " + ctx.trimNumber(rs.odom.yMeters, 2, 6),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "YAW: " + ctx.trimNumber(rs.odom.yawDeg, 1, 6),
                 SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "V: " + ctx.trimNumber(rs.odom.linearVelocityMps, 2, 6),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "W: " + ctx.trimNumber(rs.odom.angularVelocityDegPs, 1, 6),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "DT: " + ctx.trimNumber(rs.odom.dtSec, 3, 6),
                 SDL_Color{255, 210, 80, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RDT: " + ctx.trimNumber(rs.odom.rawDtSec, 3, 6),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "FWD: " + cmdIntString(rs.odom.forwardCommand),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "STR: " + cmdIntString(rs.odom.steeringCommand),
                 SDL_Color{220, 220, 225, 255}, 2);


    x = lidarACol.x;
    y = lidarACol.y;


    ctx.drawText(x, y, "LIDAR-A", SDL_Color{255, 210, 80, 255}, 2);
    y += headerGap;


    ctx.drawText(x, y, "VALID: " + yn(rs.lidarPose.valid),
                 rs.lidarPose.valid ? SDL_Color{120, 255, 170, 255}
                                    : SDL_Color{255, 120, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "FRESH: " + yn(rs.lidarPose.isFresh),
                 rs.lidarPose.isFresh ? SDL_Color{120, 255, 170, 255}
                                      : SDL_Color{255, 170, 120, 255}, 2); y += lineH;
    ctx.drawText(x, y, "CONF: " + ctx.trimNumber(rs.lidarPose.confidence, 2, 5),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "NEAR: " + distString(ctx, rs.lidarPose.nearestDistanceM),
                 SDL_Color{255, 210, 80, 255}, 2); y += lineH;
    ctx.drawText(x, y, "SECT: " + sectorName(rs.lidarPose.nearestSectorIndex),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "F: " + distString(ctx, rs.lidarPose.frontDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "FL: " + distString(ctx, rs.lidarPose.frontLeftDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "FR: " + distString(ctx, rs.lidarPose.frontRightDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "L: " + distString(ctx, rs.lidarPose.leftDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "R: " + distString(ctx, rs.lidarPose.rightDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;


    x = lidarBCol.x;
    y = lidarBCol.y;


    ctx.drawText(x, y, "LIDAR-B", SDL_Color{255, 210, 80, 255}, 2);
    y += headerGap;


    ctx.drawText(x, y, "REAR: " + distString(ctx, rs.lidarPose.rearDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RL: " + distString(ctx, rs.lidarPose.rearLeftDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RR: " + distString(ctx, rs.lidarPose.rearRightDistanceM),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "LAT: " + ctx.trimNumber(rs.lidarPose.lateralBalanceM, 2, 6),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "FBAL: " + ctx.trimNumber(rs.lidarPose.frontBalanceM, 2, 6),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RBAL: " + ctx.trimNumber(rs.lidarPose.rearBalanceM, 2, 6),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "CERR: " + ctx.trimNumber(rs.lidarPose.centerErrorM, 2, 6),
                 SDL_Color{255, 210, 80, 255}, 2); y += lineH;
    ctx.drawText(x, y, "HHINT: " + ctx.trimNumber(rs.lidarPose.headingHintDeg, 1, 6),
                 SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "AHEAD: " + yn(rs.lidarPose.obstacleAhead),
                 rs.lidarPose.obstacleAhead ? SDL_Color{255, 120, 120, 255}
                                            : SDL_Color{120, 255, 170, 255}, 2); y += lineH;
    ctx.drawText(x, y, "REARB: " + yn(rs.lidarPose.obstacleRear),
                 rs.lidarPose.obstacleRear ? SDL_Color{255, 120, 120, 255}
                                           : SDL_Color{120, 255, 170, 255}, 2); y += lineH;


    ctx.drawText(x, y, "HSUG: " + yn(rs.lidarHints.steerCorrectionSuggested),
                 rs.lidarHints.steerCorrectionSuggested ? SDL_Color{255, 210, 80, 255}
                                                        : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "HDIR: " + sideName(rs.lidarHints.suggestedSteerSign),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "HSTR: " + ctx.trimNumber(rs.lidarHints.suggestedSteerStrength, 2, 5),
                 SDL_Color{120, 180, 255, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RSUG: " + yn(rs.lidarHints.reverseCorrectionSuggested),
                 rs.lidarHints.reverseCorrectionSuggested ? SDL_Color{255, 210, 80, 255}
                                                          : SDL_Color{180, 180, 190, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RDIR: " + sideName(rs.lidarHints.preferredReverseSide),
                 SDL_Color{220, 220, 225, 255}, 2); y += lineH;
    ctx.drawText(x, y, "RSTR: " + ctx.trimNumber(rs.lidarHints.reversePreferenceStrength, 2, 5),
                 SDL_Color{120, 180, 255, 255}, 2);
}




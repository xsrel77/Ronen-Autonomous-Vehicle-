#pragma once


#include "core/SystemState.h"
#include "core/BehaviorTypes.h"


class BehaviorManager {
public:
    BehaviorManager() = default;
    ~BehaviorManager() = default;


    BehaviorDecision evaluate(const RobotState& state) const;


private:
    RobotMode determineMode(const RobotState& state) const;
};




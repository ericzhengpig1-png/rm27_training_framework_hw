#include "M2006.hpp"

namespace
{
int16_t LimitCurrent(float current, uint16_t max_current)
{
    if (current != current)
    {
        return 0;
    }

    if (max_current > 32767U)
    {
        max_current = 32767U;
    }

    const float current_limit = static_cast<float>(max_current);
    if (current > current_limit)
    {
        return static_cast<int16_t>(max_current);
    }
    if (current < -current_limit)
    {
        return -static_cast<int16_t>(max_current);
    }
    return static_cast<int16_t>(current);
}
}

M2006::M2006()
{
    gearBox = GearBox_M2006;
    maxCurrent = M2006_MAX_CURRENT_COMMAND;
    speedPid.maxOut = static_cast<float>(maxCurrent);
    speedPid.maxIOut = static_cast<float>(maxCurrent);
}

void M2006::setOutput()
{
    switch (controlMode)
    {
    case RELAX_MODE:
        speedPid.Clear();
        positionPid.Clear();
        currentSet = 0;
        break;

    case SPD_MODE:
        speedPid.ref = speedSet;
        speedPid.fdb = motorFeedback.speedFdb;
        speedPid.UpdateResult();
        currentSet = LimitCurrent(speedPid.result, maxCurrent);
        break;

    case POS_MODE:
        positionPid.ref = positionSet;
        positionPid.fdb = motorFeedback.positionFdb;
        positionPid.UpdateResult();

        speedPid.ref = positionPid.result;
        speedPid.fdb = motorFeedback.speedFdb;
        speedPid.UpdateResult();
        currentSet = LimitCurrent(speedPid.result, maxCurrent);
        break;

    default:
        speedPid.Clear();
        positionPid.Clear();
        currentSet = 0;
        break;
    }
}

M2006::MotorStateTypedef M2006::AliveCheck()
{
    if (AliveFlag == Pre_AliveFlag)
    {
        MotorState = MOTOR_OFFLINE;
        controlMode = RELAX_MODE;
        currentSet = 0;
    }
    else
    {
        MotorState = MOTOR_ONLINE;
    }

    Pre_AliveFlag = AliveFlag;
    return MotorState;
}

#include "MotorControl.hpp"

#include "DJIMotorHandler.hpp"
#include "M2006.hpp"
#include "can.h"

#ifdef MOTOR_DEBUG_ENABLED
#include "MotorDebug.hpp"
#endif

namespace
{
M2006 can1_id201_motor;
MotorControlSnapshot motor_snapshot{};

constexpr MotorSpeedPidConfig DEFAULT_SPEED_PID{
    1800.0f,
    5.0f,
    0.0f,
    500.0f,
    150.0f};

void RefreshSnapshot(const DJIMotorHandler &handler)
{
    motor_snapshot.online = can1_id201_motor.MotorState == DJIMotor::MOTOR_ONLINE;
    motor_snapshot.control_mode = static_cast<uint8_t>(can1_id201_motor.controlMode);
    motor_snapshot.speed_rpm = can1_id201_motor.motorFeedback.speed_rpm;
    motor_snapshot.speed_rad_s = can1_id201_motor.motorFeedback.speedFdb;
    motor_snapshot.current_feedback = can1_id201_motor.motorFeedback.currentFdb;
    motor_snapshot.position_rad = can1_id201_motor.motorFeedback.positionFdb;
    motor_snapshot.speed_error_rad_s = can1_id201_motor.speedPid.err[0];
    motor_snapshot.current_command = can1_id201_motor.currentSet;
    for (uint8_t i = 0U; i < 8U; ++i)
        motor_snapshot.can1_control_frame[i] = handler.can1_send_data_0[i];
}
}

void MotorControl_Init(DJIMotorHandler &handler)
{
    MotorControl_ConfigureSpeedPid(DEFAULT_SPEED_PID);
    handler.registerMotor(&can1_id201_motor, &hcan1, 0x201);
    RefreshSnapshot(handler);
}

void MotorControl_ConfigureSpeedPid(const MotorSpeedPidConfig &config)
{
    can1_id201_motor.ConfigureSpeedPid(config.kp,
                                       config.ki,
                                       config.kd,
                                       config.max_output,
                                       config.max_i_output);
}

bool MotorControl_SetOpenLoopCurrent(int16_t current)
{
    if (can1_id201_motor.MotorState != DJIMotor::MOTOR_ONLINE)
        return false;

    can1_id201_motor.SetOpenLoopCurrent(current);
    return true;
}

bool MotorControl_SetSpeedTarget(float speed_rad_s)
{
    if (can1_id201_motor.MotorState != DJIMotor::MOTOR_ONLINE)
        return false;

    can1_id201_motor.SetSpeedTarget(speed_rad_s);
    return true;
}

void MotorControl_Relax()
{
    can1_id201_motor.Relax();
}

MotorControlSnapshot MotorControl_GetSnapshot()
{
    return motor_snapshot;
}

HAL_StatusTypeDef MotorControl_Update(DJIMotorHandler &handler, uint32_t now_ticks)
{
    handler.AllMotorAliveCheck();
    RefreshSnapshot(handler);

#ifdef MOTOR_DEBUG_ENABLED
    MotorDebug_BeforeControl(now_ticks);
#else
    (void)now_ticks;
#endif

    const HAL_StatusTypeDef status = handler.updateOutputsAndSend();
    RefreshSnapshot(handler);

#ifdef MOTOR_DEBUG_ENABLED
    MotorDebug_AfterControl(status);
#endif

    return status;
}

#ifndef MOTORCONTROL_HPP
#define MOTORCONTROL_HPP

#include "main.h"

#include <cstdint>

class DJIMotorHandler;

struct MotorSpeedPidConfig
{
    float kp;
    float ki;
    float kd;
    float max_output;
    float max_i_output;
};

struct MotorControlSnapshot
{
    bool online;
    uint8_t control_mode;
    int16_t speed_rpm;
    float speed_rad_s;
    float current_feedback;
    float position_rad;
    float speed_error_rad_s;
    int16_t current_command;
    uint8_t can1_control_frame[8];
};

void MotorControl_Init(DJIMotorHandler &handler);
void MotorControl_ConfigureSpeedPid(const MotorSpeedPidConfig &config);
bool MotorControl_SetOpenLoopCurrent(int16_t current);
bool MotorControl_SetSpeedTarget(float speed_rad_s);
void MotorControl_Relax();
MotorControlSnapshot MotorControl_GetSnapshot();
HAL_StatusTypeDef MotorControl_Update(DJIMotorHandler &handler, uint32_t now_ticks);

#endif // MOTORCONTROL_HPP

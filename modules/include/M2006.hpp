//
// Created by cosmosmount on 2025/8/30.
//

#ifndef RM26_M2006_HPP
#define RM26_M2006_HPP

#include "DJIMotor.hpp"

// M2006 + C610 的固定参数。若改用其他电调或电机，在这里重新确认。
constexpr float M2006_GEAR_RATIO = 36.0f;
constexpr int16_t M2006_MAX_CURRENT_COMMAND = 10000;

/**
 * @class M2006
 * @brief M2006电机的控制类，继承自Motor类。
 *
 * 这个类实现了M2006电机的控制方法，包括输出设置等。
 */
class M2006 final : public DJIMotor
{
public:
    /**
     * @brief 构造函数，初始化M2006电机控制类。
     */
    M2006();

    /**
     * @brief 析构函数。
     */
    ~M2006() override = default;

    /**
     * @brief 实现电机输出设置。
     * 根据当前的控制模式和PID反馈调整电机输出。
     * 必须在派生类中具体实现此方法以适应具体电机。
     */
    void setOutput() override;

    MotorStateTypedef AliveCheck() override;

    void Relax();
    void SetOpenLoopCurrent(int16_t current);
    void SetSpeedTarget(float speed);
    void SetPositionTarget(float position);
    void ConfigureSpeedPid(float kp, float ki, float kd, float max_output, float max_i_output);
    void ConfigurePositionPid(float kp, float ki, float kd, float max_output, float max_i_output);
};

#endif //RM26_M2006_HPP

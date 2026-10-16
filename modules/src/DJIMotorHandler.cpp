#include "DJIMotorHandler.hpp"
#include "M2006.hpp"
#include "bsp_can.hpp"
/*
管理多台电机、打包 CAN 数据、分发反馈
中转站（控制层 PID 给到中转站转成电流形式发CAN； 反馈层原始数据处理成物理信息给控制层）和管理员（对电机先注册 写回调函数）的身份
*/
/*
上层控制线程
    │
    │ 目标速度 / 目标位置
    ▼
某台 M2006 对象
    │
    │ PID 计算
    ▼
currentSet：最终电流控制量
    │
    ▼
DJIMotorHandler
    │
    ├─ 看电机登记在 CAN1 还是 CAN2
    ├─ 看它的 CAN ID 属于 0x201~0x204，还是 0x205~0x208
    ├─ 将 currentSet 转成 CAN 规定的 2 字节有符号数
    ├─ 填入该电机在 8 字节发送包中的对应位置
    ▼
CAN_Transmit(CAN1, 0x200, 8 字节)
    │
    ▼
CAN1 总线上的所有节点都能看见这帧
    │
    ▼
ID 与槽位匹配的 C610 取走自己的两个字节
------------------------------------------------------------------
反馈层：：
真实 M2006 电机
    ↓
C610 电调
    发送原始 CAN 反馈帧
    ↓
CAN BSP 接收中断
    ↓
DJIMotorHandler
    解析编码器、rpm、电流、温度
    换算输出轴位置和角速度
    写入对应 M2006 对象的 motorFeedback
    ↓
下一轮 M2006::setOutput()
    读取 motorFeedback，继续算 PID
------------------------------------------------------------------
汇总形式如下：
目标值
    ↓
PID 算电流命令
    ↓
Handler 打包并发送
    ↓
C610 + M2006 执行
    ↓
C610 反馈真实状态
    ↓
Handler 解析并保存反馈
    ↓
PID 用反馈计算下一次命令
*/

namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr float M2006_ENCODER_TO_OUTPUT_RAD = 2.0f * PI /
                                             (static_cast<float>(DJI_MOTOR_ENCODER_RESOLUTION) * M2006_GEAR_RATIO);
constexpr float M2006_RPM_TO_OUTPUT_RAD_PER_SECOND = 2.0f * PI / (60.0f * M2006_GEAR_RATIO);

int16_t LimitCurrent(const DJIMotor *motor)
{
    uint16_t max_current = motor->maxCurrent;
    if (max_current > 32767U)
    {
        max_current = 32767U;
    }

    const int16_t current_limit = static_cast<int16_t>(max_current);
    if (motor->currentSet > current_limit)
    {
        return current_limit;
    }
    if (motor->currentSet < -current_limit)
    {
        return -current_limit;
    }
    return motor->currentSet;
}

void PackCurrent(uint8_t *data, uint8_t index, const DJIMotor *motor)
{
    if (motor == nullptr)
    {
        return;
    }

    const uint16_t current = static_cast<uint16_t>(LimitCurrent(motor));
    data[index * 2U] = static_cast<uint8_t>(current >> 8U);
    data[index * 2U + 1U] = static_cast<uint8_t>(current);
}

void MergeTransmitStatus(HAL_StatusTypeDef status, HAL_StatusTypeDef *result)
{
    if (status == HAL_ERROR)
    {
        *result = HAL_ERROR;
    }
    else if (status == HAL_BUSY && *result == HAL_OK)
    {
        *result = HAL_BUSY;
    }
}
}

void DJIMotorHandler::registerMotor(DJIMotor *motor, CAN_HandleTypeDef *hcan, uint16_t canId)
{
    if (motor == nullptr || (hcan != &hcan1 && hcan != &hcan2) ||
        canId < DJI_MOTOR_FEEDBACK_ID_MIN || canId > DJI_MOTOR_FEEDBACK_ID_MAX)
    {
        return;
    }

    const uint8_t can_index = (hcan == &hcan1) ? 0 : 1;
    const uint8_t motor_index = static_cast<uint8_t>(canId - DJI_MOTOR_FEEDBACK_ID_MIN);

    if (DJIMotorList[can_index][motor_index] != nullptr &&
        DJIMotorList[can_index][motor_index] != motor)
    {
        return;
    }

    DJIMotorList[can_index][motor_index] = motor;
    motor->hcan = hcan;
    motor->canId = canId;

    if (can_index == 0)
    {
        if (canId <= 0x204)
        {
            CAN1_0x200_Exist = true;
        }
        else
        {
            CAN1_0x1FF_Exist = true;
        }
    }
    else if (canId <= 0x204)
    {
        CAN2_0x200_Exist = true;
    }
    else
    {
        CAN2_0x1FF_Exist = true;
    }
}

HAL_StatusTypeDef DJIMotorHandler::sendControlData()
{
    for (uint8_t index = 0; index < 8; ++index)
    {
        can1_send_data_0[index] = 0;
        can1_send_data_1[index] = 0;
        can2_send_data_0[index] = 0;
        can2_send_data_1[index] = 0;
    }

    for (uint8_t index = 0; index < 4; ++index)
    {
        PackCurrent(can1_send_data_0, index, DJIMotorList[0][index]);
        PackCurrent(can1_send_data_1, index, DJIMotorList[0][index + 4]);
        PackCurrent(can2_send_data_0, index, DJIMotorList[1][index]);
        PackCurrent(can2_send_data_1, index, DJIMotorList[1][index + 4]);
    }

    HAL_StatusTypeDef result = HAL_OK;
    if (CAN1_0x200_Exist)
    {
        MergeTransmitStatus(CAN_Transmit(&hcan1, DJI_MOTOR_CONTROL_ID_1_4, can1_send_data_0, 8), &result);
    }
    if (CAN1_0x1FF_Exist)
    {
        MergeTransmitStatus(CAN_Transmit(&hcan1, DJI_MOTOR_CONTROL_ID_5_8, can1_send_data_1, 8), &result);
    }
    if (CAN2_0x200_Exist)
    {
        MergeTransmitStatus(CAN_Transmit(&hcan2, DJI_MOTOR_CONTROL_ID_1_4, can2_send_data_0, 8), &result);
    }
    if (CAN2_0x1FF_Exist)
    {
        MergeTransmitStatus(CAN_Transmit(&hcan2, DJI_MOTOR_CONTROL_ID_5_8, can2_send_data_1, 8), &result);
    }

    return result;
}

HAL_StatusTypeDef DJIMotorHandler::updateOutputsAndSend()
{
    for (uint8_t can_index = 0U; can_index < 2U; ++can_index)
    {
        for (uint8_t motor_index = 0U; motor_index < 8U; ++motor_index)
        {
            DJIMotor *motor = DJIMotorList[can_index][motor_index];
            if (motor != nullptr)
                motor->setOutput();
        }
    }

    return sendControlData();
}

void DJIMotorHandler::updateFeedback(CAN_HandleTypeDef *hcan, uint8_t *rx_data, int index)
{
    if ((hcan != &hcan1 && hcan != &hcan2) || rx_data == nullptr || index < 0 || index >= 8)
    {
        return;
    }

    const uint8_t can_index = (hcan == &hcan1) ? 0 : 1;
    DJIMotor *motor = DJIMotorList[can_index][index];
    if (motor != nullptr)
    {
        UpdateSensorData(motor, rx_data);
    }
}

void DJIMotorHandler::UpdateSensorData(DJIMotor *motor, uint8_t *can_data)
{
    if (motor == nullptr || can_data == nullptr || motor->gearBox != GearBox_M2006)
    {
        return;
    }

    const uint16_t encoder = static_cast<uint16_t>(can_data[0] << 8U) | can_data[1];
    const int16_t speed_rpm = static_cast<int16_t>(static_cast<uint16_t>(can_data[2] << 8U) | can_data[3]);
    const int16_t current = static_cast<int16_t>(static_cast<uint16_t>(can_data[4] << 8U) | can_data[5]);

    DJIMotor::MotorFeedBack &feedback = motor->motorFeedback;
    feedback.lastSpeedFdb = feedback.speedFdb;
    feedback.speed_rpm = speed_rpm;
    feedback.speedFdb = static_cast<float>(speed_rpm) * M2006_RPM_TO_OUTPUT_RAD_PER_SECOND;
    feedback.currentFdb = static_cast<float>(current);
    feedback.temperatureFdb = static_cast<float>(can_data[6]);

    if (motor->AliveFlag == 0)
    {
        feedback.ecd = encoder;
        feedback.last_ecd = static_cast<int16_t>(encoder);
        feedback.positionFdb = 0.0f;
        feedback.lastPositionFdb = 0.0f;
    }
    else
    {
        int16_t encoder_delta = static_cast<int16_t>(encoder - feedback.ecd);
        if (encoder_delta > static_cast<int16_t>(DJI_MOTOR_ENCODER_RESOLUTION / 2))
        {
            encoder_delta -= static_cast<int16_t>(DJI_MOTOR_ENCODER_RESOLUTION);
        }
        else if (encoder_delta < -static_cast<int16_t>(DJI_MOTOR_ENCODER_RESOLUTION / 2))
        {
            encoder_delta += static_cast<int16_t>(DJI_MOTOR_ENCODER_RESOLUTION);
        }

        feedback.last_ecd = static_cast<int16_t>(feedback.ecd);
        feedback.ecd = encoder;
        feedback.lastPositionFdb = feedback.positionFdb;
        feedback.positionFdb += static_cast<float>(encoder_delta) * M2006_ENCODER_TO_OUTPUT_RAD;
    }

    ++motor->AliveFlag;
}

void DJIMotorHandler::AllMotorAliveCheck()
{
    for (uint8_t can_index = 0; can_index < 2; ++can_index)
    {
        for (uint8_t motor_index = 0; motor_index < 8; ++motor_index)
        {
            DJIMotor *motor = DJIMotorList[can_index][motor_index];
            if (motor != nullptr)
            {
                motor->AliveCheck();
            }
        }
    }
}

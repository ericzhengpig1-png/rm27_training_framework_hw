#ifndef BSP_CAN_H
#define BSP_CAN_H

#include "main.h"
#include "can.h"

using CAN_RxCallback = void (*)(CAN_HandleTypeDef *hcan,
                                const CAN_RxHeaderTypeDef *header,
                                const uint8_t *data);

/**
 * @brief 初始化CAN滤波器配置。
 * 设置CAN硬件的滤波器，用于优化接收数据的处理。
 * 更多信息，请参考原文，链接：https://blog.csdn.net/weixin_54448108/article/details/128570593
 */
void CAN_Init(void);

/**
 * @brief 注册 CAN RX FIFO0 标准数据帧处理函数。
 * @note 回调在中断上下文中执行，数据指针仅在回调期间有效。
 */
void CAN_RegisterRxCallback(CAN_RxCallback callback);

/**
 * @brief 发送CAN数据。
 * @param hcan 指向CAN句柄的指针，用于配置CAN传输。
 * @param StdId CAN消息的标准标识符
 * @param msg 发送的数据
 * @param len 数据长度
 * @retval HAL_OK 已提交发送；HAL_BUSY 表示发送邮箱满；HAL_ERROR 表示参数或状态无效
 * @note 该函数用于发送CAN数据，目前只支持标准帧
 */
HAL_StatusTypeDef CAN_Transmit(CAN_HandleTypeDef *hcan, uint32_t Id, uint8_t *msg, uint16_t len);

#endif // BSP_CAN_HPP

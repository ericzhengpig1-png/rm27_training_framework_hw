//
// Created by cosmosmount on 2025/9/2.
//

#ifndef RM26_H7_BSP_PWM_HPP
#define RM26_H7_BSP_PWM_HPP

#include "tim.h"

/**
 * @brief  PWM初始化
 */
void PWM_Init(void);

/**
 * @brief  PWM启动，只是对HAL库函数的形式上的封装
 */
HAL_StatusTypeDef PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel);

/**
 * @brief  PWM停止，只是对HAL库函数的形式上的封装
 */
HAL_StatusTypeDef PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel);

/**
 * @brief  设置PWM周期
 * @param  htim: 定时器句柄
 * @param  period: 周期，单位为秒
 * @return 设置周期状态
 */
HAL_StatusTypeDef PWM_SetPeriod(TIM_HandleTypeDef *htim, float period);

/**
 * @brief  设置PWM占空比
 * @param  htim: 定时器句柄
 * @param  dutyratio: 占空比，0~1
 * @param  channel: PWM通道
 * @return 设置占空比状态
 */
HAL_StatusTypeDef PWM_SetDutyRatio(TIM_HandleTypeDef *htim, float dutyratio, uint32_t channel);

#endif //RM26_H7_BSP_PWM_HPP

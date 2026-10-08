//
// Created by cosmosmount on 2025/9/2.
//

#include "bsp_pwm.hpp"

static bool PWM_IsSupportedTimer(TIM_HandleTypeDef *htim)
{
    if (htim == &htim8) return htim->Instance == TIM8;
    if (htim == &htim10) return htim->Instance == TIM10;

    return false;
}

static uint32_t PWM_GetTimerClock(void)
{
    uint32_t timer_clock = HAL_RCC_GetPCLK2Freq();

    if ((RCC->CFGR & RCC_CFGR_PPRE2) != 0U) timer_clock *= 2U;

    return timer_clock;
}

void PWM_Init(void)
{
    // TODO: 根据实际使用的定时器和通道完成 PWM 初始化。
}

HAL_StatusTypeDef PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    if (!PWM_IsSupportedTimer(htim) || Channel != TIM_CHANNEL_1) return HAL_ERROR;

    return HAL_TIM_PWM_Start(htim, Channel);
}

HAL_StatusTypeDef PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    if (!PWM_IsSupportedTimer(htim) || Channel != TIM_CHANNEL_1) return HAL_ERROR;

    return HAL_TIM_PWM_Stop(htim, Channel);
}

HAL_StatusTypeDef PWM_SetPeriod(TIM_HandleTypeDef *htim, float period_s)
{
    if (!PWM_IsSupportedTimer(htim) || !(period_s > 0.0f)) return HAL_ERROR;

    uint32_t counter_clock = PWM_GetTimerClock() / (htim->Instance->PSC + 1U);
    float max_period_s = 65536.0f / static_cast<float>(counter_clock);
    if (period_s > max_period_s) return HAL_ERROR;

    uint32_t period_counts = static_cast<uint32_t>(period_s * static_cast<float>(counter_clock) + 0.5f);
    if (period_counts == 0U || period_counts > 65536U) return HAL_ERROR;

    uint32_t old_period_counts = __HAL_TIM_GET_AUTORELOAD(htim) + 1U;
    uint32_t old_compare = __HAL_TIM_GET_COMPARE(htim, TIM_CHANNEL_1);
    uint32_t new_compare = static_cast<uint32_t>(
        (static_cast<uint64_t>(old_compare) * period_counts + old_period_counts / 2U) / old_period_counts);

    if (new_compare > period_counts) new_compare = period_counts;
    if (new_compare > 65535U) new_compare = 65535U;

    __HAL_TIM_SET_AUTORELOAD(htim, period_counts - 1U);
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, new_compare);
    htim->Init.Period = period_counts - 1U;

    return HAL_OK;
}

HAL_StatusTypeDef PWM_SetDutyRatio(TIM_HandleTypeDef *htim, float dutyratio, uint32_t channel)
{
    if (!PWM_IsSupportedTimer(htim) || channel != TIM_CHANNEL_1) return HAL_ERROR;
    if (!(dutyratio >= 0.0f && dutyratio <= 1.0f)) return HAL_ERROR;

    uint32_t period_counts = __HAL_TIM_GET_AUTORELOAD(htim) + 1U;
    uint32_t compare = static_cast<uint32_t>(dutyratio * static_cast<float>(period_counts) + 0.5f);
    if (compare > 65535U) compare = 65535U;

    __HAL_TIM_SET_COMPARE(htim, channel, compare);

    return HAL_OK;
}

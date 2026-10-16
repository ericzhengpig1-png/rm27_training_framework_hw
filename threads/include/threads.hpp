#ifndef THREADS_HPP
#define THREADS_HPP

#include "tx_api.h"

#ifdef __cplusplus
#include "IMU.hpp"
#include "bsp_can.hpp"

struct ImuDataSnapshot
{
    acc_data_t acceleration{}; // temperature is included in this structure
    gyro_data_t angular_velocity{};
    bool initialized = false;
    bool acceleration_valid = false;
    bool temperature_valid = false;
    bool angular_velocity_valid = false;
};

struct CanRxSnapshot
{
    CAN_HandleTypeDef *can = nullptr;
    uint32_t standard_id = 0U;
    uint8_t data[8]{};
    uint8_t length = 0U;
    ULONG received_frames = 0U;
    ULONG dropped_frames = 0U;
    bool initialized = false;
    bool has_frame = false;
};

extern "C" {
#endif

UINT Threads_Init(VOID *memory_ptr);

#ifdef __cplusplus
/**
 * @brief Copy the latest IMU data and status in thread context.
 * @return TX_SUCCESS when copied; a ThreadX error otherwise.
 */
UINT Threads_GetIMUData(ImuDataSnapshot *snapshot);

/**
 * @brief Copy the latest raw CAN frame and receive status in thread context.
 * @return TX_SUCCESS when copied; a ThreadX error otherwise.
 */
UINT Threads_GetCANRxSnapshot(CanRxSnapshot *snapshot);
#endif

#ifdef __cplusplus
}
#endif

#endif // THREADS_HPP

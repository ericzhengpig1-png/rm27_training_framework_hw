#include "threads.hpp"

#include "BMI088.hpp"
#include "DJIMotorHandler.hpp"
#include "main.h"

namespace
{
    constexpr ULONG IMU_THREAD_STACK_SIZE = 1024U;
    constexpr UINT IMU_THREAD_PRIORITY = 10U;
    constexpr ULONG CONTROL_THREAD_STACK_SIZE = 1024U;
    constexpr UINT CONTROL_THREAD_PRIORITY = 10U;
    constexpr ULONG ALIVE_THREAD_STACK_SIZE = 1024U;
    constexpr UINT ALIVE_THREAD_PRIORITY = 11U;
    constexpr UINT CAN_RX_QUEUE_CAPACITY = 16U;
    constexpr ULONG CONTROL_PERIOD_TICKS = 10U;
    constexpr ULONG ALIVE_MONITOR_PERIOD_TICKS = 10U;

    struct CanRxMessage
    {
        CAN_HandleTypeDef *can;
        uint32_t standard_id;
        uint32_t length;
        uint8_t data[8];
    };

    static_assert(sizeof(CanRxMessage) % sizeof(ULONG) == 0U,
                  "CAN queue messages must use whole ThreadX words");
    constexpr UINT CAN_RX_QUEUE_MESSAGE_WORDS = static_cast<UINT>(sizeof(CanRxMessage) / sizeof(ULONG));

    CHAR imu_thread_name[] = "IMU Thread";
    CHAR imu_data_mutex_name[] = "IMU Data Mutex";
    CHAR control_thread_name[] = "Control Thread";
    CHAR alive_thread_name[] = "Alive Thread";
    CHAR can_rx_queue_name[] = "CAN RX Queue";
    CHAR can_rx_snapshot_mutex_name[] = "CAN RX Snapshot Mutex";
    CHAR imu_heartbeat_semaphore_name[] = "IMU Heartbeat";
    CHAR control_heartbeat_semaphore_name[] = "Control Heartbeat";
    TX_THREAD imu_thread;
    TX_THREAD control_thread;
    TX_THREAD alive_thread;
    VOID *imu_thread_stack = nullptr;
    VOID *control_thread_stack = nullptr;
    VOID *alive_thread_stack = nullptr;
    TX_MUTEX imu_data_mutex;
    TX_MUTEX can_rx_snapshot_mutex;
    TX_QUEUE can_rx_queue;
    TX_SEMAPHORE imu_heartbeat_semaphore;
    TX_SEMAPHORE control_heartbeat_semaphore;
    ULONG can_rx_queue_storage[CAN_RX_QUEUE_CAPACITY * CAN_RX_QUEUE_MESSAGE_WORDS];
    ImuDataSnapshot imu_data_snapshot{};
    CanRxSnapshot can_rx_snapshot{};
    volatile ULONG can_rx_dropped_frames = 0U;
    BMI088::cBMI088 bmi088;

    void SignalHeartbeat(TX_SEMAPHORE *semaphore)
    {
        // Keep only the latest signal so an old heartbeat cannot mask a stopped thread.
        tx_semaphore_get(semaphore, TX_NO_WAIT);
        tx_semaphore_put(semaphore);
    }

    bool ConsumeHeartbeat(TX_SEMAPHORE *semaphore)
    {
        return tx_semaphore_get(semaphore, TX_NO_WAIT) == TX_SUCCESS;
    }

    void SetAliveLED(bool healthy)
    {
        HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, healthy ? GPIO_PIN_RESET : GPIO_PIN_SET);
        HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, healthy ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET);
    }

    void PublishIMUData()
    {
        if (tx_mutex_get(&imu_data_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
            return;

        imu_data_snapshot.acceleration = bmi088.acc_data;
        imu_data_snapshot.angular_velocity = bmi088.gyro_data;
        imu_data_snapshot.initialized = !bmi088.self_test.INIT_ERR;
        imu_data_snapshot.acceleration_valid = !bmi088.self_test.INIT_ERR && !bmi088.self_test.ACC_DATA_ERR;
        imu_data_snapshot.angular_velocity_valid = !bmi088.self_test.INIT_ERR && !bmi088.self_test.GYRO_DATA_ERR;

        tx_mutex_put(&imu_data_mutex);
    }

    void ImuThreadEntry(ULONG input)
    {
        (void)input;

        bmi088.Config();
        bmi088.VerifyAccChipID();
        bmi088.VerifyGyroChipID();
        PublishIMUData();

        while (true)
        {
            if (!bmi088.self_test.INIT_ERR)
            {
                bmi088.ReadAccData(&bmi088.acc_data);
                bmi088.VerifyAccData();
                bmi088.ReadGyroData(&bmi088.gyro_data);
                bmi088.VerifyGyroData();
                PublishIMUData();
            }

            SignalHeartbeat(&imu_heartbeat_semaphore);
            tx_thread_sleep(1U);
        }
    }

    void CanRxCallback(CAN_HandleTypeDef *can,
                       const CAN_RxHeaderTypeDef *header,
                       const uint8_t *data)
    {
        if (can == nullptr || header == nullptr || data == nullptr || header->DLC > 8U)
            return;

        CanRxMessage message{};
        message.can = can;
        message.standard_id = header->StdId;
        message.length = header->DLC;
        for (uint32_t i = 0U; i < message.length; ++i)
            message.data[i] = data[i];

        // This callback runs in the CAN interrupt; never wait for the queue.
        if (tx_queue_send(&can_rx_queue, &message, TX_NO_WAIT) != TX_SUCCESS)
            ++can_rx_dropped_frames;
    }

    void PublishCANRxSnapshot(const CanRxMessage *message)
    {
        if (tx_mutex_get(&can_rx_snapshot_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
            return;

        can_rx_snapshot.dropped_frames = can_rx_dropped_frames;
        if (message != nullptr)
        {
            can_rx_snapshot.can = message->can;
            can_rx_snapshot.standard_id = message->standard_id;
            can_rx_snapshot.length = static_cast<uint8_t>(message->length);
            for (uint32_t i = 0U; i < 8U; ++i)
                can_rx_snapshot.data[i] = i < message->length ? message->data[i] : 0U;
            ++can_rx_snapshot.received_frames;
            can_rx_snapshot.has_frame = true;
        }

        tx_mutex_put(&can_rx_snapshot_mutex);
    }

    void DispatchMotorFeedback(DJIMotorHandler *handler, CanRxMessage *message)
    {
        if (handler == nullptr || message == nullptr || message->can == nullptr ||
            message->length != 8U ||
            message->standard_id < DJI_MOTOR_FEEDBACK_ID_MIN ||
            message->standard_id > DJI_MOTOR_FEEDBACK_ID_MAX)
        {
            return;
        }

        const int motor_index = static_cast<int>(message->standard_id - DJI_MOTOR_FEEDBACK_ID_MIN);
        handler->updateFeedback(message->can, message->data, motor_index);
    }

    void UpdateRegisteredMotorOutputs(DJIMotorHandler *handler)
    {
        if (handler == nullptr)
            return;

        for (uint8_t can_index = 0U; can_index < 2U; ++can_index)
        {
            for (uint8_t motor_index = 0U; motor_index < 8U; ++motor_index)
            {
                DJIMotor *motor = handler->DJIMotorList[can_index][motor_index];
                if (motor != nullptr)
                    motor->setOutput();
            }
        }

        handler->sendControlData();
    }

    void ControlThreadEntry(ULONG input)
    {
        (void)input;

        DJIMotorHandler *const motor_handler = DJIMotorHandler::Instance();
        CAN_RegisterRxCallback(CanRxCallback);
        const HAL_StatusTypeDef can_init_status = CAN_Init();
        if (tx_mutex_get(&can_rx_snapshot_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
        {
            can_rx_snapshot.initialized = can_init_status == HAL_OK;
            tx_mutex_put(&can_rx_snapshot_mutex);
        }

        while (true)
        {
            bool received_frame = false;
            for (UINT frame_index = 0U; frame_index < CAN_RX_QUEUE_CAPACITY; ++frame_index)
            {
                CanRxMessage message{};
                const UINT status = tx_queue_receive(&can_rx_queue, &message, TX_NO_WAIT);
                if (status == TX_QUEUE_EMPTY)
                    break;

                if (status != TX_SUCCESS)
                    break;

                received_frame = true;
                PublishCANRxSnapshot(&message);
                DispatchMotorFeedback(motor_handler, &message);
            }

            if (!received_frame)
                PublishCANRxSnapshot(nullptr);

            if (can_init_status == HAL_OK)
            {
                // The Control thread owns motor feedback, PID output and online checks.
                motor_handler->AllMotorAliveCheck();
                UpdateRegisteredMotorOutputs(motor_handler);
            }

            SignalHeartbeat(&control_heartbeat_semaphore);
            tx_thread_sleep(CONTROL_PERIOD_TICKS);
        }
    }

    void AliveThreadEntry(ULONG input)
    {
        (void)input;

        // Let IMU and CAN initialization finish before checking the first status.
        tx_thread_sleep(ALIVE_MONITOR_PERIOD_TICKS);

        while (true)
        {
            ImuDataSnapshot imu_snapshot{};
            CanRxSnapshot can_snapshot{};
            const bool imu_status_valid = Threads_GetIMUData(&imu_snapshot) == TX_SUCCESS;
            const bool can_status_valid = Threads_GetCANRxSnapshot(&can_snapshot) == TX_SUCCESS;
            const bool imu_alive = ConsumeHeartbeat(&imu_heartbeat_semaphore);
            const bool control_alive = ConsumeHeartbeat(&control_heartbeat_semaphore);
            const bool healthy = imu_status_valid && can_status_valid &&
                                 imu_snapshot.initialized && can_snapshot.initialized &&
                                 imu_alive && control_alive;

            SetAliveLED(healthy);
            tx_thread_sleep(ALIVE_MONITOR_PERIOD_TICKS);
        }
    }
}

extern "C" UINT Threads_Init(VOID *memory_ptr)
{
    if (memory_ptr == nullptr)
        return TX_PTR_ERROR;

    auto *byte_pool = static_cast<TX_BYTE_POOL *>(memory_ptr);
    bool imu_mutex_created = false;
    bool can_mutex_created = false;
    bool can_queue_created = false;
    bool imu_heartbeat_created = false;
    bool control_heartbeat_created = false;
    bool imu_thread_created = false;
    bool control_thread_created = false;
    bool alive_thread_created = false;

    auto cleanup = [&]()
    {
        if (alive_thread_created)
            tx_thread_delete(&alive_thread);
        if (control_thread_created)
            tx_thread_delete(&control_thread);
        if (imu_thread_created)
            tx_thread_delete(&imu_thread);
        if (alive_thread_stack != nullptr)
            tx_byte_release(alive_thread_stack);
        if (control_thread_stack != nullptr)
            tx_byte_release(control_thread_stack);
        if (imu_thread_stack != nullptr)
            tx_byte_release(imu_thread_stack);
        if (control_heartbeat_created)
            tx_semaphore_delete(&control_heartbeat_semaphore);
        if (imu_heartbeat_created)
            tx_semaphore_delete(&imu_heartbeat_semaphore);
        if (can_queue_created)
            tx_queue_delete(&can_rx_queue);
        if (can_mutex_created)
            tx_mutex_delete(&can_rx_snapshot_mutex);
        if (imu_mutex_created)
            tx_mutex_delete(&imu_data_mutex);
    };

    UINT status = tx_mutex_create(&imu_data_mutex, imu_data_mutex_name, TX_INHERIT);
    if (status != TX_SUCCESS)
        return status;
    imu_mutex_created = true;

    status = tx_mutex_create(&can_rx_snapshot_mutex, can_rx_snapshot_mutex_name, TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    can_mutex_created = true;

    status = tx_queue_create(&can_rx_queue,
                             can_rx_queue_name,
                             CAN_RX_QUEUE_MESSAGE_WORDS,
                             can_rx_queue_storage,
                             sizeof(can_rx_queue_storage));
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    can_queue_created = true;

    status = tx_semaphore_create(&imu_heartbeat_semaphore, imu_heartbeat_semaphore_name, 0U);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    imu_heartbeat_created = true;

    status = tx_semaphore_create(&control_heartbeat_semaphore, control_heartbeat_semaphore_name, 0U);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    control_heartbeat_created = true;

    status = tx_byte_allocate(byte_pool, &imu_thread_stack, IMU_THREAD_STACK_SIZE, TX_NO_WAIT);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }

    status = tx_byte_allocate(byte_pool, &control_thread_stack, CONTROL_THREAD_STACK_SIZE, TX_NO_WAIT);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }

    status = tx_byte_allocate(byte_pool, &alive_thread_stack, ALIVE_THREAD_STACK_SIZE, TX_NO_WAIT);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }

    status = tx_thread_create(&imu_thread,
                              imu_thread_name,
                              ImuThreadEntry,
                              0U,
                              imu_thread_stack,
                              IMU_THREAD_STACK_SIZE,
                              IMU_THREAD_PRIORITY,
                              IMU_THREAD_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    imu_thread_created = true;

    status = tx_thread_create(&control_thread,
                              control_thread_name,
                              ControlThreadEntry,
                              0U,
                              control_thread_stack,
                              CONTROL_THREAD_STACK_SIZE,
                              CONTROL_THREAD_PRIORITY,
                              CONTROL_THREAD_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    control_thread_created = true;

    status = tx_thread_create(&alive_thread,
                              alive_thread_name,
                              AliveThreadEntry,
                              0U,
                              alive_thread_stack,
                              ALIVE_THREAD_STACK_SIZE,
                              ALIVE_THREAD_PRIORITY,
                              ALIVE_THREAD_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        cleanup();
        return status;
    }
    alive_thread_created = true;

    return status;
}

extern "C" UINT Threads_GetIMUData(ImuDataSnapshot *snapshot)
{
    if (snapshot == nullptr)
        return TX_PTR_ERROR;

    UINT status = tx_mutex_get(&imu_data_mutex, TX_WAIT_FOREVER);
    if (status != TX_SUCCESS)
        return status;

    *snapshot = imu_data_snapshot;
    return tx_mutex_put(&imu_data_mutex);
}

extern "C" UINT Threads_GetCANRxSnapshot(CanRxSnapshot *snapshot)
{
    if (snapshot == nullptr)
        return TX_PTR_ERROR;

    UINT status = tx_mutex_get(&can_rx_snapshot_mutex, TX_WAIT_FOREVER);
    if (status != TX_SUCCESS)
        return status;

    *snapshot = can_rx_snapshot;
    return tx_mutex_put(&can_rx_snapshot_mutex);
}

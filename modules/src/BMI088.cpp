#include "BMI088.hpp"
#include "spi.h"
#include "tim.h"
#include "tx_api.h"
#include "bsp_pwm.hpp"
#include <cmath>

namespace BMI088
{
    namespace
    {
        constexpr uint32_t BMI088_SPI_TIMEOUT = 10U;

        HAL_StatusTypeDef SetChipSelect(BMI088_SENSOR cs, GPIO_PinState state)
        {
            switch (cs)
            {
            case BMI088_CS_ACC:  // 拉低ACC_CS引脚 选中
                HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, state);
                return HAL_OK;

            case BMI088_CS_GYRO:
                HAL_GPIO_WritePin(GYRO_CS_GPIO_Port, GYRO_CS_Pin, state);
                return HAL_OK;

            default:
                return HAL_ERROR;
            }
        }

        int16_t ParseSigned16(const uint8_t *data)
        {
            const uint16_t value = static_cast<uint16_t>(data[0]) |
                                   (static_cast<uint16_t>(data[1]) << 8U);
            return static_cast<int16_t>(value);
        }
    }

    /**
     * @brief BMI088 acc gyro 标定
     * @note 标定后的陀螺仪零偏存储在 Gyro_offset 中。
     * @attention 不管工作模式是blocking还是IT,标定时都是blocking模式,所以不用担心中断关闭后无法标定(RobotInit关闭了全局中断)
     * @attention 标定精度和等待时间有关。
     * @todo 将标定次数(等待时间)变为参数供设定
     * @section 整体流程为1.累加加速度数据计算gNrom()
     *                   2.累加陀螺仪数据计算零飘
     *                   3. 如果标定过程运动幅度过大,重新标定
     *                   4.保存标定参数
     */
    void cBMI088::Calibrate()
    {
        /*
        为什么标定： 当 IMU 静止时，陀螺仪理论输出应该是 0。
                    实际上通常会有一点固定偏差，因此连续采样很多次取平均，
                    把平均值保存为 Gyro_offset；之后每次读陀螺仪数据时都减掉这个偏差。
        怎么标定：

        保存旧零偏
            ↓
        临时把零偏设为 0
            ↓
        连续读取 4000 次陀螺仪数据
            ↓
        任一次读取无效？
        ├─ 是：恢复旧零偏，CALIBRATE_ERR = true，结束
        └─ 否：计算三轴平均值
                    ↓
                任一平均零偏 > 0.1 rad/s？
                ├─ 是：恢复旧零偏，CALIBRATE_ERR = true
                └─ 否：保存新的平均零偏，CALIBRATE_ERR = false
        */

        const int calib_samples = 4000; // 采样次数
        float gyro_sum[3] = {0.0f, 0.0f, 0.0f};
        gyro_data_t temp_gyro = {};
        const float previous_offset[3] = {Gyro_offset[0], Gyro_offset[1], Gyro_offset[2]};

        // 标定时先暂时清除旧零偏，得到真实静止偏置。
        Gyro_offset[0] = 0.0f;
        Gyro_offset[1] = 0.0f;
        Gyro_offset[2] = 0.0f;

        for (int i = 0; i < calib_samples; i++)
        {
            ReadGyroData(&temp_gyro);
            VerifyGyroData();
            if (self_test.GYRO_DATA_ERR)
            {
                Gyro_offset[0] = previous_offset[0];
                Gyro_offset[1] = previous_offset[1];
                Gyro_offset[2] = previous_offset[2];
                self_test.CALIBRATE_ERR = true;
                return;
            }

            gyro_sum[0] += temp_gyro.x;
            gyro_sum[1] += temp_gyro.y;
            gyro_sum[2] += temp_gyro.z;

            tx_thread_sleep(1);
        }

        Gyro_offset[0] = gyro_sum[0] / calib_samples;
        Gyro_offset[1] = gyro_sum[1] / calib_samples;
        Gyro_offset[2] = gyro_sum[2] / calib_samples;

        if (std::fabs(Gyro_offset[0]) > 0.1f ||
            std::fabs(Gyro_offset[1]) > 0.1f ||
            std::fabs(Gyro_offset[2]) > 0.1f)
        {
            self_test.CALIBRATE_ERR = true;
            Gyro_offset[0] = previous_offset[0];
            Gyro_offset[1] = previous_offset[1];
            Gyro_offset[2] = previous_offset[2];
        }
        else
        {
            self_test.CALIBRATE_ERR = false;
        }
    }

    void cBMI088::TemperatureControl(float target_temp)
    {
        float current_temp = 0.0f;
        ReadAccTemperature(&current_temp);

        if (!TemperatureReadOk || !std::isfinite(target_temp) || !std::isfinite(current_temp))
        {
            TempPid.Clear();
            PWM_SetDutyRatio(&HEATING_RESISTANCE_TIM, 0.0f, HEATING_RESISTANCE_CHANNEL);
            self_test.TEMP_CTRL_ERR = true;
            return;
        }

        TempPid.ref = target_temp;
        TempPid.fdb = current_temp;
        TempPid.UpdateResult();

        const float duty_ratio = TempPid.result > 0.0f ? TempPid.result : 0.0f;
        if (PWM_SetDutyRatio(&HEATING_RESISTANCE_TIM, duty_ratio, HEATING_RESISTANCE_CHANNEL) != HAL_OK)
        {
            TempPid.Clear();
            PWM_SetDutyRatio(&HEATING_RESISTANCE_TIM, 0.0f, HEATING_RESISTANCE_CHANNEL);
            self_test.TEMP_CTRL_ERR = true;
            return;
        }

        if (!HeatingPwmStarted)
        {
            if (PWM_Start(&HEATING_RESISTANCE_TIM, HEATING_RESISTANCE_CHANNEL) != HAL_OK)
            {
                TempPid.Clear();
                PWM_SetDutyRatio(&HEATING_RESISTANCE_TIM, 0.0f, HEATING_RESISTANCE_CHANNEL);
                self_test.TEMP_CTRL_ERR = true;
                return;
            }

            HeatingPwmStarted = true;
        }

        self_test.TEMP_CTRL_ERR = false;
    }
    /*
    与原来代码差别：
    ReadReg()→ 自己处理 BMI088 加速度计需要丢弃的无效字节

    VerifyAccChipID()→ 只拿到真正的 chip_id
                     → 不需要知道 Dummy byte 的细节
    */
    void cBMI088::VerifyAccChipID()
    {
        uint8_t chip_id = 0;

        const HAL_StatusTypeDef status = ReadReg(BMI088_CS_ACC, ACC_CHIP_ID_ADDR, &chip_id, 1);
        tx_thread_sleep(1);

        self_test.ACC_CHIP_ID_ERR = (status != HAL_OK || chip_id != ACC_CHIP_ID_VAL);
        self_test.INIT_ERR = !ConfigOk || self_test.ACC_CHIP_ID_ERR || self_test.GYRO_CHIP_ID_ERR;
    }

    void cBMI088::VerifyGyroChipID()
    {
        uint8_t chip_id = 0;
        const HAL_StatusTypeDef status = ReadReg(BMI088_CS_GYRO, GYRO_CHIP_ID_ADDR, &chip_id, 1);
        tx_thread_sleep(1);

        self_test.GYRO_CHIP_ID_ERR = (status != HAL_OK || chip_id != GYRO_CHIP_ID_VAL);
        self_test.INIT_ERR = !ConfigOk || self_test.ACC_CHIP_ID_ERR || self_test.GYRO_CHIP_ID_ERR;
    }

    void cBMI088::VerifyAccData()
    {
        const float max_acceleration = 32768.0f * IMU_ACCEL_6G_SEN;
        self_test.ACC_DATA_ERR = !AccReadOk ||
                                  !std::isfinite(acc_data.x) ||
                                  !std::isfinite(acc_data.y) ||
                                  !std::isfinite(acc_data.z) ||
                                  std::fabs(acc_data.x) > max_acceleration ||
                                  std::fabs(acc_data.y) > max_acceleration ||
                                  std::fabs(acc_data.z) > max_acceleration;
    }

    void cBMI088::VerifyGyroData()
    {
        const float max_angular_rate = 32768.0f * IMU_GYRO_2000_SEN;
        self_test.GYRO_DATA_ERR = !GyroReadOk ||
                                   !std::isfinite(gyro_data.x) ||
                                   !std::isfinite(gyro_data.y) ||
                                   !std::isfinite(gyro_data.z) ||
                                   std::fabs(gyro_data.x) > max_angular_rate ||
                                   std::fabs(gyro_data.y) > max_angular_rate ||
                                   std::fabs(gyro_data.z) > max_angular_rate;
    }

    HAL_StatusTypeDef cBMI088::WriteReg(enum BMI088_SENSOR cs, uint8_t addr, const uint8_t *data, uint8_t len)
    {
        if (data == nullptr || len == 0)
            return HAL_ERROR;

        HAL_StatusTypeDef status = SetChipSelect(cs, GPIO_PIN_RESET);
        if (status != HAL_OK)
            return status;

        uint8_t write_addr = addr & BMI088_SPI_WRITE_CODE;
        status = HAL_SPI_Transmit(&hspi1, &write_addr, 1, BMI088_SPI_TIMEOUT);
        if (status == HAL_OK)
            status = HAL_SPI_Transmit(&hspi1, const_cast<uint8_t *>(data), len, BMI088_SPI_TIMEOUT);

        SetChipSelect(cs, GPIO_PIN_SET);
        return status;
    }

    HAL_StatusTypeDef cBMI088::ReadReg(enum BMI088_SENSOR cs, uint8_t addr, uint8_t *data, uint8_t len)
    {
        if (data == nullptr || len == 0)  // 地址接收
            return HAL_ERROR;

        HAL_StatusTypeDef status = SetChipSelect(cs, GPIO_PIN_RESET);  // 拉低 ACC_CS
        if (status != HAL_OK)
            return status;

        // 最高位为1 是读操作 (spi)
        uint8_t read_addr = addr | BMI088_SPI_READ_CODE;     // 发送读取命令 0x22（固定：芯片内部哪个位置开始读数据） | 0x80
        status = HAL_SPI_Transmit(&hspi1, &read_addr, 1, BMI088_SPI_TIMEOUT);
        // 使用SPI1外设 要发送的地址、读命令 发送1个字节 最多等待多久ms
        if (status == HAL_OK && cs == BMI088_CS_ACC)  // 加速度计 SPI 协议要求先接收掉 1 个 dummy byte
        {
            uint8_t dummy_byte = 0;
            status = HAL_SPI_Receive(&hspi1, &dummy_byte, 1, BMI088_SPI_TIMEOUT);
        }

        if (status == HAL_OK)  // 把传感器寄存器中的数据，接收到 MCU 内存的 data 里
            status = HAL_SPI_Receive(&hspi1, data, len, BMI088_SPI_TIMEOUT);

        SetChipSelect(cs, GPIO_PIN_SET);  // 重新拉高 ACC_CS
        return status;
    }

    void cBMI088::Config()
    {
        ConfigOk = true;
        tx_thread_sleep(10); //< 等待系统稳定

        const auto write_config = [this](BMI088_SENSOR cs, uint8_t addr, uint8_t value)
        {
            if (WriteReg(cs, addr, &value, 1) == HAL_OK)
                return true;

            ConfigOk = false;
            self_test.INIT_ERR = true;
            return false;
        };

        /*-------------------------------------加速度计初始化-------------------------------------*/

        //< 先软重启，清空所有寄存器
        if (!write_config(BMI088_CS_ACC, ACC_SOFTRESET_ADDR, ACC_SOFTRESET_VAL)) return;
        tx_thread_sleep(100); //< 延时100ms,重启需要时间

        //< 打开加速度计电源
        if (!write_config(BMI088_CS_ACC, ACC_PWR_CTRL_ADDR, ACC_PWR_CTRL_ON)) return;
        tx_thread_sleep(150);

        //< 加速度计变成正常模式
        if (!write_config(BMI088_CS_ACC, ACC_PWR_CONF_ADDR, ACC_PWR_CONF_ACT)) return;
        tx_thread_sleep(10); //

        //< 测量范围
        if (!write_config(BMI088_CS_ACC, ACC_RANGE_ADDR, ACC_RANGE_6G)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_ACC, ACC_CONF_ADDR, ACC_CONF_BWP_NORM | ACC_CONF_ODR_800_Hz)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_ACC, INT1_IO_CTRL_ADDR, 0x08)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_ACC, INT_MAP_DATA_ADDR, 0x04)) return;
        tx_thread_sleep(5); //< 延时5ms

        /*-------------------------------------陀螺仪初始化-------------------------------------*/
        //< 先软重启，清空所有寄存器
        if (!write_config(BMI088_CS_GYRO, GYRO_SOFTRESET_ADDR, GYRO_SOFTRESET_VAL)) return;
        tx_thread_sleep(100); //< 延时100ms,重启需要时间

        if (!write_config(BMI088_CS_GYRO, GYRO_RANGE_ADDR, GYRO_RANGE_2000_DEG_S)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_GYRO, GYRO_BANDWIDTH_ADDR, GYRO_ODR_2000Hz_BANDWIDTH_230Hz)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_GYRO, GYRO_LPM1_ADDR, GYRO_LPM1_NOR)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_GYRO, GYRO_INT_CTRL_ADDR, GYRO_DRDY_ON)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_GYRO, GYRO_INT3_INT4_IO_CONF_ADDR, 0x00)) return;
        tx_thread_sleep(5); //< 延时5ms

        if (!write_config(BMI088_CS_GYRO, GYRO_INT3_INT4_IO_MAP_ADDR, 0x01)) return;
        tx_thread_sleep(5); //< 延时5ms
    }


    void cBMI088::ReadAccData(acc_data_t *data)
    {
        if (data == nullptr)
            return;

        uint8_t raw_data[ACC_XYZ_LEN] = {};
        AccReadOk = (ReadReg(BMI088_CS_ACC, ACC_X_LSB_ADDR, raw_data, ACC_XYZ_LEN) == HAL_OK);
        if (!AccReadOk)
        {
            *data = {};
            acc_data = *data;
            return;
        }

        data->x = static_cast<float>(ParseSigned16(&raw_data[0])) * IMU_ACCEL_6G_SEN;
        data->y = static_cast<float>(ParseSigned16(&raw_data[2])) * IMU_ACCEL_6G_SEN;
        data->z = static_cast<float>(ParseSigned16(&raw_data[4])) * IMU_ACCEL_6G_SEN;
        ReadAccTemperature(&data->temperature);
        acc_data = *data;
    }

    void cBMI088::ReadGyroData(gyro_data_t *data)
    {
        if (data == nullptr)
            return;

        uint8_t raw_data[GYRO_XYZ_LEN] = {};
        GyroReadOk = (ReadReg(BMI088_CS_GYRO, GYRO_RATE_X_LSB_ADDR, raw_data, GYRO_XYZ_LEN) == HAL_OK);
        if (!GyroReadOk)
        {
            *data = {};
            gyro_data = *data;
            return;
        }

        data->x = static_cast<float>(ParseSigned16(&raw_data[0])) * IMU_GYRO_2000_SEN - Gyro_offset[0];
        data->y = static_cast<float>(ParseSigned16(&raw_data[2])) * IMU_GYRO_2000_SEN - Gyro_offset[1];
        data->z = static_cast<float>(ParseSigned16(&raw_data[4])) * IMU_GYRO_2000_SEN - Gyro_offset[2];
        gyro_data = *data;
    }

    void cBMI088::ReadAccTemperature(float *temp)  // 通过SPI BMI088 加速度计 读取温度原始数据 转换成°C的float
                                                   // 并写到变量（temp）中
    {
        // 输入检查 → 读取寄存器 → 拼接原始值 → 处理正负号 → 换算成温度
        if (temp == nullptr)
            return;

        uint8_t raw_data[TEMP_LEN] = {};  // 创建字节数组 接收温度寄存器原始数据 TEMP_LEN=2
                                          // raw_data[0] 读的高字节 raw_data[1] 读的低字节
        if (ReadReg(BMI088_CS_ACC, TEMP_MSB_ADDR, raw_data, TEMP_LEN) != HAL_OK)
        {
            TemperatureReadOk = false;
            *temp = 0.0f;
            return;
        }

        TemperatureReadOk = true;
        int16_t raw_temperature = (static_cast<int16_t>(raw_data[0]) << 3U) |  // 每一个raw 8位 高八位全填满 左移三位 低八位只有左侧三位 向右移动5位 拼接
                                  (static_cast<int16_t>(raw_data[1]) >> 5U);

        if (raw_temperature > 1023)
            raw_temperature -= 2048;  // 11位二补码 合法范围是 -1024 ~ 1023

        *temp = static_cast<float>(raw_temperature) * TEMP_UNIT + TEMP_BIAS;  // 换算成摄氏度
    }

}


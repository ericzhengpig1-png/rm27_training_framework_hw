#ifndef BSP_USART_HPP
#define BSP_USART_HPP

#include "main.h"
#include "stm32f4xx_hal_uart.h"

enum USART_Mode
{
    USART_MODE_BLOCK = 0,
    USART_MODE_DMA = 1,
    USART_MODE_IT = 2
  };

/**
 * @brief UART 接收事件回调。
 * @note 回调运行在中断上下文；buffer 在 DMA 持续接收时可能被覆盖。
 * @param huart 发生事件的串口句柄
 * @param buffer 调用 USART_Receive() 时提供的缓冲区
 * @param write_position DMA 当前写入位置，不等同于本次新增字节数
 * @param event HAL_UART_RXEVENT_HT、HAL_UART_RXEVENT_TC 或 HAL_UART_RXEVENT_IDLE
 * @param user_context 注册回调时提供的模块上下文
 */
using USART_RxCallback = void (*)(
    UART_HandleTypeDef *huart,
    const uint8_t *buffer,
    uint16_t write_position,
    HAL_UART_RxEventTypeTypeDef event,
    void *user_context);

/**
 * @brief 初始化串口。
 */
void USART_Init(void);

/**
 * @brief 初始化串口1。
 * @note 串口1用于调试信息输出。使用DMA发送和接收。
 */
void USART1_Init(void);

/**
 * @brief 初始化串口6。
 * @note 串口6用于和裁判系统通信。使用DMA发送和接收。
 */
void USART6_Init(void);

/**
 * @brief 串口发送数据。
 * @param huart 指向串口句柄的指针
 * @param pData 发送的数据
 * @param Size 数据长度
 * @param mode 发送模式
 * @retval HAL_OK 表示阻塞发送已完成或 DMA/中断发送已启动；HAL_BUSY 表示串口正忙；HAL_TIMEOUT 表示阻塞发送超时；HAL_ERROR 表示参数或状态无效
 * @note DMA 和中断发送返回后，pData 必须保持有效，直到 HAL 发送完成。
 */
HAL_StatusTypeDef USART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, enum USART_Mode mode);

/**
 * @brief 注册串口接收事件回调。
 * @param huart 指向串口句柄的指针
 * @param callback 接收事件回调，可为 nullptr
 * @param user_context 传给回调的模块上下文
 * @retval HAL_OK 注册成功；HAL_ERROR 表示未知串口句柄
 */
HAL_StatusTypeDef USART_RegisterRxCallback(
    UART_HandleTypeDef *huart,
    USART_RxCallback callback,
    void *user_context);

/**
 * @brief 启动 DMA 接收至空闲事件。
 * @param huart 指向串口句柄的指针
 * @param pData 接收数据的缓冲区
 * @param Size 接收缓冲区大小
 * @retval HAL_OK 启动成功；HAL_BUSY 表示已有接收；HAL_ERROR 表示参数或状态无效
 * @note pData 在接收停止前必须保持有效。
 */
HAL_StatusTypeDef USART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);

#endif //  __BSP_USART_H

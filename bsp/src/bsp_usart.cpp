#include "bsp_usart.hpp"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart6;

constexpr uint32_t USART_BLOCK_TIMEOUT_MS = 100U;

struct USART_RxState
{
  uint8_t *buffer;              // DMA 正在写入的接收缓冲区
  USART_RxCallback callback;    // 数据事件发生后调用的函数
  void *user_context;           // 原样传回给调用模块的上下文
};

// 两路 UART 各自保存状态，互不共享接收缓冲区和回调。
static USART_RxState usart1_rx_state = {};
static USART_RxState usart6_rx_state = {};

// 输入：huart，待查询的 UART 句柄。
// 输出：USART1/USART6 对应的接收状态；未知句柄返回 nullptr。
static USART_RxState *USART_GetRxState(UART_HandleTypeDef *huart)
{
  if (huart == &huart1) return &usart1_rx_state;
  if (huart == &huart6) return &usart6_rx_state;
  return nullptr;
}

void USART_Init(void)
{
  USART1_Init();
  USART6_Init();
}

void USART6_Init()
{
  // CubeMX 已完成 USART6、DMA 和中断配置；调用模块注册回调后自行启动接收。
};

void USART1_Init()
{
  // CubeMX 已完成 USART1、DMA 和中断配置；调用模块注册回调后自行启动接收。
};

HAL_StatusTypeDef USART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, enum USART_Mode mode)
{
  if (huart != &huart1 && huart != &huart6) return HAL_ERROR;
  if (huart->gState == HAL_UART_STATE_RESET || pData == nullptr || Size == 0U) return HAL_ERROR;

  switch (mode)
  {
    case USART_MODE_BLOCK:
      return HAL_UART_Transmit(huart, pData, Size, USART_BLOCK_TIMEOUT_MS);
    case USART_MODE_DMA:
      return HAL_UART_Transmit_DMA(huart, pData, Size);
    case USART_MODE_IT:
      return HAL_UART_Transmit_IT(huart, pData, Size);
    default:
      return HAL_ERROR;
  }
}

/*
登记 USART_RegisterRxCallback 收到消息后通知谁（串口 MyRxCallback 模块context（协议） -> 接收状态（OK / ERROR ））
启动 USART_Receive            把收到的数据写进哪个buffer
中转 HAL_UARTEx_RxEventCallback 事件来了，通知之前登记的函数
处理 你写的 MyRxCallback       检查并解析数据 

USART_Receive() 
    ↓
请 DMA 把数据写入 buffer
    ↓
串口收到数据 / 线路空闲 / buffer 写到一半或写满
    ↓
HAL_UARTEx_RxEventCallback()
    ↓
找到该串口登记过的通知函数 c
    ↓
调用你自己的 MyRxCallback()
*/
HAL_StatusTypeDef USART_RegisterRxCallback(
  UART_HandleTypeDef *huart,
  USART_RxCallback callback,
  void *user_context)
{
  USART_RxState *rx_state = USART_GetRxState(huart);
  if (rx_state == nullptr) return HAL_ERROR;

  // BSP 只保存“收到数据后通知谁”，不理解或解析具体协议。
  rx_state->callback = callback;
  rx_state->user_context = user_context;
  return HAL_OK;
}

HAL_StatusTypeDef USART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
{
  // 接收 xx串口、数据写进buffer、buffer最大容量是size字节。输出状态？
  USART_RxState *rx_state = USART_GetRxState(huart);
  if (rx_state == nullptr || pData == nullptr || Size == 0U) return HAL_ERROR;
  if (huart->RxState == HAL_UART_STATE_RESET) return HAL_ERROR;
  if (huart->RxState != HAL_UART_STATE_READY) return HAL_BUSY;

  // 回调触发时需要知道 DMA 使用的是哪块缓冲区。
  rx_state->buffer = pData;

  // HAL 负责 DMA 搬运，并在半满、写满或串口空闲时触发事件。
  HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(huart, pData, Size);

  // 未成功启动时，不保留一个无效的缓冲区地址。
  if (status != HAL_OK) rx_state->buffer = nullptr;

  return status;
}

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  // 当串口的DMA接收到数据后，HAL自动调用这个函数？ 确认串口 找到之前的回调函数 把 那一路收到数据 数据buffer在哪里 size 为什么触发）交出去 
  USART_RxState *rx_state = USART_GetRxState(huart);
  if (rx_state == nullptr || rx_state->buffer == nullptr || rx_state->callback == nullptr) return;

  // 回调处于中断上下文：只转交原始信息，不做协议解析或阻塞操作。
  rx_state->callback(
    huart,
    rx_state->buffer,
    Size,
    // 让模块区分 DMA 半满、写满和 UART 空闲三种事件。
    HAL_UARTEx_GetRxEventType(huart),
    rx_state->user_context);
}

#include "bsp_can.hpp"

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

static bool can1_ready = false;
static bool can2_ready = false;
static CAN_RxCallback can_rx_callback = nullptr;

/**
 * @brief 初始化CAN滤波器配置。
 * 设置CAN硬件的滤波器，用于优化接收数据的处理。
 * 更多信息，请参考原文，链接：https://blog.csdn.net/weixin_54448108/article/details/128570593
 */
void CAN_Init(void)  // 配置接收过滤器 启动CAN 控制器 开启RX FIFO0接收通知 
{
    can1_ready = false;
    can2_ready = false;

    CAN_FilterTypeDef can_filter_st = {};               ///< 定义过滤器结构体
    can_filter_st.FilterActivation = ENABLE;           ///< ENABLE使能过滤器
    can_filter_st.FilterMode = CAN_FILTERMODE_IDMASK;  ///< 设置过滤器模式--标识符屏蔽位模式
    can_filter_st.FilterScale = CAN_FILTERSCALE_32BIT; ///< 过滤器的位宽 32 位
    can_filter_st.FilterIdHigh = 0x0000;               ///< ID高位
    can_filter_st.FilterIdLow = 0x0000;                ///< ID低位
    can_filter_st.FilterMaskIdHigh = 0x0000;           ///< 过滤器掩码高位
    can_filter_st.FilterMaskIdLow = 0x0000;            ///< 过滤器掩码低位
    can_filter_st.SlaveStartFilterBank = 14;            ///< 双CAN模式下从过滤器起始编号

    can_filter_st.FilterBank = 0;                                      ///< 过滤器组-双CAN可指定0~27
    can_filter_st.FilterFIFOAssignment = CAN_RX_FIFO0;                 ///< 与过滤器组管理的 FIFO
    if (HAL_CAN_ConfigFilter(&hcan1, &can_filter_st) != HAL_OK) return;
    if (HAL_CAN_Start(&hcan1) != HAL_OK) return;
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return;
    can1_ready = true;

    can_filter_st.FilterBank = 14;                                     ///< 过滤器组-双CAN可指定0~27
    if (HAL_CAN_ConfigFilter(&hcan2, &can_filter_st) != HAL_OK) return;
    if (HAL_CAN_Start(&hcan2) != HAL_OK) return;
    if (HAL_CAN_ActivateNotification(&hcan2, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return;
    can2_ready = true;
}

void CAN_RegisterRxCallback(CAN_RxCallback callback)
{
    can_rx_callback = callback;
}

HAL_StatusTypeDef CAN_Transmit(CAN_HandleTypeDef *hcan, uint32_t Id, uint8_t *msg, uint16_t len)
{
    if ((hcan == &hcan1 && !can1_ready) || (hcan == &hcan2 && !can2_ready)) return HAL_ERROR;
    if (hcan != &hcan1 && hcan != &hcan2) return HAL_ERROR;
    if (Id > 0x7FFU || len > 8U || (len > 0U && msg == nullptr)) return HAL_ERROR;
    if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) == 0U) return HAL_BUSY;

    CAN_TxHeaderTypeDef tx_header = {};
    uint32_t mailbox = 0U;
    tx_header.StdId = Id;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = len;
    return HAL_CAN_AddTxMessage(hcan, &tx_header, msg, &mailbox);
}

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if ((hcan == &hcan1 && !can1_ready) || (hcan == &hcan2 && !can2_ready)) return;
    if (hcan != &hcan1 && hcan != &hcan2) return;

    CAN_RxHeaderTypeDef rx_header = {};
    uint8_t rx_data[8] = {};
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) return;
    if (rx_header.IDE != CAN_ID_STD || rx_header.RTR != CAN_RTR_DATA || rx_header.DLC > 8U) return;
    if (can_rx_callback != nullptr) can_rx_callback(hcan, &rx_header, rx_data);
}

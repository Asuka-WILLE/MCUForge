#include "sbus.h"
#include "main.h"
#include "usart.h"
#include "move_mode_control.h"
#include <string.h>


uint16_t sbus_ch[16] = {0};
uint8_t uart5_rx_byte = 0;

extern uint8_t rx_trig;
extern uint8_t uart2_rx_buf[7]; 
extern uint8_t uart2_rx_done;
extern int16_t motor_real_speed;

// 全局变量定义（必须加volatile）
volatile uint8_t sbus_buf[SBUS_FRAME_LEN];
volatile uint8_t sbus_frame_ok = 0;
volatile uint8_t sbus_failsafe = 0;    // 故障安全标志
volatile uint8_t sbus_frame_lost = 0;  // 帧丢失标志

// 接收状态机变量（静态改为全局，方便超时处理）
static uint8_t sbus_rx_index = 0;
static uint16_t sbus_err_cnt = 0;
static uint32_t sbus_last_rx_time = 0; // 最后一次接收字节的时间

void SBUS_Init(void)
{
    HAL_UART_Receive_IT(&huart5, &uart5_rx_byte, 1);
}

void SBUS_Receive(uint8_t data)
{
    static uint8_t index = 0;

    if(index == 0 && data != 0x0F)
    {
        return;
    }

    sbus_buf[index++] = data;

    if(index >= SBUS_FRAME_LEN)
    {
        index = 0;
        sbus_frame_ok = 1;
    }
}


void SBUS_ParseChannels(const uint8_t sbus_buf[SBUS_FRAME_LEN], int16_t channels[SBUS_NUM_CHANNELS])
{
    // 清零输出数组
    memset(channels, 0, sizeof(int16_t) * SBUS_NUM_CHANNELS);

    // 通道0（第1个通道）
    channels[0]  = ((int16_t)sbus_buf[1] >> 0) | ((int16_t)sbus_buf[2] << 8) & 0x07FF;
    // 通道1
    channels[1]  = ((int16_t)sbus_buf[2] >> 3) | ((int16_t)sbus_buf[3] << 5) & 0x07FF;
    // 通道2
    channels[2]  = ((int16_t)sbus_buf[3] >> 6) | ((int16_t)sbus_buf[4] << 2) | ((int16_t)sbus_buf[5] << 10) & 0x07FF;
    // 通道3
    channels[3]  = ((int16_t)sbus_buf[5] >> 1) | ((int16_t)sbus_buf[6] << 7) & 0x07FF;
    // 通道4
    channels[4]  = ((int16_t)sbus_buf[6] >> 4) | ((int16_t)sbus_buf[7] << 4) & 0x07FF;
    // 通道5
    channels[5]  = ((int16_t)sbus_buf[7] >> 7) | ((int16_t)sbus_buf[8] << 1) | ((int16_t)sbus_buf[9] << 9) & 0x07FF;
    // 通道6
    channels[6]  = ((int16_t)sbus_buf[9] >> 2) | ((int16_t)sbus_buf[10] << 6) & 0x07FF;
    // 通道7
    channels[7]  = ((int16_t)sbus_buf[10] >> 5) | ((int16_t)sbus_buf[11] << 3) & 0x07FF;
    // 通道8
    channels[8]  = ((int16_t)sbus_buf[12] >> 0) | ((int16_t)sbus_buf[13] << 8) & 0x07FF;
    // 通道9
    channels[9]  = ((int16_t)sbus_buf[13] >> 3) | ((int16_t)sbus_buf[14] << 5) & 0x07FF;
    // 通道10
    channels[10] = ((int16_t)sbus_buf[14] >> 6) | ((int16_t)sbus_buf[15] << 2) | ((int16_t)sbus_buf[16] << 10) & 0x07FF;
    // 通道11
    channels[11] = ((int16_t)sbus_buf[16] >> 1) | ((int16_t)sbus_buf[17] << 7) & 0x07FF;
    // 通道12
    channels[12] = ((int16_t)sbus_buf[17] >> 4) | ((int16_t)sbus_buf[18] << 4) & 0x07FF;
    // 通道13
    channels[13] = ((int16_t)sbus_buf[18] >> 7) | ((int16_t)sbus_buf[19] << 1) | ((int16_t)sbus_buf[20] << 9) & 0x07FF;
    // 通道14
    channels[14] = ((int16_t)sbus_buf[20] >> 2) | ((int16_t)sbus_buf[21] << 6) & 0x07FF;
    // 通道15
    channels[15] = ((int16_t)sbus_buf[21] >> 5) | ((int16_t)sbus_buf[22] << 3) & 0x07FF;
		
		//uint8_t ch17 = (sbus_buf[23] & 0x80) ? 1 : 0; // 通道17
		//uint8_t ch18 = (sbus_buf[23] & 0x40) ? 1 : 0; // 通道18
    // 与0x07FF按位与，确保每个通道值都是11位（防止溢出）
    for(uint8_t i=0; i<SBUS_NUM_CHANNELS; i++)
    {
        channels[i] &= 0x07FF;
    }
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == UART5)
    {
        // 更新最后接收时间（用于超时检测）
        sbus_last_rx_time = HAL_GetTick();
        
        // 1. 等待帧头状态
        if(sbus_rx_index == 0)
        {
            sbus_err_cnt++;
            // 连续200个无效字节（约24ms）强制重置
            if(sbus_err_cnt > 200)
            {
                sbus_err_cnt = 0;
                goto restart_rx; // 统一跳转重启接收
            }
            
            // 检测到正确帧头
            if(uart5_rx_byte == 0x0F)
            {
                sbus_err_cnt = 0;
                sbus_buf[sbus_rx_index++] = uart5_rx_byte;
            }
            goto restart_rx;
        }
        
        // 2. 接收数据状态
        sbus_buf[sbus_rx_index++] = uart5_rx_byte;
        
        // 3. 接收完成，校验帧尾
        if(sbus_rx_index >= SBUS_FRAME_LEN)
        {
            sbus_rx_index = 0;
            // 关键：校验帧尾0x00，无效则丢弃整帧
            if(sbus_buf[SBUS_FRAME_LEN-1] == 0x00)
            {
                // 解析标志位
                sbus_failsafe = (sbus_buf[23] & 0x08) ? 1 : 0;
                sbus_frame_lost = (sbus_buf[23] & 0x04) ? 1 : 0;
                sbus_frame_ok = 1;
            }
        }
        
				restart_rx:
        // 统一重启接收，只写一次，避免遗漏
        HAL_UART_Receive_IT(&huart5, &uart5_rx_byte, 1);
    }
		
			 
		if(huart->Instance == USART2)
    {
        // 1. ����ȡ֡��Ϣ
        uint8_t addr   = uart2_rx_buf[0];    // վ��
        uint8_t func   = uart2_rx_buf[1];    // ������
        uint8_t len    = uart2_rx_buf[2];    // ���ݳ���

        // 2. ��������
        if((addr == 1 || addr == 2) && func == 0x03 && len == 0x02)
        {
            uint16_t calc_crc = Modbus_CRC16(uart2_rx_buf, 5);
            // �յ��� CRC�����ֽ���ǰ�����ֽ��ں�
            uint8_t recv_crc_lo = uart2_rx_buf[5];
            uint8_t recv_crc_hi = uart2_rx_buf[6];
            uint16_t recv_crc   = ((uint16_t)recv_crc_hi << 8) | recv_crc_lo;

            // ֻ�� CRC ��ȷ���Ž����ٶ�
            if(calc_crc == recv_crc)
            {
                motor_real_speed = (int16_t)(uart2_rx_buf[3] << 8 | uart2_rx_buf[4]);
                uart2_rx_done = 1;
            }
        }

        // ���¿�������
        HAL_UART_Receive_IT(&huart2, uart2_rx_buf, 7);
    
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == UART5)
    {
        // 清除所有错误标志
        __HAL_UART_CLEAR_FLAG(huart, UART_FLAG_ORE | UART_FLAG_PE | UART_FLAG_FE | UART_FLAG_NE);
        // 重置接收状态机
        sbus_rx_index = 0;
        sbus_err_cnt = 0;
        // 重新启动接收
        HAL_UART_Receive_IT(&huart5, &uart5_rx_byte, 1);
    }
}


// 主循环中调用的超时检测函数（必须每1ms调用一次）
void SBUS_TimeoutCheck(void)
{
    // 超过30ms未收到任何字节（约2个SBUS帧周期），强制重置
    if((HAL_GetTick() - sbus_last_rx_time) > 30)
    {
        sbus_rx_index = 0;
        sbus_err_cnt = 0;
        sbus_failsafe = 1; // 超时视为信号丢失
    }
}
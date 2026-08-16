#include "move_mode_control.h"
#include "usart.h"
extern int16_t emergency_stop;
extern volatile uint8_t en_flag;

#define MOTOR_PROFILE_TIME_VALUE   0x0064U
#define MOTOR_CONFIG_WRITE_GAP_MS  10U

// ===================== MODBUS CRC16 =====================
uint16_t Modbus_CRC16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static HAL_StatusTypeDef motor_write_u16(uint8_t slave_addr, uint16_t reg, uint16_t value)
{
    uint8_t cmd[8] = {
        slave_addr,
        0x06,
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
        0,
        0
    };
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;
    return RS485_SendPacket(cmd, 8);
}

static void motor_configure_profile(uint8_t slave_addr)
{
    /* Clear retained target first, then apply the same profile to both drives. */
    (void)motor_write_u16(slave_addr, 0x2318, 0x0000); HAL_Delay(MOTOR_CONFIG_WRITE_GAP_MS);
    (void)motor_write_u16(slave_addr, 0x2102, 0x0001); HAL_Delay(MOTOR_CONFIG_WRITE_GAP_MS);
    (void)motor_write_u16(slave_addr, 0x2320, MOTOR_PROFILE_TIME_VALUE); HAL_Delay(MOTOR_CONFIG_WRITE_GAP_MS);
    (void)motor_write_u16(slave_addr, 0x2321, MOTOR_PROFILE_TIME_VALUE); HAL_Delay(MOTOR_CONFIG_WRITE_GAP_MS);
}

// ===================== 电机初始化（速度模式 + 使能）=====================
// ===================== 双电机速度设置 =====================

// ===================== 读取电机速度（站号1）=====================
// ===================== 澶辫兘+鎬ュ仠 =====================
void motor_stop(void)
{
    uint8_t cmd[8] = {0x01,0x06,0x21,0x00,0,0,0x83,0xF6};
    RS485_SendPacket(cmd,8);
		HAL_Delay(50);
		uint8_t cmd2[8] = {0x02,0x06,0x21,0x00,0,0,0x83,0xC5};
    RS485_SendPacket(cmd2,8);
		en_flag = 0;
}

void motor_emergency_stop(void)
{
    uint8_t estop[8] = {0x01,0x06,0x23,0x22,0,1,0xE3,0x84};
    RS485_SendPacket(estop,8);
		HAL_Delay(30);
		uint8_t estop2[8] = {0x02,0x06,0x23,0x22,0,1,0xE3,0xB7};
    RS485_SendPacket(estop2,8);	
    HAL_Delay(30);
		emergency_stop = 1;
    motor_stop();
}


// ===================== 浣胯兘 + 娓呴櫎鎬ュ仠鏍囪瘑 =====================
void motor_enable(void)
{
    motor_configure_profile(0x01);
    motor_configure_profile(0x02);

    uint8_t cmd[8]  = {0x01,0x06,0x21,0x00,0x00,0x01,0x42,0x36};
    RS485_SendPacket(cmd, 8);
    HAL_Delay(30);
    
    uint8_t cmd2[8] = {0x02,0x06,0x21,0x00,0x00,0x01,0x42,0x05};
    RS485_SendPacket(cmd2, 8);
    HAL_Delay(30);
		
		en_flag = 1;
}

void motor_clear_emergency_stop(void)
{
    uint8_t clear_estop1[8] = {0x01,0x06,0x23,0x22,0x00,0x00,0x22,0x44};
		RS485_SendPacket(clear_estop1, 8);
		HAL_Delay(30);
    uint8_t clear_estop2[8] = {0x02,0x06,0x23,0x22,0x00,0x00,0x22,0x77};
    RS485_SendPacket(clear_estop2, 8);
		HAL_Delay(30);

		motor_enable();
		emergency_stop = 0;
} 



// ===================== 鍗囬檷鏈烘瀯鎺у埗 =====================

void lift_up(void)
{
    uint8_t cmd[8] = {0x01,0x06,0x00,0x01,0x00,0x02,0x59,0xCB};
    RS485_SendPacket2(cmd, 8);
}


void lift_down(void)
{
    uint8_t cmd[8] = {0x01,0x06,0x00,0x01,0x00,0x04,0xD9,0xC9};
    RS485_SendPacket2(cmd, 8);
}


void lift_stop(void)
{
    uint8_t cmd[8] = {0x01,0x06,0x00,0x01,0x00,0x01,0x19,0xCA};
    RS485_SendPacket2(cmd, 8);
}

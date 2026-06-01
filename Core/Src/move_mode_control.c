#include "move_mode_control.h"
#include "usart.h"
#include <string.h>


extern uint16_t test1;
extern uint16_t test2;
extern uint8_t uart2_rx_buf[7];  // 接收缓存
extern uint8_t uart2_rx_done;
extern int16_t motor_real_speed;
extern int16_t emergency_stop;
extern int16_t en_flag;

#define MODBUS_RESPONSE_LEN        7U
#define MOTOR_READ_TIMEOUT_MS      50U
#define LIFT_READ_TIMEOUT_MS       80U

static int16_t modbus_parse_i16_response(uint8_t *buf, uint8_t slave_addr)
{
    if(buf[0] != slave_addr || buf[1] != 0x03 || buf[2] != 0x02)
    {
        return 0;
    }

    uint16_t calc_crc = Modbus_CRC16(buf, 5);
    uint16_t recv_crc = ((uint16_t)buf[6] << 8) | buf[5];
    if(calc_crc != recv_crc)
    {
        return 0;
    }

    return (int16_t)(((uint16_t)buf[3] << 8) | buf[4]);
}

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

// ===================== 电机初始化（速度模式 + 使能）=====================
void motor_start_init(void)
{
    /*
     * 下面旧硬编码帧保留为错误记录，不再使用。
     * 错误原因：寄存器和值正确，但 CRC 与当前 Modbus RTU 帧不匹配；
     * 若直接发送，驱动器会判定 CRC 校验失败而忽略命令。
     */
    // uint8_t mode[8]  = {0x01,0x06,0x21,0x02,0x00,0x01,0x95,0xC2}; // 正确 CRC 应为 E3 F6
    // uint8_t en[8]    = {0x01,0x06,0x21,0x00,0x00,0x01,0x91,0xC0}; // 正确 CRC 应为 42 36
    // uint8_t accel[8] = {0x01,0x06,0x23,0x20,0x01,0xF4,0x76,0x70}; // 正确 CRC 应为 83 93
    // uint8_t decel[8] = {0x01,0x06,0x23,0x21,0x01,0xF4,0x36,0x71}; // 正确 CRC 应为 D2 53

    // RS485_SendPacket(set_addr_01, 8);
    HAL_Delay(200);
    motor_write_u16(0x01, 0x2102, 0x0001); HAL_Delay(200);
    motor_write_u16(0x01, 0x2320, 0x01F4); HAL_Delay(200);
    motor_write_u16(0x01, 0x2321, 0x01F4); HAL_Delay(200);

    HAL_Delay(200);        // 多加一段延时，让电机准备好
    motor_write_u16(0x01, 0x2100, 0x0001);
    HAL_Delay(300);        // 使能后多等一会，确保电机就绪
}

void change_station(void){
		uint8_t unlock[8]  = {0x01, 0x06, 0x21, 0x19, 0x26, 0x94, 0x49, 0xFE};
		uint8_t station[8] = {0x01, 0x06, 0x45, 0x03, 0x00, 0x02, 0xED, 0x07};
		uint8_t lock[8]    = {0x01, 0x06, 0x21, 0x19, 0x26, 0x8E, 0xC8, 0x35};
		RS485_SendPacket(unlock,8); HAL_Delay(200);
		RS485_SendPacket(station,8);  HAL_Delay(200);
    RS485_SendPacket(lock,8);  HAL_Delay(200);

}

// ===================== 双电机速度设置 =====================

void speed_set(int16_t left_rpm, int16_t right_rpm)
{
    left_rpm  = (left_rpm  > MAX_RPM) ? MAX_RPM : (left_rpm  < -MAX_RPM) ? -MAX_RPM : left_rpm;
    right_rpm = (right_rpm > MAX_RPM) ? MAX_RPM : (right_rpm < -MAX_RPM) ? -MAX_RPM : right_rpm;

    // ----------------- 鍙宠疆 ---------------
    uint8_t r_cmd[8] = {0x02, 0x06, 0x23, 0x18, 0,0, 0,0};
    r_cmd[4] = (right_rpm >> 8) & 0xFF;
    r_cmd[5] = right_rpm & 0xFF;
    uint16_t crc_r = Modbus_CRC16(r_cmd,6);
    r_cmd[6] = crc_r & 0xFF;
    r_cmd[7] = (crc_r >> 8) & 0xFF;
    RS485_SendPacket(r_cmd,8);
		HAL_Delay(2);  // no response wait needed
	
	  // ---------------- 宸﹁疆 ----------------
    uint8_t l_cmd[8] = {0x01, 0x06, 0x23, 0x18, 0,0, 0,0};// 0x00, 0x64, 0xCA, 0x79
    l_cmd[4] = (left_rpm >> 8) & 0xFF;
    l_cmd[5] = left_rpm & 0xFF;
    uint16_t crc_l = Modbus_CRC16(l_cmd,6);
    l_cmd[6] = crc_l & 0xFF;
    l_cmd[7] = (crc_l >> 8) & 0xFF;
    RS485_SendPacket(l_cmd,8);
   HAL_Delay(5);

    
}

// ===================== 读取电机速度（站号1）=====================
int16_t motor_read_speed(uint8_t slave_addr)
{
    uint8_t buf[MODBUS_RESPONSE_LEN] = {0};
    uint8_t cmd[8] = {slave_addr, 0x03, 0x50, 0x00, 0x00, 0x01, 0x00, 0x00};
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = crc >> 8;

    uart2_rx_done = 0;
    memset(uart2_rx_buf, 0, sizeof(uart2_rx_buf));
    __HAL_UART_FLUSH_DRREGISTER(&huart2);
    RS485_SendPacket(cmd, 8);

    if(RS485_ReceivePacket(buf, MODBUS_RESPONSE_LEN, MOTOR_READ_TIMEOUT_MS) != HAL_OK)
    {
        return 0;
    }

    motor_real_speed = modbus_parse_i16_response(buf, slave_addr);
    return motor_real_speed;
}

int16_t lift_read_height(void)
{
    uint8_t buf[MODBUS_RESPONSE_LEN] = {0};
    uint8_t cmd[8] = {0x01, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00};
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = crc >> 8;

    __HAL_UART_FLUSH_DRREGISTER(&huart3);
    RS485_SendPacket2(cmd, 8);

    if(RS485_ReceivePacket2(buf, MODBUS_RESPONSE_LEN, LIFT_READ_TIMEOUT_MS) != HAL_OK)
    {
        return -1;
    }

    if(buf[0] != 0x01 || buf[1] != 0x03 || buf[2] != 0x02)
    {
        return -1;
    }

    uint16_t calc_crc = Modbus_CRC16(buf, 5);
    uint16_t recv_crc = ((uint16_t)buf[6] << 8) | buf[5];
    if(calc_crc != recv_crc)
    {
        return -1;
    }

    return (int16_t)(((uint16_t)buf[3] << 8) | buf[4]);
}

/*
int16_t motor_read_speed(uint8_t slave_addr)
{
    uint8_t buf[7] = {0};  // 正确长度：7字节

    // 你实测成功的指令：读 1 个寄存器（0x5000）
    uint8_t cmd[8] = {0x01, 0x03, 0x50, 0x00, 0x00, 0x01, 0x00, 0x00};
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;

    RS485_SendPacket(cmd, 8);
   
    RS485_ReceivePacket(buf, 7, 100);

    // 解析真实速度
		int16_t speed = (int16_t)(buf[3] << 8 | buf[4]);
		//int16_t speed=buf[4];
    return speed;
}
*/


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
		motor_enable();
	
    uint8_t clear_estop1[8] = {0x01,0x06,0x23,0x22,0x00,0x00,0x22,0x44};
		RS485_SendPacket(clear_estop1, 8);
		HAL_Delay(30);
    uint8_t clear_estop2[8] = {0x02,0x06,0x23,0x22,0x00,0x00,0x22,0x77};
    RS485_SendPacket(clear_estop2, 8);
		HAL_Delay(30);
		
		emergency_stop = 0;
} 



// ===================== 自动扫描电机地址 =====================
uint8_t motor_scan_address(void)
{
    for(uint8_t addr=1; addr<=10; addr++)
    {
        uint8_t cmd[8] = {addr,0x03,0x50,0x12,0x00,0x02,0,0};
        uint16_t crc = Modbus_CRC16(cmd,6);
        cmd[6] = crc & 0xFF;
        cmd[7] = (crc>>8)&0xFF;

        RS485_SendPacket(cmd,8);
        HAL_Delay(30);

        uint8_t buf[9] = {0};
        /* 读取 2 个寄存器时正常响应为：地址 + 功能码 + 字节数4 + 数据4 + CRC2，共 9 字节。 */
        if(RS485_ReceivePacket(buf,9, 300) == HAL_OK)
        {
            return addr;
        }
    }
    return 0;
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


void lift_reset(void)
{
    uint8_t cmd[8] = {0x01,0x06,0x00,0x01,0x00,0x08,0xD9,0xCC};
    RS485_SendPacket2(cmd, 8);
}



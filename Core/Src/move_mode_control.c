#include "move_mode_control.h"
#include "usart.h"
#include <string.h>


extern uint16_t test1;
extern uint16_t test2;
extern uint8_t uart2_rx_buf[7];  // ½ÓÊÕ»º´æ
extern uint8_t uart2_rx_done;
extern uint16_t motor_real_speed;
extern int16_t emergency_stop;
extern int16_t en_flag;

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

// ===================== µç»ú³õÊ¼»¯£¨ËÙ¶ÈÄ£Ê½ + Ê¹ÄÜ£©=====================
void motor_start_init(void)
{
		//¸ÄÕ¾ºÅ
	//	uint8_t set_addr_01[8] = {0x01, 0x06, 0x45, 0x03, 0x00, 0x02, 0xED, 0x07};
    uint8_t mode[8]    = {0x01,0x06,0x21,0x02,0x00,0x01,0x95,0xC2};
    uint8_t en[8]      = {0x01,0x06,0x21,0x00,0x00,0x01,0x91,0xC0};
    uint8_t accel[8]   = {0x01,0x06,0x23,0x20,0x01,0xF4,0x76,0x70};
    uint8_t decel[8]   = {0x01,0x06,0x23,0x21,0x01,0xF4,0x36,0x71};

		//RS485_SendPacket(set_addr_01, 8);
    HAL_Delay(200);
    RS485_SendPacket(mode,8);   HAL_Delay(200);
    RS485_SendPacket(accel,8);  HAL_Delay(200);
    RS485_SendPacket(decel,8);  HAL_Delay(200);
    
    HAL_Delay(200);        // ¶à¼ÓÒ»¶ÎÑÓÊ±£¬ÈÃµç»ú×¼±¸ºÃ
    RS485_SendPacket(en,8);
    HAL_Delay(300);        // Ê¹ÄÜºó¶àµÈÒ»»á£¬È·±£µç»ú¾ÍĞ÷
}

void change_station(void){
		uint8_t unlock[8]  = {0x01, 0x06, 0x21, 0x19, 0x26, 0x94, 0x49, 0xFE};
		uint8_t station[8] = {0x01, 0x06, 0x45, 0x03, 0x00, 0x02, 0xED, 0x07};
		uint8_t lock[8]    = {0x01, 0x06, 0x21, 0x19, 0x26, 0x8E, 0xC8, 0x35};
		RS485_SendPacket(unlock,8); HAL_Delay(200);
		RS485_SendPacket(station,8);  HAL_Delay(200);
    RS485_SendPacket(lock,8);  HAL_Delay(200);

}

// ===================== Ë«µç»úËÙ¶ÈÉèÖÃ =====================

void speed_set(int16_t left_rpm, int16_t right_rpm)
{
    left_rpm  = (left_rpm  > MAX_RPM) ? MAX_RPM : (left_rpm  < -MAX_RPM) ? -MAX_RPM : left_rpm;
    right_rpm = (right_rpm > MAX_RPM) ? MAX_RPM : (right_rpm < -MAX_RPM) ? -MAX_RPM : right_rpm;

    // ---------------- å·¦è½® ----------------
    uint8_t l_cmd[8] = {0x01, 0x06, 0x23, 0x18, 0,0, 0,0};// 0x00, 0x64, 0xCA, 0x79
    l_cmd[4] = (left_rpm >> 8) & 0xFF;
    l_cmd[5] = left_rpm & 0xFF;
    uint16_t crc_l = Modbus_CRC16(l_cmd,6);
    l_cmd[6] = crc_l & 0xFF;
    l_cmd[7] = (crc_l >> 8) & 0xFF;
    RS485_SendPacket(l_cmd,8);
   HAL_Delay(50);

    // ----------------- å³è½® ---------------
    uint8_t r_cmd[8] = {0x02, 0x06, 0x23, 0x18, 0,0, 0,0};
    r_cmd[4] = (right_rpm >> 8) & 0xFF;
    r_cmd[5] = right_rpm & 0xFF;
    uint16_t crc_r = Modbus_CRC16(r_cmd,6);
    r_cmd[6] = crc_r & 0xFF;
    r_cmd[7] = (crc_r >> 8) & 0xFF;
    RS485_SendPacket(r_cmd,8);
		HAL_Delay(10);  //æ ¹æœ¬ä¸éœ€è¦ç­‰å¾…ï¼Œå› ä¸ºä¸éœ€è¦ç­‰å¾…æ•°æ®è¿”å›
}

// ===================== ¶ÁÈ¡µç»úËÙ¶È£¨Õ¾ºÅ1£©=====================
int16_t motor_read_speed(uint8_t slave_addr)
{
    uint8_t cmd[8] = {slave_addr, 0x03, 0x50, 0x00, 0x00, 0x01, 0x00, 0x00};
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = crc >> 8;

    
    uart2_rx_done = 0;
   // motor_real_speed = 0;
    memset(uart2_rx_buf, 0, 7);  

    // ·¢ËÍ
    RS485_SendPacket(cmd, 8);

    // µÈ´ıĞÂµÄ¡¢ÕıÈ·µÄÖ¡
    uint32_t start = HAL_GetTick();
    while(uart2_rx_done == 0 && (HAL_GetTick() - start < 100));

    return motor_real_speed;
}

/*
int16_t motor_read_speed(uint8_t slave_addr)
{
    uint8_t buf[7] = {0};  // ÕıÈ·³¤¶È£º7×Ö½Ú

    // ÄãÊµ²â³É¹¦µÄÖ¸Áî£º¶Á 1 ¸ö¼Ä´æÆ÷£¨0x5000£©
    uint8_t cmd[8] = {0x01, 0x03, 0x50, 0x00, 0x00, 0x01, 0x00, 0x00};
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;

    RS485_SendPacket(cmd, 8);
   
    RS485_ReceivePacket(buf, 7, 100);

    // ½âÎöÕæÊµËÙ¶È
		int16_t speed = (int16_t)(buf[3] << 8 | buf[4]);
		//int16_t speed=buf[4];
    return speed;
}
*/


// ===================== å¤±èƒ½+æ€¥åœ =====================
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


// ===================== ä½¿èƒ½ + æ¸…é™¤æ€¥åœæ ‡è¯† =====================
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



// ===================== ×Ô¶¯É¨Ãèµç»úµØÖ· =====================
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

        uint8_t buf[7] = {0};
        if(RS485_ReceivePacket(buf,7, 300) == HAL_OK)
        {
            return addr;
        }
    }
    return 0;
}


// ===================== å‡é™æœºæ„æ§åˆ¶ =====================

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




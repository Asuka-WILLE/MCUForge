#ifndef __MOVE_MODE_CONTROL_H
#define __MOVE_MODE_CONTROL_H

#include "main.h"
#include "usart.h"
#include "stm32h7xx_hal.h"

#define RPM_CHANGE_THRESHOLD 5
#define MAX_RPM 32  

// 升降机构状态定义
typedef enum {
    LIFT_STOP = 0,
    LIFT_UP = 1,
    LIFT_DOWN = 2,
   // LIFT_RESET
} LiftState;


uint16_t Modbus_CRC16(uint8_t *data, uint16_t len);
void    motor_start_init(void);
void change_station(void);
void    speed_set(int16_t left, int16_t right);
int16_t motor_read_speed(uint8_t motor);
int16_t lift_read_height(void);
void    motor_stop(void);
void 		motor_enable(void);
void    motor_emergency_stop(void);
void motor_clear_emergency_stop(void);
uint8_t motor_scan_address(void);
void lift_up(void);
void lift_down(void);
void lift_stop(void);
void lift_reset(void);

#endif
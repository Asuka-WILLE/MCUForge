#ifndef __MOVE_MODE_CONTROL_H
#define __MOVE_MODE_CONTROL_H

#include "main.h"
#include "usart.h"
#include "stm32h7xx_hal.h"

#define MAX_RPM 32

// 升降机构状态定义
typedef enum {
    LIFT_STOP = 0,
    LIFT_UP = 1,
    LIFT_DOWN = 2
} LiftState;

uint16_t Modbus_CRC16(uint8_t *data, uint16_t len);
void motor_stop(void);
void motor_enable(void);
void motor_emergency_stop(void);
void motor_clear_emergency_stop(void);
void lift_up(void);
void lift_down(void);
void lift_stop(void);

#endif

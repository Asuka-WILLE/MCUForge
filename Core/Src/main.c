/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "move_mode_control.h"
#include "lcd.h"
#include "SBUS.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TELEMETRY_PERIOD_MS 50U
#define TELEMETRY_LINE_MAX  1024U
#define TELEMETRY_RESPONSE_LEN 7U
#define TELEMETRY_QUERY_PERIOD_MS 60U
#define TELEMETRY_MOTOR_TIMEOUT_MS 50U
#define TELEMETRY_LIFT_TIMEOUT_MS  15U
#define TELEMETRY_TX_TIMEOUT_MS    5U
#define TELEMETRY_CONTROL_HOLDOFF_MS 5U
#define MOTOR_WRITE_ECHO_TIMEOUT_MS 6U
#define MOTOR_COMMAND_REFRESH_MS   100U
#define MOTOR_COMMAND_IMMEDIATE_DELTA_RPM 1
#define MOTOR_TRAJECTORY_PERIOD_MS 20U
#define MOTOR_TRAJECTORY_MAX_DT_MS 50U
#define MOTOR_TRAJECTORY_TRACKING_GAIN_PER_S 10.0f
#define MOTOR_TRAJECTORY_TARGET_EPSILON_RPM 0.05f
#define MOTOR_TRAJECTORY_ACCEL_EPSILON_RPM_PER_S 0.25f
#define MOTOR_TRAJECTORY_BENCH_TEST 0U
#define MOTOR_TRAJECTORY_BENCH_CYCLE_MS 10000U

/*
 * 速度单位是驱动器命令 RPM。先对底盘前后速度和转向量分别限加速度、
 * 限加加速度，再混合成左右轮，避免两侧独立轨迹造成起步不同步。
 * 停车末段让目标减速度随速度误差减小，使制动力在零速前逐步退出。
 */
#define MOTOR_LINEAR_MAX_ACCEL_RPM_PER_S 60.0f
#define MOTOR_LINEAR_MAX_DECEL_RPM_PER_S 55.0f
#define MOTOR_LINEAR_MAX_JERK_RPM_PER_S2 800.0f
#define MOTOR_LINEAR_MAX_STOP_JERK_RPM_PER_S2 600.0f
#define MOTOR_STEER_MAX_ACCEL_CMD_PER_S 160.0f
#define MOTOR_STEER_MAX_DECEL_CMD_PER_S 160.0f
#define MOTOR_STEER_MAX_JERK_CMD_PER_S2 1600.0f
#define MOTOR_STEER_MAX_STOP_JERK_CMD_PER_S2 1600.0f
#define CASTER_ALIGN_CRAWL_RPM             8
#define CASTER_ALIGN_MIN_BOOST_REQUEST_RPM 4
#define CASTER_ALIGN_MOVING_DURATION_MS    500U
#define CASTER_ALIGN_MATCH_ERROR_RPM       1
#define CASTER_ALIGN_MATCH_DURATION_MS     200U
#define CASTER_ALIGN_MAX_DURATION_MS       2200U
#define CASTER_ALIGN_TRIGGER_STEER_CMD     8
#define CASTER_ALIGN_STRAIGHT_STEER_CMD    4
#define CASTER_ALIGN_REST_SPEED_RPM        0.75f
#define CASTER_ALIGN_REST_ACCEL_RPM_PER_S  2.0f
#define CASTER_ALIGN_COUNTER_STEER_CMD      0
#define CASTER_ALIGN_FORWARD_LAUNCH_BIAS_CMD        4
#define CASTER_ALIGN_FORWARD_PRELOAD_BIAS_CMD       8
#define CASTER_ALIGN_FORWARD_PRELOAD_DURATION_MS  250U
#define PC_TEST_MAX_LINEAR_RPM              20
#define PC_TEST_MAX_STEER_CMD               32
#define PC_TEST_MIN_DURATION_MS             100U
#define PC_TEST_MAX_DURATION_MS             10000U
#define PC_TEST_REST_SPEED_RPM              2
#define PC_TEST_RX_BUFFER_SIZE              64U
#define STRAIGHT_SYNC_MIN_COMMAND_RPM        5
#define STRAIGHT_SYNC_FEEDBACK_MAX_AGE_MS    400U
#define STRAIGHT_SYNC_FILTER_ALPHA           0.5f
#define STRAIGHT_SYNC_ERROR_THRESHOLD_RPM    1.5f
#define STRAIGHT_SYNC_MAX_TRIM_RPM           2
#define STRAIGHT_SYNC_LAUNCH_FAST_RPM         4
#define STRAIGHT_SYNC_LAUNCH_STALLED_RPM      1
#define STRAIGHT_SYNC_LAUNCH_TRIM_RPM         4
#define NEUTRAL_TERMINAL_SPEED_RPM            1.0f
#define NEUTRAL_TERMINAL_STEER_CMD            1.0f
#define NEUTRAL_ZERO_BURST_PERIOD_MS          20U
#define NEUTRAL_ZERO_BURST_DURATION_MS        300U
#define RC_REQUIRED_VALID_FRAMES   3U
#define RC_TRUST_TIMEOUT_MS        60U
#define RC_FAILSAFE_STOP_TIMEOUT_MS 150U
#define RC_ZERO_REFRESH_MS         50U
#define RC_CH3_CENTER              992
#define RC_CH4_CENTER              992
#define RC_STICK_NEUTRAL_DEADZONE  60
#define RC_STICK_JUMP_THRESHOLD    350
#define RC_STICK_JUMP_CONFIRM_FRAMES 2U
#define RC_STICK_JUMP_CONFIRM_TOLERANCE 350
#define RC_LCD_REFRESH_MS          250U
#define RC_LCD_FONT_SIZE           16U
#define RC_LCD_ROW_STEP            20U
#define RC_LCD_LINE_CHARS          (LCD_W / (RC_LCD_FONT_SIZE / 2U))
#define RC_LCD_ROWS                8U

// ===================== 辅助函数 =====================
/**
 * @brief  摇杆死区处理
 * @param  value  原始摇杆值
 * @param  center 摇杆中位值
 * @param  deadzone 死区范围
 * @retval 处理后的值（死区内返回0）
 */
static int16_t joystick_deadzone(int16_t value, int16_t center, int16_t deadzone)
{
    int16_t diff = value - center;
    if (abs(diff) <= deadzone)
        return 0;
    return diff;
}

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint16_t adc_val[1];

volatile int16_t speed, steer;
volatile int16_t left_rpm, right_rpm;

volatile uint8_t modbus_read_flag = 0;  // 定时读取标志
volatile uint8_t motor_read_flag = 0;  // 定时器读速度标志
volatile uint8_t en_flag=0;

uint8_t uart2_rx_buf[7] = {0};  // 接收缓存
uint8_t uart2_rx_done = 0;     // 接收完成标志
int16_t motor_real_speed = 0;               // 左电机速度（全局）

uint16_t test1=0;
uint16_t test2=0;

extern volatile uint8_t sbus_buf[SBUS_FRAME_LEN];
extern volatile uint8_t sbus_frame_ok;
extern volatile uint8_t sbus_failsafe;    // 故障安全标志
extern volatile uint8_t sbus_frame_lost;  

volatile LiftState current_lift_state = LIFT_STOP;
int16_t emergency_stop=0;
int16_t left=0;
int16_t right=0;
int16_t current_speed_rpm=0;
int16_t lift_height_mm=-1;
volatile uint8_t telemetry_failsafe=0;
static uint32_t telemetry_last_tick=0;
static uint32_t telemetry_last_query_tick=0;
static uint32_t telemetry_control_last_tick=0;
static uint32_t telemetry_query_start_tick=0;
static uint8_t telemetry_rx_buf[TELEMETRY_RESPONSE_LEN]={0};
static uint8_t telemetry_rx_len=0;
static uint8_t telemetry_query_slave=0;
static uint8_t telemetry_query_step=0;
static uint8_t motor_speed_cmd_valid=0;
static int16_t motor_last_left_cmd=0;
static int16_t motor_last_right_cmd=0;
static uint32_t motor_speed_cmd_last_tick=0;
static int16_t motor_target_linear=0;
static int16_t motor_target_steer=0;
static uint32_t left_feedback_tick=0U;
static uint32_t right_feedback_tick=0U;
static uint32_t wheel_feedback_sequence=0U;
static uint32_t motor_write_sequence=0U;
static uint32_t left_write_ok_count=0U;
static uint32_t right_write_ok_count=0U;
static uint32_t left_write_fail_count=0U;
static uint32_t right_write_fail_count=0U;
static uint8_t left_write_echo_ok=0U;
static uint8_t right_write_echo_ok=0U;

typedef enum
{
    PC_TEST_IDLE = 0,
    PC_TEST_ACTIVE,
    PC_TEST_DONE,
    PC_TEST_STOPPED,
    PC_TEST_REJECTED,
    PC_TEST_CANCELLED,
    PC_TEST_BAD_COMMAND
} PcTestStatus;

typedef struct
{
    uint8_t active;
    int16_t linear;
    int16_t steer;
    uint32_t stop_tick;
    PcTestStatus status;
} PcTestControl;

typedef struct
{
    float filtered_error;
    int16_t trim;
    uint32_t last_feedback_sequence;
} StraightSyncController;

typedef struct
{
    uint8_t active;
    uint8_t terminal_latched;
    uint32_t request_tick;
    uint32_t terminal_tick;
    uint32_t last_zero_send_tick;
} NeutralStopController;

static PcTestControl pc_test_control={0};
static StraightSyncController straight_sync={0};
static NeutralStopController neutral_stop={0};
static volatile uint8_t pc_test_rx_ready=0U;
static volatile uint8_t pc_test_rx_length=0U;
static uint8_t pc_test_rx_buffer[PC_TEST_RX_BUFFER_SIZE]={0};

typedef struct
{
    float speed;
    float acceleration;
} MotorTrajectoryAxis;

typedef struct
{
    MotorTrajectoryAxis linear;
    MotorTrajectoryAxis steer;
    uint32_t last_update_tick;
    uint8_t initialized;
} MotorTrajectory;

typedef enum
{
    CASTER_ALIGN_IDLE = 0,
    CASTER_ALIGN_BRAKE,
    CASTER_ALIGN_CRAWL,
    CASTER_ALIGN_FAILED
} CasterAlignState;

typedef struct
{
    CasterAlignState state;
    uint8_t alignment_required;
    int8_t last_direction;
    int8_t last_turn_sign;
    int16_t previous_linear_request;
    int16_t previous_steer_request;
    uint32_t crawl_start_tick;
    uint32_t crawl_motion_start_tick;
    uint32_t crawl_match_start_tick;
} CasterAlignmentController;

static MotorTrajectory motor_trajectory={0};
static CasterAlignmentController caster_alignment={
    .state = CASTER_ALIGN_IDLE,
    .alignment_required = 1U
};
static int16_t motor_conditioned_linear=0;
static int16_t motor_conditioned_steer=0;
static uint8_t rc_ready=0;
static uint8_t rc_valid_frame_count=0;
static uint8_t rc_prev_channels_valid=0;
static int16_t rc_prev_ch3=RC_CH3_CENTER;
static int16_t rc_prev_ch4=RC_CH4_CENTER;
static int16_t rc_jump_candidate_ch3=RC_CH3_CENTER;
static int16_t rc_jump_candidate_ch4=RC_CH4_CENTER;
static uint8_t rc_jump_confirm_count=0;
static uint32_t rc_last_valid_frame_tick=0;
static uint32_t rc_not_ready_since_tick=0;
static uint32_t rc_last_zero_command_tick=0;
static uint8_t rc_zero_command_sent=0;
static uint8_t rc_lift_stop_sent=0;
static uint8_t rc_failsafe_stop_done=0;
static uint32_t rc_frame_lost_count=0U;
static uint32_t rc_not_ready_event_count=0U;
static uint32_t rc_recovery_count=0U;
static uint8_t rc_last_stop_reason=0U;

typedef enum
{
    RC_STOP_REASON_NONE = 0,
    RC_STOP_REASON_FAILSAFE = 1,
    RC_STOP_REASON_TIMEOUT = 2,
    RC_STOP_REASON_INVALID_FRAME = 3
} RcStopReason;

typedef struct
{
    uint8_t frame_seen;
    uint8_t accepted;
    uint8_t failsafe;
    uint8_t frame_lost;
    int16_t ch3;
    int16_t ch4;
    int16_t ch6;
    int16_t ch8;
    int16_t desired_speed;
    int16_t desired_steer;
    int16_t desired_left_rpm;
    int16_t desired_right_rpm;
    LiftState desired_lift_state;
    uint32_t last_frame_tick;
} RcLcdDebugData;

static RcLcdDebugData rc_lcd_debug={0};
static uint32_t rc_lcd_last_refresh_tick=0;
static char rc_lcd_line_cache[RC_LCD_ROWS][RC_LCD_LINE_CHARS + 1U];
static uint16_t rc_lcd_color_cache[RC_LCD_ROWS];
static uint8_t rc_lcd_cache_valid[RC_LCD_ROWS];

typedef enum
{
    TELEMETRY_QUERY_IDLE = 0,
    TELEMETRY_SEND_LEFT,
    TELEMETRY_WAIT_LEFT,
    TELEMETRY_SEND_RIGHT,
    TELEMETRY_WAIT_RIGHT,
    TELEMETRY_SEND_LIFT,
    TELEMETRY_WAIT_LIFT
} TelemetryQueryState;

static TelemetryQueryState telemetry_query_state=TELEMETRY_QUERY_IDLE;

static const char *pc_test_status_text(void)
{
    switch(pc_test_control.status)
    {
        case PC_TEST_ACTIVE:
            return "ACTIVE";
        case PC_TEST_DONE:
            return "DONE";
        case PC_TEST_STOPPED:
            return "STOPPED";
        case PC_TEST_REJECTED:
            return "REJECTED";
        case PC_TEST_CANCELLED:
            return "CANCELLED";
        case PC_TEST_BAD_COMMAND:
            return "BAD_COMMAND";
        default:
            return "IDLE";
    }
}

void PC_TestCommand_Receive(const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if(data == NULL || length == 0U)
    {
        return;
    }

    for(index = 0U; index < length; index++)
    {
        uint8_t byte = data[index];

        if(pc_test_rx_ready)
        {
            break;
        }
        if(byte == '\r')
        {
            continue;
        }
        if(byte == '\n')
        {
            if(pc_test_rx_length > 0U)
            {
                pc_test_rx_buffer[pc_test_rx_length] = '\0';
                pc_test_rx_ready = 1U;
            }
            break;
        }
        if(pc_test_rx_length < (PC_TEST_RX_BUFFER_SIZE - 1U))
        {
            pc_test_rx_buffer[pc_test_rx_length++] = byte;
        }
        else
        {
            pc_test_rx_length = 0U;
            pc_test_control.status = PC_TEST_BAD_COMMAND;
        }
    }
}

static void pc_test_cancel(PcTestStatus status)
{
    pc_test_control.active = 0U;
    pc_test_control.linear = 0;
    pc_test_control.steer = 0;
    pc_test_control.stop_tick = 0U;
    pc_test_control.status = status;
}

static uint8_t pc_test_take_command(char *command, uint32_t command_size)
{
    uint8_t length;

    if(!pc_test_rx_ready || command == NULL || command_size == 0U)
    {
        return 0U;
    }

    __disable_irq();
    length = pc_test_rx_length;
    if(length >= command_size)
    {
        length = (uint8_t)(command_size - 1U);
    }
    memcpy(command, pc_test_rx_buffer, length);
    command[length] = '\0';
    pc_test_rx_length = 0U;
    pc_test_rx_ready = 0U;
    __enable_irq();
    return 1U;
}

static uint8_t pc_test_interlocks_ready(void)
{
    return (rc_ready &&
            en_flag &&
            !emergency_stop &&
            !sbus_failsafe &&
            motor_target_linear == 0 &&
            motor_target_steer == 0 &&
            abs(left) <= PC_TEST_REST_SPEED_RPM &&
            abs(right) <= PC_TEST_REST_SPEED_RPM);
}

static void pc_test_process(uint32_t now)
{
    char command[PC_TEST_RX_BUFFER_SIZE];

    if(pc_test_take_command(command, sizeof(command)))
    {
        int linear;
        int steer;
        unsigned long duration_ms;
        char extra;

        if(strcmp(command, "STOP") == 0)
        {
            pc_test_cancel(PC_TEST_STOPPED);
        }
        else if(sscanf(command, "MOVE %d %d %lu %c",
                       &linear, &steer, &duration_ms, &extra) == 3)
        {
            uint8_t values_valid = (abs(linear) <= PC_TEST_MAX_LINEAR_RPM &&
                                    abs(steer) <= PC_TEST_MAX_STEER_CMD &&
                                    (linear != 0 || steer != 0) &&
                                    duration_ms >= PC_TEST_MIN_DURATION_MS &&
                                    duration_ms <= PC_TEST_MAX_DURATION_MS);

            if(values_valid && pc_test_interlocks_ready())
            {
                pc_test_control.active = 1U;
                pc_test_control.linear = (int16_t)linear;
                pc_test_control.steer = (int16_t)steer;
                pc_test_control.stop_tick = now + (uint32_t)duration_ms;
                pc_test_control.status = PC_TEST_ACTIVE;
            }
            else
            {
                pc_test_cancel(PC_TEST_REJECTED);
            }
        }
        else
        {
            pc_test_cancel(PC_TEST_BAD_COMMAND);
        }
    }

    if(pc_test_control.active)
    {
        if(!rc_ready || !en_flag || emergency_stop || sbus_failsafe ||
           motor_target_linear != 0 || motor_target_steer != 0)
        {
            pc_test_cancel(PC_TEST_CANCELLED);
        }
        else if((int32_t)(now - pc_test_control.stop_tick) >= 0)
        {
            pc_test_cancel(PC_TEST_DONE);
        }
    }
}

static uint32_t pc_test_remaining_ms(uint32_t now)
{
    if(!pc_test_control.active || (int32_t)(pc_test_control.stop_tick - now) <= 0)
    {
        return 0U;
    }
    return pc_test_control.stop_tick - now;
}

static const char *caster_alignment_state_text(void)
{
    switch(caster_alignment.state)
    {
        case CASTER_ALIGN_BRAKE:
            return "BRAKE";
        case CASTER_ALIGN_CRAWL:
            return "CRAWL";
        case CASTER_ALIGN_FAILED:
            return "FAILED";
        default:
            return "IDLE";
    }
}

static const char *telemetry_state_text(void)
{
#if MOTOR_TRAJECTORY_BENCH_TEST
    return "BENCH";
#else
    if(telemetry_failsafe)
    {
        return "FAILSAFE";
    }
    if(emergency_stop)
    {
        return "ESTOP";
    }
    if(!en_flag)
    {
        return "DISABLED";
    }
    if(caster_alignment.state != CASTER_ALIGN_IDLE)
    {
        return "ALIGN";
    }
    return "RUN";
#endif
}

static uint8_t telemetry_parse_i16_response(uint8_t *buf, uint8_t slave_addr, int16_t *value)
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

    *value = (int16_t)(((uint16_t)buf[3] << 8) | buf[4]);
    return 1;
}

static void telemetry_clear_uart_rx(UART_HandleTypeDef *huart)
{
    uint8_t dummy;
    uint8_t guard = 32;

    __HAL_UART_CLEAR_OREFLAG(huart);
    huart->ErrorCode = HAL_UART_ERROR_NONE;
    while(guard-- > 0 && HAL_UART_Receive(huart, &dummy, 1, 0) == HAL_OK)
    {
    }
}

static void telemetry_reset_rx_buffer(void)
{
    memset(telemetry_rx_buf, 0, sizeof(telemetry_rx_buf));
    telemetry_rx_len = 0;
}

static uint8_t telemetry_poll_uart_response(UART_HandleTypeDef *huart)
{
    uint8_t byte;

    while(telemetry_rx_len < TELEMETRY_RESPONSE_LEN)
    {
        HAL_StatusTypeDef sta = HAL_UART_Receive(huart, &byte, 1, 0);
        if(sta == HAL_OK)
        {
            telemetry_rx_buf[telemetry_rx_len++] = byte;
            continue;
        }

        if(sta == HAL_ERROR)
        {
            __HAL_UART_CLEAR_OREFLAG(huart);
            huart->ErrorCode = HAL_UART_ERROR_NONE;
        }
        break;
    }

    return (telemetry_rx_len >= TELEMETRY_RESPONSE_LEN);
}

static void telemetry_build_read_cmd(uint8_t slave_addr, uint16_t reg, uint8_t *cmd)
{
    cmd[0] = slave_addr;
    cmd[1] = 0x03;
    cmd[2] = (uint8_t)(reg >> 8);
    cmd[3] = (uint8_t)(reg & 0xFF);
    cmd[4] = 0x00;
    cmd[5] = 0x01;
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = crc >> 8;
}

static uint8_t telemetry_start_motor_query(uint8_t slave_addr, TelemetryQueryState next_state)
{
    uint8_t cmd[8];

    telemetry_reset_rx_buffer();
    telemetry_clear_uart_rx(&huart2);
    telemetry_build_read_cmd(slave_addr, 0x5000, cmd);

    if(RS485_SendPacketTimeout(cmd, 8, TELEMETRY_TX_TIMEOUT_MS) != HAL_OK)
    {
        telemetry_query_state = TELEMETRY_QUERY_IDLE;
        return 0;
    }

    telemetry_query_slave = slave_addr;
    telemetry_query_start_tick = HAL_GetTick();
    telemetry_query_state = next_state;
    return 1;
}

static uint8_t telemetry_start_lift_query(void)
{
    uint8_t cmd[8];

    telemetry_reset_rx_buffer();
    telemetry_clear_uart_rx(&huart3);
    telemetry_build_read_cmd(0x01, 0x0002, cmd);

    if(RS485_SendPacket2Timeout(cmd, 8, TELEMETRY_TX_TIMEOUT_MS) != HAL_OK)
    {
        telemetry_query_state = TELEMETRY_QUERY_IDLE;
        return 0;
    }

    telemetry_query_slave = 0x01;
    telemetry_query_start_tick = HAL_GetTick();
    telemetry_query_state = TELEMETRY_WAIT_LIFT;
    return 1;
}

static void telemetry_update_cached_speed(void)
{
    current_speed_rpm = (int16_t)((left - right) / 2);
}

static void telemetry_finish_motor_query(uint8_t response_ready)
{
    int16_t value;

    if(response_ready && telemetry_parse_i16_response(telemetry_rx_buf, telemetry_query_slave, &value))
    {
        if(telemetry_query_slave == 1)
        {
            left = value;
            left_feedback_tick = HAL_GetTick();
        }
        else if(telemetry_query_slave == 2)
        {
            right = value;
            right_feedback_tick = HAL_GetTick();
            wheel_feedback_sequence++;
        }
        telemetry_update_cached_speed();
    }
}

static void telemetry_finish_lift_query(uint8_t response_ready)
{
    int16_t value;

    if(response_ready && telemetry_parse_i16_response(telemetry_rx_buf, 0x01, &value))
    {
        lift_height_mm = value;
    }
}

static void telemetry_advance_query_step(void)
{
    telemetry_query_step++;
    if(telemetry_query_step >= 3)
    {
        telemetry_query_step = 0;
    }
}

static void telemetry_abort_pending_query(void)
{
    if(telemetry_query_state == TELEMETRY_WAIT_LEFT ||
       telemetry_query_state == TELEMETRY_WAIT_RIGHT ||
       telemetry_query_state == TELEMETRY_WAIT_LIFT)
    {
        telemetry_query_state = TELEMETRY_QUERY_IDLE;
        telemetry_reset_rx_buffer();
        telemetry_clear_uart_rx(&huart2);
        telemetry_clear_uart_rx(&huart3);
    }
}

static void telemetry_schedule_current_step(void)
{
    switch(telemetry_query_step)
    {
        case 0:
            telemetry_query_state = TELEMETRY_SEND_LEFT;
            break;
        case 1:
            telemetry_query_state = TELEMETRY_SEND_RIGHT;
            break;
        default:
            telemetry_query_state = TELEMETRY_SEND_LIFT;
            break;
    }
}

static void telemetry_control_command_begin(void)
{
    /* 遥控/电机/升降写命令优先：发控制命令前丢弃后台读取，避免旧回包污染下一帧。 */
    telemetry_abort_pending_query();
}

static void telemetry_control_command_end(void)
{
    telemetry_control_last_tick = HAL_GetTick();
}

static float float_clamp(float value, float minimum, float maximum)
{
    if(value > maximum)
    {
        return maximum;
    }
    if(value < minimum)
    {
        return minimum;
    }
    return value;
}

static float float_abs(float value)
{
    return (value < 0.0f) ? -value : value;
}

static int16_t motor_trajectory_round(float value)
{
    return (value >= 0.0f) ? (int16_t)(value + 0.5f) : (int16_t)(value - 0.5f);
}

static void motor_trajectory_reset(void)
{
    motor_trajectory.linear.speed = 0.0f;
    motor_trajectory.linear.acceleration = 0.0f;
    motor_trajectory.steer.speed = 0.0f;
    motor_trajectory.steer.acceleration = 0.0f;
    motor_trajectory.last_update_tick = 0U;
    motor_trajectory.initialized = 0U;
}

static void neutral_stop_reset(void)
{
    neutral_stop.active = 0U;
    neutral_stop.terminal_latched = 0U;
    neutral_stop.request_tick = 0U;
    neutral_stop.terminal_tick = 0U;
    neutral_stop.last_zero_send_tick = 0U;
}

static void neutral_stop_update_request(int16_t requested_linear,
                                        int16_t requested_steer,
                                        uint32_t now)
{
    if(requested_linear == 0 && requested_steer == 0)
    {
        if(!neutral_stop.active)
        {
            neutral_stop.active = 1U;
            neutral_stop.terminal_latched = 0U;
            neutral_stop.request_tick = now;
            neutral_stop.terminal_tick = 0U;
            neutral_stop.last_zero_send_tick = 0U;
        }
    }
    else
    {
        neutral_stop_reset();
    }
}

static void neutral_stop_apply_terminal(int16_t *left_cmd,
                                        int16_t *right_cmd,
                                        uint32_t now)
{
    if(left_cmd == NULL || right_cmd == NULL || !neutral_stop.active)
    {
        return;
    }

    if(float_abs(motor_trajectory.linear.speed) <= NEUTRAL_TERMINAL_SPEED_RPM &&
       float_abs(motor_trajectory.steer.speed) <= NEUTRAL_TERMINAL_STEER_CMD)
    {
        motor_trajectory.linear.speed = 0.0f;
        motor_trajectory.linear.acceleration = 0.0f;
        motor_trajectory.steer.speed = 0.0f;
        motor_trajectory.steer.acceleration = 0.0f;
        *left_cmd = 0;
        *right_cmd = 0;

        if(!neutral_stop.terminal_latched)
        {
            neutral_stop.terminal_latched = 1U;
            neutral_stop.terminal_tick = now;
            neutral_stop.last_zero_send_tick = 0U;
        }
    }
}

static void motor_trajectory_axis_update(MotorTrajectoryAxis *axis,
                                         float target_speed,
                                         float max_accel,
                                         float max_decel,
                                         float max_jerk,
                                         float max_stop_jerk,
                                         float dt)
{
    float speed_error = target_speed - axis->speed;
    float target_acceleration;
    float acceleration_limit;
    float acceleration_step;
    float next_speed;

    /* 只有速度误差和加速度都足够小时才锁定目标，避免末端力矩突卸。 */
    if(float_abs(speed_error) <= MOTOR_TRAJECTORY_TARGET_EPSILON_RPM &&
       float_abs(axis->acceleration) <= MOTOR_TRAJECTORY_ACCEL_EPSILON_RPM_PER_S)
    {
        axis->speed = target_speed;
        axis->acceleration = 0.0f;
        return;
    }

    /* Select accel/decel by whether speed magnitude is rising, not by direction. */
    acceleration_limit = ((axis->speed * speed_error) < 0.0f) ? max_decel : max_accel;
    target_acceleration = float_clamp(speed_error * MOTOR_TRAJECTORY_TRACKING_GAIN_PER_S,
                                      -acceleration_limit,
                                      acceleration_limit);
    acceleration_step = ((target_speed == 0.0f) ? max_stop_jerk : max_jerk) * dt;
    axis->acceleration += float_clamp(target_acceleration - axis->acceleration,
                                      -acceleration_step,
                                      acceleration_step);
    next_speed = axis->speed + axis->acceleration * dt;

    /* 防止浮点积分在目标点附近来回越过；此时加速度已被前面的 jerk 限制收小。 */
    axis->speed = next_speed;
    if(float_abs(target_speed - axis->speed) <= MOTOR_TRAJECTORY_TARGET_EPSILON_RPM &&
       float_abs(axis->acceleration) <= MOTOR_TRAJECTORY_ACCEL_EPSILON_RPM_PER_S)
    {
        axis->speed = target_speed;
        axis->acceleration = 0.0f;
    }
}

static void motor_trajectory_update(int16_t target_linear,
                                    int16_t target_steer,
                                    int16_t *left_rpm,
                                    int16_t *right_rpm)
{
    uint32_t now = HAL_GetTick();
    int16_t smoothed_linear;
    int16_t smoothed_steer;

    if(!motor_trajectory.initialized)
    {
        motor_trajectory.last_update_tick = now;
        motor_trajectory.initialized = 1U;
    }
    else if((now - motor_trajectory.last_update_tick) >= MOTOR_TRAJECTORY_PERIOD_MS)
    {
        uint32_t elapsed_ms = now - motor_trajectory.last_update_tick;
        float dt;

        if(elapsed_ms > MOTOR_TRAJECTORY_MAX_DT_MS)
        {
            elapsed_ms = MOTOR_TRAJECTORY_MAX_DT_MS;
        }
        dt = (float)elapsed_ms * 0.001f;
        motor_trajectory_axis_update(&motor_trajectory.linear,
                                     (float)target_linear,
                                     MOTOR_LINEAR_MAX_ACCEL_RPM_PER_S,
                                     MOTOR_LINEAR_MAX_DECEL_RPM_PER_S,
                                     MOTOR_LINEAR_MAX_JERK_RPM_PER_S2,
                                     MOTOR_LINEAR_MAX_STOP_JERK_RPM_PER_S2,
                                     dt);
        motor_trajectory_axis_update(&motor_trajectory.steer,
                                     (float)target_steer,
                                     MOTOR_STEER_MAX_ACCEL_CMD_PER_S,
                                     MOTOR_STEER_MAX_DECEL_CMD_PER_S,
                                     MOTOR_STEER_MAX_JERK_CMD_PER_S2,
                                     MOTOR_STEER_MAX_STOP_JERK_CMD_PER_S2,
                                     dt);
        motor_trajectory.last_update_tick = now;
    }

    /* 保持原来的整数混控规则，避免平滑器改变已验证的左右轮转向比例。 */
    smoothed_linear = motor_trajectory_round(motor_trajectory.linear.speed);
    smoothed_steer = motor_trajectory_round(motor_trajectory.steer.speed);
    *left_rpm = smoothed_linear + smoothed_steer / 4;
    *right_rpm = smoothed_linear - smoothed_steer / 4;
}

static int8_t caster_sign_i16(int16_t value)
{
    if(value > 0)
    {
        return 1;
    }
    if(value < 0)
    {
        return -1;
    }
    return 0;
}

static void caster_alignment_reset(void)
{
    caster_alignment.state = CASTER_ALIGN_IDLE;
    caster_alignment.alignment_required = 1U;
    caster_alignment.last_direction = 0;
    caster_alignment.last_turn_sign = 0;
    caster_alignment.previous_linear_request = 0;
    caster_alignment.previous_steer_request = 0;
    caster_alignment.crawl_start_tick = 0U;
    caster_alignment.crawl_motion_start_tick = 0U;
    caster_alignment.crawl_match_start_tick = 0U;
    motor_conditioned_linear = 0;
    motor_conditioned_steer = 0;
}

static void caster_alignment_update(int16_t requested_linear,
                                    int16_t requested_steer,
                                    uint32_t now,
                                    int16_t *conditioned_linear,
                                    int16_t *conditioned_steer)
{
    int8_t requested_direction = caster_sign_i16(requested_linear);
    uint8_t wants_straight = (abs(requested_steer) <= CASTER_ALIGN_STRAIGHT_STEER_CMD);
    uint8_t direction_changed = (requested_direction != 0 &&
                                 caster_alignment.last_direction != 0 &&
                                 requested_direction != caster_alignment.last_direction);
    uint8_t returned_from_turn = (wants_straight &&
                                  abs(caster_alignment.previous_steer_request) >= CASTER_ALIGN_TRIGGER_STEER_CMD);

    if(abs(requested_steer) >= CASTER_ALIGN_TRIGGER_STEER_CMD)
    {
        caster_alignment.alignment_required = 1U;
        caster_alignment.last_turn_sign = caster_sign_i16(requested_steer);
    }
    if(direction_changed || returned_from_turn)
    {
        caster_alignment.alignment_required = 1U;
    }

    if(requested_direction == 0)
    {
        caster_alignment.state = CASTER_ALIGN_IDLE;
        *conditioned_linear = 0;
        *conditioned_steer = requested_steer;
    }
    else if(!wants_straight)
    {
        /* Operator steering always overrides the automatic straight-line alignment. */
        caster_alignment.state = CASTER_ALIGN_IDLE;
        *conditioned_linear = requested_linear;
        *conditioned_steer = requested_steer;
    }
    else
    {
        if(caster_alignment.state == CASTER_ALIGN_IDLE &&
           caster_alignment.alignment_required)
        {
            caster_alignment.state = CASTER_ALIGN_BRAKE;
        }

        if(caster_alignment.state == CASTER_ALIGN_BRAKE)
        {
            *conditioned_linear = 0;
            *conditioned_steer = 0;

            if(float_abs(motor_trajectory.linear.speed) <= CASTER_ALIGN_REST_SPEED_RPM &&
               float_abs(motor_trajectory.steer.speed) <= CASTER_ALIGN_REST_SPEED_RPM &&
               float_abs(motor_trajectory.linear.acceleration) <= CASTER_ALIGN_REST_ACCEL_RPM_PER_S &&
               float_abs(motor_trajectory.steer.acceleration) <= CASTER_ALIGN_REST_ACCEL_RPM_PER_S)
            {
                caster_alignment.state = CASTER_ALIGN_CRAWL;
                caster_alignment.crawl_start_tick = now;
                caster_alignment.crawl_motion_start_tick = 0U;
                caster_alignment.crawl_match_start_tick = 0U;
            }
        }

        if(caster_alignment.state == CASTER_ALIGN_CRAWL)
        {
            int16_t crawl_linear = (int16_t)(requested_direction * CASTER_ALIGN_CRAWL_RPM);
            int16_t left_direction_speed;
            int16_t right_direction_speed;
            uint8_t left_moving_in_direction;
            uint8_t right_moving_in_direction;
            uint8_t wheel_speeds_matched;
            uint8_t moving_duration_complete;
            uint8_t crawl_timed_out;

            if(abs(requested_linear) < CASTER_ALIGN_MIN_BOOST_REQUEST_RPM)
            {
                crawl_linear = requested_linear;
            }

            left_direction_speed = (int16_t)(requested_direction * left);
            right_direction_speed = (int16_t)(requested_direction * -right);

            *conditioned_linear = crawl_linear;
            if(requested_direction > 0 &&
               abs(crawl_linear) >= CASTER_ALIGN_CRAWL_RPM &&
               (caster_alignment.crawl_motion_start_tick == 0U ||
                left_direction_speed >
                (right_direction_speed + CASTER_ALIGN_MATCH_ERROR_RPM)))
            {
                int16_t forward_bias_cmd =
                    ((now - caster_alignment.crawl_start_tick) <
                     CASTER_ALIGN_FORWARD_PRELOAD_DURATION_MS) ?
                    CASTER_ALIGN_FORWARD_PRELOAD_BIAS_CMD :
                    CASTER_ALIGN_FORWARD_LAUNCH_BIAS_CMD;

                /* Forward breakaway: hold back the early left wheel and help the late right wheel. */
                *conditioned_steer = -forward_bias_cmd;
            }
            else if(abs(crawl_linear) >= CASTER_ALIGN_CRAWL_RPM &&
               caster_alignment.last_turn_sign != 0)
            {
                *conditioned_steer = (int16_t)(-caster_alignment.last_turn_sign *
                                               CASTER_ALIGN_COUNTER_STEER_CMD);
            }
            else
            {
                *conditioned_steer = 0;
            }

            left_moving_in_direction = (requested_direction > 0) ?
                                       (left >= 1) : (left <= -1);
            right_moving_in_direction = (requested_direction > 0) ?
                                        (right <= -1) : (right >= 1);
            if(caster_alignment.crawl_motion_start_tick == 0U &&
               left_moving_in_direction && right_moving_in_direction)
            {
                caster_alignment.crawl_motion_start_tick = now;
            }

            wheel_speeds_matched = (left_moving_in_direction &&
                                    right_moving_in_direction &&
                                    abs(left_direction_speed - right_direction_speed) <=
                                    CASTER_ALIGN_MATCH_ERROR_RPM);
            if(wheel_speeds_matched)
            {
                if(caster_alignment.crawl_match_start_tick == 0U)
                {
                    caster_alignment.crawl_match_start_tick = now;
                }
            }
            else
            {
                caster_alignment.crawl_match_start_tick = 0U;
            }

            moving_duration_complete = (caster_alignment.crawl_motion_start_tick != 0U &&
                                        (now - caster_alignment.crawl_motion_start_tick) >=
                                        CASTER_ALIGN_MOVING_DURATION_MS &&
                                        caster_alignment.crawl_match_start_tick != 0U &&
                                        (now - caster_alignment.crawl_match_start_tick) >=
                                        CASTER_ALIGN_MATCH_DURATION_MS);
            crawl_timed_out = ((now - caster_alignment.crawl_start_tick) >=
                               CASTER_ALIGN_MAX_DURATION_MS);

            if(moving_duration_complete)
            {
                caster_alignment.state = CASTER_ALIGN_IDLE;
                caster_alignment.alignment_required = 0U;
                caster_alignment.last_turn_sign = 0;
                caster_alignment.crawl_motion_start_tick = 0U;
                caster_alignment.crawl_match_start_tick = 0U;
                *conditioned_linear = requested_linear;
                *conditioned_steer = requested_steer;
            }
            else if(crawl_timed_out)
            {
                caster_alignment.state = CASTER_ALIGN_FAILED;
                caster_alignment.alignment_required = 1U;
                caster_alignment.crawl_motion_start_tick = 0U;
                caster_alignment.crawl_match_start_tick = 0U;
                *conditioned_linear = 0;
                *conditioned_steer = 0;
            }
        }
        else if(caster_alignment.state == CASTER_ALIGN_IDLE)
        {
            *conditioned_linear = requested_linear;
            *conditioned_steer = requested_steer;
        }
        else if(caster_alignment.state == CASTER_ALIGN_FAILED)
        {
            *conditioned_linear = 0;
            *conditioned_steer = 0;
        }
    }

    if(requested_direction != 0)
    {
        caster_alignment.last_direction = requested_direction;
    }
    caster_alignment.previous_linear_request = requested_linear;
    caster_alignment.previous_steer_request = requested_steer;
    motor_conditioned_linear = *conditioned_linear;
    motor_conditioned_steer = *conditioned_steer;
}

static void straight_sync_reset(void)
{
    straight_sync.filtered_error = 0.0f;
    straight_sync.trim = 0;
    straight_sync.last_feedback_sequence = wheel_feedback_sequence;
}

static void straight_sync_apply(int16_t *left_cmd, int16_t *right_cmd, uint32_t now)
{
    uint8_t straight_running;
    int8_t travel_direction;

    if(left_cmd == NULL || right_cmd == NULL)
    {
        return;
    }

    straight_running = (caster_alignment.state == CASTER_ALIGN_IDLE &&
                        motor_conditioned_steer == 0 &&
                        abs(*left_cmd) >= STRAIGHT_SYNC_MIN_COMMAND_RPM &&
                        abs(*right_cmd) >= STRAIGHT_SYNC_MIN_COMMAND_RPM &&
                        ((*left_cmd > 0 && *right_cmd > 0) ||
                         (*left_cmd < 0 && *right_cmd < 0)) &&
                        (now - left_feedback_tick) <= STRAIGHT_SYNC_FEEDBACK_MAX_AGE_MS &&
                        (now - right_feedback_tick) <= STRAIGHT_SYNC_FEEDBACK_MAX_AGE_MS);

    if(!straight_running)
    {
        straight_sync_reset();
        return;
    }

    if(straight_sync.last_feedback_sequence != wheel_feedback_sequence)
    {
        int16_t physical_left_rpm = left;
        int16_t physical_right_rpm = (int16_t)(-right);
        float travel_left_rpm;
        float travel_right_rpm;

        straight_sync.last_feedback_sequence = wheel_feedback_sequence;
        travel_direction = (*left_cmd > 0) ? 1 : -1;
        travel_left_rpm = (float)(travel_direction * physical_left_rpm);
        travel_right_rpm = (float)(travel_direction * physical_right_rpm);

        if(travel_left_rpm >= STRAIGHT_SYNC_LAUNCH_FAST_RPM &&
           travel_right_rpm <= STRAIGHT_SYNC_LAUNCH_STALLED_RPM)
        {
            /* Left broke static friction first: only reduce the moving wheel. */
            straight_sync.filtered_error = travel_left_rpm - travel_right_rpm;
            straight_sync.trim = STRAIGHT_SYNC_LAUNCH_TRIM_RPM;
        }
        else if(travel_right_rpm >= STRAIGHT_SYNC_LAUNCH_FAST_RPM &&
                travel_left_rpm <= STRAIGHT_SYNC_LAUNCH_STALLED_RPM)
        {
            /* Right broke static friction first: correction is symmetric. */
            straight_sync.filtered_error = travel_left_rpm - travel_right_rpm;
            straight_sync.trim = -STRAIGHT_SYNC_LAUNCH_TRIM_RPM;
        }
        else if(travel_left_rpm >= 1.0f && travel_right_rpm >= 1.0f)
        {
            float error = travel_left_rpm - travel_right_rpm;
            straight_sync.filtered_error += STRAIGHT_SYNC_FILTER_ALPHA *
                                            (error - straight_sync.filtered_error);

            if(straight_sync.filtered_error >= STRAIGHT_SYNC_ERROR_THRESHOLD_RPM)
            {
                straight_sync.trim = STRAIGHT_SYNC_MAX_TRIM_RPM;
            }
            else if(straight_sync.filtered_error <= -STRAIGHT_SYNC_ERROR_THRESHOLD_RPM)
            {
                straight_sync.trim = -STRAIGHT_SYNC_MAX_TRIM_RPM;
            }
            else
            {
                straight_sync.trim = 0;
            }
        }
        else
        {
            straight_sync.filtered_error = 0.0f;
            straight_sync.trim = 0;
        }
    }

    /* Never boost the slow wheel: reduce only the wheel that is running ahead. */
    if(straight_sync.trim > 0)
    {
        *left_cmd -= (int16_t)((*left_cmd > 0) ? straight_sync.trim : -straight_sync.trim);
    }
    else if(straight_sync.trim < 0)
    {
        int16_t trim_abs = (int16_t)(-straight_sync.trim);
        *right_cmd -= (int16_t)((*right_cmd > 0) ? trim_abs : -trim_abs);
    }
}

#if MOTOR_TRAJECTORY_BENCH_TEST
static void motor_trajectory_bench_test_update(void)
{
    uint32_t phase_ms = HAL_GetTick() % MOTOR_TRAJECTORY_BENCH_CYCLE_MS;
    int16_t commanded_left_rpm;
    int16_t commanded_right_rpm;

    if(phase_ms < 500U)
    {
        motor_target_linear = 0;
        motor_target_steer = 0;
    }
    else if(phase_ms < 2500U)
    {
        motor_target_linear = 30;
        motor_target_steer = 0;
    }
    else if(phase_ms < 3500U)
    {
        motor_target_linear = 0;
        motor_target_steer = 0;
    }
    else if(phase_ms < 4500U)
    {
        motor_target_linear = 0;
        motor_target_steer = 20;
    }
    else if(phase_ms < 7000U)
    {
        motor_target_linear = 30;
        motor_target_steer = 0;
    }
    else if(phase_ms < 9000U)
    {
        motor_target_linear = -20;
        motor_target_steer = 0;
    }
    else
    {
        motor_target_linear = 0;
        motor_target_steer = 0;
    }

    caster_alignment_update(motor_target_linear,
                            motor_target_steer,
                            HAL_GetTick(),
                            &motor_conditioned_linear,
                            &motor_conditioned_steer);
    motor_trajectory_update(motor_conditioned_linear,
                            motor_conditioned_steer,
                            &commanded_left_rpm,
                            &commanded_right_rpm);

    /* Bench mode exposes the trajectory but never calls speed_set(). */
    motor_last_left_cmd = commanded_left_rpm;
    motor_last_right_cmd = -commanded_right_rpm;
    motor_speed_cmd_valid = 2U;
}
#endif

static void motor_speed_command_invalidate(void)
{
    motor_speed_cmd_valid = 0;
    motor_target_linear = 0;
    motor_target_steer = 0;
    if(pc_test_control.active)
    {
        pc_test_cancel(PC_TEST_CANCELLED);
    }
    straight_sync_reset();
    neutral_stop_reset();
    motor_trajectory_reset();
    caster_alignment_reset();
}

static int16_t rc_desired_speed_from_ch3(int16_t ch3)
{
    return joystick_deadzone(ch3, RC_CH3_CENTER, RC_STICK_NEUTRAL_DEADZONE) / 32;
}

static int16_t rc_desired_steer_from_ch4(int16_t ch4)
{
    return joystick_deadzone(ch4, RC_CH4_CENTER, RC_STICK_NEUTRAL_DEADZONE) / 32;
}

static LiftState rc_desired_lift_from_ch8(int16_t ch8)
{
    if(ch8 < 500)
    {
        return LIFT_UP;
    }
    if(ch8 > 1500)
    {
        return LIFT_DOWN;
    }
    return LIFT_STOP;
}

static const char *rc_lift_state_text(LiftState state)
{
    switch(state)
    {
        case LIFT_UP:
            return "UP";
        case LIFT_DOWN:
            return "DOWN";
        default:
            return "STOP";
    }
}

static void rc_lcd_show_line(uint8_t row, uint16_t color, const char *fmt, ...)
{
    char line[RC_LCD_LINE_CHARS + 1U];
    size_t len;
    va_list args;

    if(row >= RC_LCD_ROWS)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    len = strlen(line);
    if(len < RC_LCD_LINE_CHARS)
    {
        memset(&line[len], ' ', RC_LCD_LINE_CHARS - len);
        line[RC_LCD_LINE_CHARS] = '\0';
    }
    else
    {
        line[RC_LCD_LINE_CHARS] = '\0';
    }

    if(rc_lcd_cache_valid[row] &&
       rc_lcd_color_cache[row] == color &&
       strcmp(rc_lcd_line_cache[row], line) == 0)
    {
        return;
    }

    LCD_ShowString(0, row * RC_LCD_ROW_STEP, (uint8_t *)line, color, BLACK, RC_LCD_FONT_SIZE, 0);
    strcpy(rc_lcd_line_cache[row], line);
    rc_lcd_color_cache[row] = color;
    rc_lcd_cache_valid[row] = 1;
}

static void rc_lcd_init_screen(void)
{
    LCD_Fill(0,0,LCD_W, LCD_H,BLACK);
    LCD_DrawRectangle(0, 0, LCD_W - 1, LCD_H - 1, WHITE);
    LCD_Fill(0, LCD_H - 16, LCD_W / 4, LCD_H, RED);
    LCD_Fill(LCD_W / 4, LCD_H - 16, LCD_W / 2, LCD_H, GREEN);
    LCD_Fill(LCD_W / 2, LCD_H - 16, (LCD_W * 3) / 4, LCD_H, BLUE);
    LCD_Fill((LCD_W * 3) / 4, LCD_H - 16, LCD_W, LCD_H, YELLOW);
    memset(rc_lcd_cache_valid, 0, sizeof(rc_lcd_cache_valid));
    rc_lcd_show_line(0, CYAN, "LCD OK / RC INPUT DEBUG");
    rc_lcd_show_line(1, WHITE, "Waiting SBUS frame...");
}

static void rc_lcd_capture_frame(const int16_t ch[SBUS_NUM_CHANNELS],
                                 uint8_t accepted,
                                 uint8_t failsafe,
                                 uint8_t frame_lost,
                                 uint32_t now)
{
    int16_t desired_speed = rc_desired_speed_from_ch3(ch[2]);
    int16_t desired_steer = rc_desired_steer_from_ch4(ch[3]);

    rc_lcd_debug.frame_seen = 1;
    rc_lcd_debug.accepted = accepted;
    rc_lcd_debug.failsafe = failsafe;
    rc_lcd_debug.frame_lost = frame_lost;
    rc_lcd_debug.ch3 = ch[2];
    rc_lcd_debug.ch4 = ch[3];
    rc_lcd_debug.ch6 = ch[5];
    rc_lcd_debug.ch8 = ch[7];
    rc_lcd_debug.desired_speed = desired_speed;
    rc_lcd_debug.desired_steer = desired_steer;
    rc_lcd_debug.desired_left_rpm = desired_speed + desired_steer / 4;
    rc_lcd_debug.desired_right_rpm = -(desired_speed - desired_steer / 4);
    rc_lcd_debug.desired_lift_state = rc_desired_lift_from_ch8(ch[7]);
    rc_lcd_debug.last_frame_tick = now;
}

static void rc_lcd_process(uint32_t now)
{
    uint32_t age_ms;

    if((now - rc_lcd_last_refresh_tick) < RC_LCD_REFRESH_MS)
    {
        return;
    }
    rc_lcd_last_refresh_tick = now;

    rc_lcd_show_line(0, CYAN, "RC INPUT DEBUG");

    if(!rc_lcd_debug.frame_seen)
    {
        rc_lcd_show_line(1, YELLOW, "Waiting SBUS frame...");
        rc_lcd_show_line(2, WHITE, "");
        rc_lcd_show_line(3, WHITE, "");
        rc_lcd_show_line(4, WHITE, "");
        rc_lcd_show_line(5, WHITE, "");
        rc_lcd_show_line(6, WHITE, "");
        rc_lcd_show_line(7, WHITE, "");
        return;
    }

    age_ms = now - rc_lcd_debug.last_frame_tick;
    rc_lcd_show_line(1, rc_ready ? GREEN : RED,
                     "STAT:%s ACC:%s AGE:%lu",
                     rc_ready ? "READY" : "STOP",
                     rc_lcd_debug.accepted ? "YES" : "NO",
                     (unsigned long)age_ms);
    rc_lcd_show_line(2, WHITE, "FS:%u LOST:%u EN:%u EST:%d",
                     (unsigned int)(sbus_failsafe || rc_lcd_debug.failsafe),
                     (unsigned int)rc_lcd_debug.frame_lost,
                     (unsigned int)en_flag,
                     emergency_stop);
    rc_lcd_show_line(3, WHITE, "CH3:%4d CH4:%4d",
                     rc_lcd_debug.ch3,
                     rc_lcd_debug.ch4);
    rc_lcd_show_line(4, WHITE, "CH6:%4d CH8:%4d",
                     rc_lcd_debug.ch6,
                     rc_lcd_debug.ch8);
    rc_lcd_show_line(5, YELLOW, "SPD:%+4d STR:%+4d",
                     rc_lcd_debug.desired_speed,
                     rc_lcd_debug.desired_steer);
    rc_lcd_show_line(6, YELLOW, "LSET:%+4d RSET:%+4d",
                     rc_lcd_debug.desired_left_rpm,
                     rc_lcd_debug.desired_right_rpm);
    rc_lcd_show_line(7, CYAN, "LIFT:%s",
                     rc_lift_state_text(rc_lcd_debug.desired_lift_state));
}

static uint8_t rc_channel_in_range(int16_t value)
{
    return (value >= SBUS_CHANNEL_MIN && value <= SBUS_CHANNEL_MAX);
}

static uint8_t rc_control_channels_in_range(const int16_t ch[SBUS_NUM_CHANNELS])
{
    return (rc_channel_in_range(ch[2]) &&
            rc_channel_in_range(ch[3]) &&
            rc_channel_in_range(ch[5]) &&
            rc_channel_in_range(ch[7]));
}

static uint8_t rc_sticks_neutral(const int16_t ch[SBUS_NUM_CHANNELS])
{
    return (abs(ch[2] - RC_CH3_CENTER) <= RC_STICK_NEUTRAL_DEADZONE &&
            abs(ch[3] - RC_CH4_CENTER) <= RC_STICK_NEUTRAL_DEADZONE);
}

static void rc_jump_confirmation_reset(void)
{
    rc_jump_candidate_ch3 = RC_CH3_CENTER;
    rc_jump_candidate_ch4 = RC_CH4_CENTER;
    rc_jump_confirm_count = 0U;
}

static uint8_t rc_stick_jump_ok(const int16_t ch[SBUS_NUM_CHANNELS])
{
    if(!rc_prev_channels_valid)
    {
        rc_jump_confirmation_reset();
        return 1;
    }

    /* 回中意味着减速/停车，第一帧立即接受，不给松杆停车增加确认延迟。 */
    if(rc_sticks_neutral(ch))
    {
        rc_jump_confirmation_reset();
        return 1;
    }

    if(abs(ch[2] - rc_prev_ch3) <= RC_STICK_JUMP_THRESHOLD &&
       abs(ch[3] - rc_prev_ch4) <= RC_STICK_JUMP_THRESHOLD)
    {
        rc_jump_confirmation_reset();
        return 1;
    }

    /*
     * 大幅快速推杆先冻结旧目标；连续两帧方向一致且数值接近时再接受。
     * 这样单帧毛刺不会驱动车辆，合法快速操作也不会被锁回未就绪状态。
     */
    if(rc_jump_confirm_count == 0U ||
       abs(ch[2] - rc_jump_candidate_ch3) > RC_STICK_JUMP_CONFIRM_TOLERANCE ||
       abs(ch[3] - rc_jump_candidate_ch4) > RC_STICK_JUMP_CONFIRM_TOLERANCE)
    {
        rc_jump_candidate_ch3 = ch[2];
        rc_jump_candidate_ch4 = ch[3];
        rc_jump_confirm_count = 1U;
        return 0;
    }

    if(rc_jump_confirm_count < RC_STICK_JUMP_CONFIRM_FRAMES)
    {
        rc_jump_confirm_count++;
    }

    if(rc_jump_confirm_count >= RC_STICK_JUMP_CONFIRM_FRAMES)
    {
        rc_jump_confirmation_reset();
        return 1;
    }

    return 0;
}

typedef enum
{
    RC_FRAME_REJECTED = 0,
    RC_FRAME_ACCEPTED,
    RC_FRAME_JUMP_PENDING
} RcFrameTrustResult;

static RcFrameTrustResult rc_frame_trust_result(const int16_t ch[SBUS_NUM_CHANNELS],
                                                uint8_t failsafe,
                                                uint8_t frame_lost)
{
    /*
     * frame_lost only reports an isolated missing RF frame.  The current SBUS
     * frame remains usable; persistent loss is covered by failsafe and timeout.
     */
    (void)frame_lost;
    if(failsafe)
    {
        return RC_FRAME_REJECTED;
    }

    if(!rc_control_channels_in_range(ch))
    {
        return RC_FRAME_REJECTED;
    }

    if(!rc_ready && !rc_sticks_neutral(ch))
    {
        return RC_FRAME_REJECTED;
    }

    if(!rc_stick_jump_ok(ch))
    {
        return RC_FRAME_JUMP_PENDING;
    }

    return RC_FRAME_ACCEPTED;
}

static void rc_enter_not_ready(uint32_t now, RcStopReason reason)
{
    if(rc_ready || rc_not_ready_since_tick == 0U)
    {
        if(rc_ready)
        {
            rc_not_ready_event_count++;
            rc_last_stop_reason = (uint8_t)reason;
        }
        rc_not_ready_since_tick = now;
        rc_zero_command_sent = 0;
        rc_lift_stop_sent = 0;
        rc_failsafe_stop_done = 0;
    }

    rc_ready = 0;
    rc_valid_frame_count = 0;
    rc_prev_channels_valid = 0;
    rc_jump_confirmation_reset();
}

static void rc_accept_trustworthy_frame(const int16_t ch[SBUS_NUM_CHANNELS], uint32_t now)
{
    rc_prev_ch3 = ch[2];
    rc_prev_ch4 = ch[3];
    rc_prev_channels_valid = 1;
    rc_last_valid_frame_tick = now;

    if(rc_valid_frame_count < RC_REQUIRED_VALID_FRAMES)
    {
        rc_valid_frame_count++;
    }

    if(rc_valid_frame_count >= RC_REQUIRED_VALID_FRAMES && !rc_ready)
    {
        rc_ready = 1;
        rc_recovery_count++;
        rc_not_ready_since_tick = 0;
        rc_zero_command_sent = 0;
        rc_lift_stop_sent = 0;
        rc_failsafe_stop_done = 0;
        motor_speed_command_invalidate();
    }
}

static uint8_t motor_write_speed_with_echo(uint8_t slave_addr, int16_t rpm)
{
    uint8_t cmd[8] = {
        slave_addr, 0x06, 0x23, 0x18,
        (uint8_t)(((uint16_t)rpm >> 8) & 0xFFU),
        (uint8_t)((uint16_t)rpm & 0xFFU),
        0U, 0U
    };
    uint8_t response[8] = {0};
    uint16_t crc = Modbus_CRC16(cmd, 6U);

    cmd[6] = (uint8_t)(crc & 0xFFU);
    cmd[7] = (uint8_t)(crc >> 8);
    telemetry_clear_uart_rx(&huart2);

    if(RS485_SendPacketTimeout(cmd, sizeof(cmd), TELEMETRY_TX_TIMEOUT_MS) != HAL_OK)
    {
        return 0U;
    }
    if(RS485_ReceivePacket(response, sizeof(response), MOTOR_WRITE_ECHO_TIMEOUT_MS) != HAL_OK)
    {
        return 0U;
    }
    return (memcmp(response, cmd, sizeof(cmd)) == 0) ? 1U : 0U;
}

static void motor_speed_set_confirmed(int16_t left_cmd, int16_t right_cmd)
{
    left_cmd = (left_cmd > MAX_RPM) ? MAX_RPM :
               (left_cmd < -MAX_RPM) ? -MAX_RPM : left_cmd;
    right_cmd = (right_cmd > MAX_RPM) ? MAX_RPM :
                (right_cmd < -MAX_RPM) ? -MAX_RPM : right_cmd;

    /* Preserve the proven right-first order, but consume each 0x06 echo. */
    right_write_echo_ok = motor_write_speed_with_echo(2U, right_cmd);
    if(right_write_echo_ok)
    {
        right_write_ok_count++;
    }
    else
    {
        right_write_fail_count++;
    }

    /* The second drive requires a quiet Modbus interval after the first echo. */
    HAL_Delay(2U);
    left_write_echo_ok = motor_write_speed_with_echo(1U, left_cmd);
    if(left_write_echo_ok)
    {
        left_write_ok_count++;
    }
    else
    {
        left_write_fail_count++;
    }
    motor_write_sequence++;
}

static void rc_safety_stop_update(uint32_t now)
{
    if(!rc_zero_command_sent ||
       (now - rc_last_zero_command_tick) >= RC_ZERO_REFRESH_MS)
    {
        telemetry_control_command_begin();
        motor_speed_set_confirmed(0, 0);
        telemetry_control_command_end();
        motor_speed_command_invalidate();
        rc_zero_command_sent = 1;
        rc_last_zero_command_tick = HAL_GetTick();
    }

    if(!rc_lift_stop_sent || current_lift_state != LIFT_STOP)
    {
        telemetry_control_command_begin();
        lift_stop();
        telemetry_control_command_end();
        current_lift_state = LIFT_STOP;
        rc_lift_stop_sent = 1;
    }

    if(!rc_failsafe_stop_done &&
       rc_not_ready_since_tick != 0U &&
       (now - rc_not_ready_since_tick) >= RC_FAILSAFE_STOP_TIMEOUT_MS &&
       en_flag)
    {
        telemetry_control_command_begin();
        motor_emergency_stop();
        telemetry_control_command_end();
        motor_speed_command_invalidate();
        rc_failsafe_stop_done = 1;
    }
}

static uint8_t telemetry_query_is_waiting(void)
{
    return (telemetry_query_state == TELEMETRY_WAIT_LEFT ||
            telemetry_query_state == TELEMETRY_WAIT_RIGHT ||
            telemetry_query_state == TELEMETRY_WAIT_LIFT);
}

static void motor_speed_control_update(int16_t left_cmd, int16_t right_cmd)
{
    uint32_t now = HAL_GetTick();
    uint8_t refresh_due = ((now - motor_speed_cmd_last_tick) >= MOTOR_COMMAND_REFRESH_MS);
    uint8_t neutral_zero_burst_due =
        (neutral_stop.active &&
         neutral_stop.terminal_latched &&
         left_cmd == 0 && right_cmd == 0 &&
         (now - neutral_stop.terminal_tick) <= NEUTRAL_ZERO_BURST_DURATION_MS &&
         (neutral_stop.last_zero_send_tick == 0U ||
          (now - neutral_stop.last_zero_send_tick) >= NEUTRAL_ZERO_BURST_PERIOD_MS));
    uint8_t large_change = (abs(left_cmd - motor_last_left_cmd) >= MOTOR_COMMAND_IMMEDIATE_DELTA_RPM ||
                            abs(right_cmd - motor_last_right_cmd) >= MOTOR_COMMAND_IMMEDIATE_DELTA_RPM);
    uint8_t zero_cross = ((left_cmd == 0 && motor_last_left_cmd != 0) ||
                          (right_cmd == 0 && motor_last_right_cmd != 0) ||
                          (left_cmd != 0 && motor_last_left_cmd == 0) ||
                          (right_cmd != 0 && motor_last_right_cmd == 0));
    uint8_t urgent_command = (!motor_speed_cmd_valid || large_change || zero_cross ||
                              neutral_zero_burst_due);

    /*
     * 正常运行时不要让 100 ms 的速度保活刷新打断正在等待的右轮回包。
     * 真实操控变化仍然优先，必要时会中止后台遥测并立即下发速度。
     */
    if(telemetry_query_is_waiting() && !urgent_command)
    {
        return;
    }

    if(urgent_command || refresh_due)
    {
        telemetry_control_command_begin();
        motor_speed_set_confirmed(left_cmd, right_cmd);
        telemetry_control_command_end();

        motor_last_left_cmd = left_cmd;
        motor_last_right_cmd = right_cmd;
        motor_speed_cmd_last_tick = HAL_GetTick();
        motor_speed_cmd_valid = 1;
        if(neutral_zero_burst_due)
        {
            neutral_stop.last_zero_send_tick = motor_speed_cmd_last_tick;
        }
    }
}

static void telemetry_service_query(uint32_t now, uint8_t allow_start)
{
    uint8_t response_ready;
    uint8_t timeout;

    switch(telemetry_query_state)
    {
        case TELEMETRY_QUERY_IDLE:
            if((now - telemetry_last_query_tick) >= TELEMETRY_QUERY_PERIOD_MS &&
               (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                telemetry_last_query_tick = now;
                telemetry_schedule_current_step();
            }
            break;

        case TELEMETRY_SEND_LEFT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_motor_query(1, TELEMETRY_WAIT_LEFT))
                {
                    telemetry_advance_query_step();
                    telemetry_query_state = TELEMETRY_QUERY_IDLE;
                }
            }
            break;

        case TELEMETRY_WAIT_LEFT:
            response_ready = telemetry_poll_uart_response(&huart2);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_MOTOR_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                telemetry_finish_motor_query(response_ready);
                telemetry_advance_query_step();
                telemetry_query_state = TELEMETRY_QUERY_IDLE;
            }
            break;

        case TELEMETRY_SEND_RIGHT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_motor_query(2, TELEMETRY_WAIT_RIGHT))
                {
                    telemetry_advance_query_step();
                    telemetry_query_state = TELEMETRY_QUERY_IDLE;
                }
            }
            break;

        case TELEMETRY_WAIT_RIGHT:
            response_ready = telemetry_poll_uart_response(&huart2);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_MOTOR_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                telemetry_finish_motor_query(response_ready);
                telemetry_advance_query_step();
                telemetry_query_state = TELEMETRY_QUERY_IDLE;
            }
            break;

        case TELEMETRY_SEND_LIFT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_lift_query())
                {
                    telemetry_advance_query_step();
                    telemetry_query_state = TELEMETRY_QUERY_IDLE;
                }
            }
            break;

        case TELEMETRY_WAIT_LIFT:
            response_ready = telemetry_poll_uart_response(&huart3);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_LIFT_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                telemetry_finish_lift_query(response_ready);
                telemetry_advance_query_step();
                telemetry_query_state = TELEMETRY_QUERY_IDLE;
                telemetry_reset_rx_buffer();
            }
            break;

        default:
            telemetry_query_state = TELEMETRY_QUERY_IDLE;
            telemetry_reset_rx_buffer();
            break;
    }
}

static void telemetry_send(void)
{
    static char line[TELEMETRY_LINE_MAX];
    uint32_t now = HAL_GetTick();
    uint32_t rc_age_ms = rc_last_valid_frame_tick == 0U ? 0U : (now - rc_last_valid_frame_tick);
    int len = snprintf(line, sizeof(line),
                       "{\"tick_ms\":%lu,\"left_rpm\":%d,\"right_rpm\":%d,\"left_cmd\":%d,\"right_cmd\":%d,\"cmd_valid\":%u,\"target_linear\":%d,\"target_steer\":%d,\"conditioned_linear\":%d,\"conditioned_steer\":%d,\"caster_state\":\"%s\",\"traj_tick\":%lu,\"traj_speed_x100\":%ld,\"traj_accel_x100\":%ld,\"pc_test_active\":%u,\"pc_test_linear\":%d,\"pc_test_steer\":%d,\"pc_test_remaining_ms\":%lu,\"pc_test_status\":\"%s\",\"sync_trim\":%d,\"sync_error_x100\":%ld,\"rc_ready\":%u,\"rc_age_ms\":%lu,\"rc_frame_lost_count\":%lu,\"rc_stop_count\":%lu,\"rc_recovery_count\":%lu,\"rc_stop_reason\":%u,\"rc_ch3\":%d,\"rc_ch4\":%d,\"rc_ch6\":%d,\"sbus_failsafe\":%u,\"speed_rpm\":%d,\"state\":\"%s\",\"height_mm\":%d,\"motor_write_sequence\":%lu,\"left_write_echo_ok\":%u,\"right_write_echo_ok\":%u,\"left_write_fail_count\":%lu,\"right_write_fail_count\":%lu}\r\n",
                       (unsigned long)now,
                       left,
                       right,
                       motor_last_left_cmd,
                       motor_last_right_cmd,
                       (unsigned int)motor_speed_cmd_valid,
                       motor_target_linear,
                       motor_target_steer,
                       motor_conditioned_linear,
                       motor_conditioned_steer,
                       caster_alignment_state_text(),
                       (unsigned long)motor_trajectory.last_update_tick,
                       (long)(motor_trajectory.linear.speed * 100.0f),
                       (long)(motor_trajectory.linear.acceleration * 100.0f),
                       (unsigned int)pc_test_control.active,
                       pc_test_control.linear,
                       pc_test_control.steer,
                       (unsigned long)pc_test_remaining_ms(HAL_GetTick()),
                       pc_test_status_text(),
                       straight_sync.trim,
                       (long)(straight_sync.filtered_error * 100.0f),
                       (unsigned int)rc_ready,
                       (unsigned long)rc_age_ms,
                       (unsigned long)rc_frame_lost_count,
                       (unsigned long)rc_not_ready_event_count,
                       (unsigned long)rc_recovery_count,
                       (unsigned int)rc_last_stop_reason,
                       rc_lcd_debug.ch3,
                       rc_lcd_debug.ch4,
                       rc_lcd_debug.ch6,
                       (unsigned int)sbus_failsafe,
                       current_speed_rpm,
                       telemetry_state_text(),
                       lift_height_mm,
                       (unsigned long)motor_write_sequence,
                       (unsigned int)left_write_echo_ok,
                       (unsigned int)right_write_echo_ok,
                       (unsigned long)left_write_fail_count,
                       (unsigned long)right_write_fail_count);

    if(len > 0 && len < (int)sizeof(line))
    {
        (void)CDC_Transmit_HS((uint8_t *)line, (uint16_t)len);
    }
}

static void telemetry_process(void)
{
    uint32_t now = HAL_GetTick();
    telemetry_service_query(now, 1);

    if((now - telemetry_last_tick) >= TELEMETRY_PERIOD_MS)
    {
        telemetry_last_tick = now;
        telemetry_send();
    }
}

static void telemetry_process_poll_only(void)
{
    telemetry_service_query(HAL_GetTick(), 0);
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	char buf[64];
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_UART5_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
	SBUS_Init();
	// 开启LCD背光
	HAL_Delay(10);
	LCD_Init();
	rc_lcd_init_screen();
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
	HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_val,1);	// 读取ADC按键键值
	HAL_TIM_Base_Start_IT(&htim2);
	lift_stop();
	current_lift_state = LIFT_STOP;
  //	change_station();  //手动切换站号
	//	motor_start_init();  //手动使能
	//speed_set(10, 10);   // 左电机 10rpm，右电机 10rpm
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {	
		
		telemetry_process_poll_only();

#if MOTOR_TRAJECTORY_BENCH_TEST
		motor_trajectory_bench_test_update();
		telemetry_process();
		continue;
#endif
		
		uint32_t now = HAL_GetTick();
		/*
		 * Do not let the SBUS driver's 30 ms byte watchdog override the outer
		 * 60 ms valid-frame policy while control is healthy.
		 */
		if(!rc_ready || (now - rc_last_valid_frame_tick) > RC_TRUST_TIMEOUT_MS)
		{
			SBUS_TimeoutCheck();
		}
		if(sbus_failsafe)
		{
				rc_enter_not_ready(now, RC_STOP_REASON_FAILSAFE);
		}
		if(sbus_frame_ok)
		{
				__disable_irq();
				uint8_t local_buf[SBUS_FRAME_LEN];
				memcpy(local_buf, (void*)sbus_buf, SBUS_FRAME_LEN);
				uint8_t local_failsafe = sbus_failsafe;
				uint8_t local_frame_lost = sbus_frame_lost;
				sbus_frame_ok = 0;
				__enable_irq();

				int16_t ch[16] = {0};
				SBUS_ParseChannels(local_buf, ch);
				if(local_frame_lost)
				{
					rc_frame_lost_count++;
				}
				RcFrameTrustResult frame_result = rc_frame_trust_result(ch, local_failsafe, local_frame_lost);
				uint8_t frame_accepted = (frame_result == RC_FRAME_ACCEPTED);
				if(frame_accepted)
				{
						rc_accept_trustworthy_frame(ch, now);
				}
				else if(frame_result == RC_FRAME_REJECTED)
				{
						rc_enter_not_ready(now,
						                   local_failsafe ? RC_STOP_REASON_FAILSAFE :
						                                    RC_STOP_REASON_INVALID_FRAME);
				}
				/* 大跳变确认期间保持上一条可信目标，不执行本帧控制。 */
				rc_lcd_capture_frame(ch, frame_accepted, local_failsafe, local_frame_lost, now);
				telemetry_failsafe = !rc_ready;
				
/*
				static uint8_t debug_counter = 0;
				if(debug_counter++ >= 0)
				{
						sprintf(buf, "CH3:%4d | CH4:%4d | CH6:%4d | CH8:%4d\r\n", 
										ch[2], ch[3], ch[5], ch[7]);
						HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
						debug_counter = 0;
				}
*/
				//		}  //这个 } 复原的时候要删掉

			

				if(rc_ready && frame_accepted)
				{
						// ===================== 第一步：急停 / 使能（只设标志！） =====================
						// 最高优先级：急停/失能
						if(ch[5] > 1500)
						{
								if(!emergency_stop)
								{
										telemetry_control_command_begin();
										motor_emergency_stop();
										telemetry_control_command_end();
										motor_speed_command_invalidate();
								}
								if(current_lift_state != LIFT_STOP)
								{
										telemetry_control_command_begin();
										lift_stop();
										telemetry_control_command_end();
										current_lift_state = LIFT_STOP; // 强制同步软件标志与硬件状态
								}
						}
						else
						{
								// 使能
								if(ch[5] < 500 && (emergency_stop || !en_flag) && rc_sticks_neutral(ch))
								{
										telemetry_control_command_begin();
										motor_clear_emergency_stop();
										telemetry_control_command_end();
										motor_speed_command_invalidate();
								}

								// ===================== 第二步：电机驱动 =====================
								if(en_flag && !emergency_stop)
								{
										motor_target_linear = rc_desired_speed_from_ch3(ch[2]);  // 前后油门 CH3
										motor_target_steer = rc_desired_steer_from_ch4(ch[3]);   // 左右转向 CH4
								}
								else
								{
										motor_target_linear = 0;
										motor_target_steer = 0;
								}

								// ===================== 第三步：升降控制 =====================
								LiftState desired_lift_state;  // 用户想要的状态
						//		LiftState allowed_lift_state;  // 系统允许的状态

								// 3.1 先计算用户想要的状态（只看摇杆）
								desired_lift_state = rc_desired_lift_from_ch8(ch[7]);

								// 3.3 只有系统允许的状态发生变化时才发送指令
								if(desired_lift_state != current_lift_state)
								{
										telemetry_control_command_begin();
										switch(desired_lift_state)
										{
												case LIFT_UP:    lift_up();    break;
												case LIFT_DOWN:  lift_down();  break;
												case LIFT_STOP:  lift_stop();  break;
										}
										telemetry_control_command_end();
										current_lift_state = desired_lift_state;
								}
						}
							

				}
				else
				{
						// 信号丢失 → 全部清零
						/* Safety stop is handled outside sbus_frame_ok as well. */
				}
		}
		if(rc_ready && (now - rc_last_valid_frame_tick) > RC_TRUST_TIMEOUT_MS)
		{
			rc_enter_not_ready(now, RC_STOP_REASON_TIMEOUT);
		}
		pc_test_process(HAL_GetTick());
		if(!rc_ready)
		{
				rc_safety_stop_update(HAL_GetTick());
		}
		else if(en_flag && !emergency_stop)
		{
				int16_t commanded_left_rpm;
				int16_t commanded_right_rpm;
				int16_t requested_linear;
				int16_t requested_steer;

				requested_linear = pc_test_control.active ? pc_test_control.linear : motor_target_linear;
				requested_steer = pc_test_control.active ? pc_test_control.steer : motor_target_steer;

				/*
				 * 操作指令直接进入公共 S 曲线。旧 CASTER_ALIGN_CRAWL 会把指令缓存数百毫秒，
				 * 造成摇杆归中后才释放旧运动；正常操控路径禁止再进入该状态机。
				 */
				neutral_stop_update_request(requested_linear, requested_steer, HAL_GetTick());
				caster_alignment.state = CASTER_ALIGN_IDLE;
				caster_alignment.alignment_required = 0U;
				motor_conditioned_linear = requested_linear;
				motor_conditioned_steer = requested_steer;

				/* 固定 20 ms 周期推进公共轨迹，线速度和转向量先平滑再混控。 */
				motor_trajectory_update(motor_conditioned_linear,
				                        motor_conditioned_steer,
				                        &commanded_left_rpm,
				                        &commanded_right_rpm);
				neutral_stop_apply_terminal(&commanded_left_rpm,
				                            &commanded_right_rpm,
				                            HAL_GetTick());
				straight_sync_apply(&commanded_left_rpm,
				                    &commanded_right_rpm,
				                    HAL_GetTick());
				motor_speed_control_update(commanded_left_rpm, -commanded_right_rpm);
		}
/*
		// LCD 显示
		left  = motor_read_speed(1);
		right = motor_read_speed(2);
		LCD_ShowString(40, 80, (uint8_t*)"Left:         RPM", BLUE, WHITE, 24, 0);
		LCD_ShowString(40,130, (uint8_t*)"Right:        RPM", BLUE, WHITE, 24, 0);
		LCD_ShowIntNum(115,130,right,4,RED,WHITE,24);
		LCD_ShowIntNum(115,80,left,4,RED,WHITE,24); 
*/		
		telemetry_failsafe = (!rc_ready || sbus_failsafe);
		rc_lcd_process(HAL_GetTick());
		telemetry_process();
														
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the SYSCFG APB clock
  */
  __HAL_RCC_CRS_CLK_ENABLE();

  /** Configures CRS
  */
  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB2;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000,1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;

  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

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

typedef enum
{
    TELEMETRY_MOTOR_QUERY_NONE = 0,
    TELEMETRY_MOTOR_QUERY_LEGACY_LEFT,
    TELEMETRY_MOTOR_QUERY_LEGACY_RIGHT,
    TELEMETRY_MOTOR_QUERY_HIGH_RES_SPEED,
    TELEMETRY_MOTOR_QUERY_DIAGNOSTIC,
    TELEMETRY_MOTOR_QUERY_POSITION,
    TELEMETRY_MOTOR_QUERY_CONFIG
} TelemetryMotorQueryKind;

typedef enum
{
    MOTOR_CONFIG_FW_YEAR = 0,
    MOTOR_CONFIG_FW_DATE,
    MOTOR_CONFIG_SPEED_KP,
    MOTOR_CONFIG_SPEED_KI,
    MOTOR_CONFIG_ZERO_HOLD_DELAY,
    MOTOR_CONFIG_INERTIA,
    MOTOR_CONFIG_PID_ALGORITHM,
    MOTOR_CONFIG_ACCEL_TIME,
    MOTOR_CONFIG_DECEL_TIME,
    MOTOR_CONFIG_ZERO_SPEED_THRESHOLD,
    MOTOR_CONFIG_ZERO_SPEED_FILTER,
    MOTOR_CONFIG_TORQUE_LIMIT_ENABLE,
    MOTOR_CONFIG_FORWARD_TORQUE_LIMIT,
    MOTOR_CONFIG_REVERSE_TORQUE_LIMIT,
    MOTOR_CONFIG_MAX_CURRENT,
    MOTOR_CONFIG_COUNT
} MotorConfigIndex;

typedef struct
{
    int16_t left_speed_x10;
    int16_t right_speed_x10;
    uint32_t left_speed_tick;
    uint32_t right_speed_tick;
    uint32_t speed_pair_tick;
    uint32_t speed_pair_sequence;
    uint8_t speed_pair_valid;

    int16_t left_torque_permille;
    int16_t right_torque_permille;
    int16_t left_drive_target_rpm;
    int16_t right_drive_target_rpm;
    uint16_t left_mode;
    uint16_t right_mode;
    uint16_t left_bus_voltage;
    uint16_t right_bus_voltage;
    uint32_t left_diagnostic_tick;
    uint32_t right_diagnostic_tick;
    uint32_t diagnostic_pair_tick;
    uint32_t diagnostic_pair_sequence;
    uint8_t diagnostic_pair_valid;

    int32_t left_position_counts;
    int32_t right_position_counts;
    uint32_t left_position_tick;
    uint32_t right_position_tick;
    uint32_t position_pair_tick;
    uint32_t position_pair_sequence;
    uint8_t position_pair_valid;
} MotorDiagnosticData;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TELEMETRY_PERIOD_MS 50U
#define TELEMETRY_LINE_MAX  4096U
#define TELEMETRY_RX_BUFFER_MAX 64U
#define TELEMETRY_PAYLOAD_MAX 32U
#define TELEMETRY_LEGACY_QUERY_PERIOD_MS 60U
#define TELEMETRY_HIGH_RES_PAIR_PERIOD_MS 40U
#define TELEMETRY_DIAGNOSTIC_PAIR_PERIOD_MS 100U
#define TELEMETRY_POSITION_PAIR_PERIOD_MS 100U
#define TELEMETRY_CONFIG_PAIR_PERIOD_MS 250U
#define TELEMETRY_LIFT_QUERY_PERIOD_MS 500U
#define TELEMETRY_MOTOR_TIMEOUT_MS 50U
#define TELEMETRY_LIFT_TIMEOUT_MS  15U
#define TELEMETRY_TX_TIMEOUT_MS    5U
#define TELEMETRY_CONTROL_HOLDOFF_MS 5U
#define MOTOR_HIGH_RES_SPEED_REG   0x500EU
#define MOTOR_DIAGNOSTIC_START_REG 0x5002U
#define MOTOR_DIAGNOSTIC_REG_COUNT 9U
#define MOTOR_POSITION_START_REG   0x5015U
#define MOTOR_POSITION_REG_COUNT   2U
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
#define MOTOR_LINEAR_MAX_DECEL_RPM_PER_S 80.0f
#define MOTOR_LINEAR_MAX_JERK_RPM_PER_S2 800.0f
#define MOTOR_LINEAR_MAX_STOP_JERK_RPM_PER_S2 1200.0f
#define MOTOR_STEER_MAX_ACCEL_CMD_PER_S 160.0f
#define MOTOR_STEER_MAX_DECEL_CMD_PER_S 200.0f
#define MOTOR_STEER_MAX_JERK_CMD_PER_S2 1600.0f
#define MOTOR_STEER_MAX_STOP_JERK_CMD_PER_S2 2400.0f
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
#define STRAIGHT_SYNC_MAX_TRIM_RPM           1
#define RC_REQUIRED_VALID_FRAMES   3U
#define RC_TRUST_TIMEOUT_MS        30U
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
static uint32_t telemetry_control_last_tick=0;
static uint32_t telemetry_query_start_tick=0;
static uint32_t telemetry_legacy_query_tick=0;
static uint32_t telemetry_high_res_query_tick=0;
static uint32_t telemetry_diagnostic_query_tick=0;
static uint32_t telemetry_position_query_tick=0;
static uint32_t telemetry_config_query_tick=0;
static uint32_t telemetry_lift_query_tick=0;
static uint8_t telemetry_rx_buf[TELEMETRY_RX_BUFFER_MAX]={0};
static uint8_t telemetry_rx_len=0;
static uint8_t telemetry_expected_response_len=0;
static uint8_t telemetry_query_slave=0;
static uint8_t telemetry_legacy_query_step=0;
static uint16_t telemetry_query_reg=0U;
static uint16_t telemetry_query_count=0U;
static TelemetryMotorQueryKind telemetry_motor_query_kind=TELEMETRY_MOTOR_QUERY_NONE;
static uint8_t telemetry_pair_left_payload[TELEMETRY_PAYLOAD_MAX]={0};
static uint8_t telemetry_pair_left_length=0U;
static uint8_t telemetry_pair_left_valid=0U;
static uint32_t telemetry_pair_left_tick=0U;
static uint8_t telemetry_config_index=0U;
static uint8_t telemetry_config_scan_done=0U;
static uint32_t telemetry_config_left_support_mask=0U;
static uint32_t telemetry_config_right_support_mask=0U;
static uint32_t telemetry_config_mismatch_mask=0U;
static uint16_t telemetry_config_left_values[MOTOR_CONFIG_COUNT]={0};
static uint16_t telemetry_config_right_values[MOTOR_CONFIG_COUNT]={0};
static MotorDiagnosticData motor_diagnostic={0};
static const uint16_t motor_config_registers[MOTOR_CONFIG_COUNT]={
    0x5018U,
    0x5019U,
    0x2300U,
    0x2301U,
    0x2309U,
    0x2310U,
    0x2313U,
    0x2320U,
    0x2321U,
    0x2325U,
    0x2329U,
    0x2422U,
    0x2424U,
    0x2425U,
    0x242AU
};
static uint8_t motor_speed_cmd_valid=0;
static int16_t motor_last_left_cmd=0;
static int16_t motor_last_right_cmd=0;
static uint32_t motor_speed_cmd_last_tick=0;
static int16_t motor_target_linear=0;
static int16_t motor_target_steer=0;
static uint32_t left_feedback_tick=0U;
static uint32_t right_feedback_tick=0U;
static uint32_t wheel_feedback_sequence=0U;

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

static PcTestControl pc_test_control={0};
static StraightSyncController straight_sync={0};
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

static uint8_t telemetry_parse_read_response(const uint8_t *buf,
                                             uint8_t response_len,
                                             uint8_t slave_addr,
                                             uint16_t register_count,
                                             uint8_t *payload,
                                             uint8_t *payload_len)
{
    uint8_t data_len=(uint8_t)(register_count * 2U);
    uint8_t expected_len=(uint8_t)(data_len + 5U);
    uint16_t calc_crc;
    uint16_t recv_crc;

    if(buf == NULL || payload == NULL || payload_len == NULL ||
       data_len > TELEMETRY_PAYLOAD_MAX || response_len != expected_len ||
       buf[0] != slave_addr || buf[1] != 0x03U || buf[2] != data_len)
    {
        return 0U;
    }

    calc_crc = Modbus_CRC16((uint8_t *)buf, (uint16_t)(response_len - 2U));
    recv_crc = ((uint16_t)buf[response_len - 1U] << 8) |
               buf[response_len - 2U];
    if(calc_crc != recv_crc)
    {
        return 0U;
    }

    memcpy(payload, &buf[3], data_len);
    *payload_len = data_len;
    return 1U;
}

static int16_t telemetry_payload_i16(const uint8_t *payload, uint8_t register_index)
{
    uint8_t offset=(uint8_t)(register_index * 2U);
    return (int16_t)(((uint16_t)payload[offset] << 8) | payload[offset + 1U]);
}

static uint16_t telemetry_payload_u16(const uint8_t *payload, uint8_t register_index)
{
    uint8_t offset=(uint8_t)(register_index * 2U);
    return ((uint16_t)payload[offset] << 8) | payload[offset + 1U];
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
    telemetry_expected_response_len = 0U;
}

static uint8_t telemetry_poll_uart_response(UART_HandleTypeDef *huart)
{
    uint8_t byte;

    while(telemetry_rx_len < TELEMETRY_RX_BUFFER_MAX &&
          (telemetry_expected_response_len == 0U ||
           telemetry_rx_len < telemetry_expected_response_len))
    {
        HAL_StatusTypeDef sta = HAL_UART_Receive(huart, &byte, 1, 0);
        if(sta == HAL_OK)
        {
            telemetry_rx_buf[telemetry_rx_len++] = byte;

            if(telemetry_rx_len == 3U)
            {
                if((telemetry_rx_buf[1] & 0x80U) != 0U)
                {
                    telemetry_expected_response_len = 5U;
                }
                else if(telemetry_rx_buf[1] == 0x03U)
                {
                    uint16_t calculated_len=(uint16_t)telemetry_rx_buf[2] + 5U;
                    telemetry_expected_response_len =
                        (calculated_len <= TELEMETRY_RX_BUFFER_MAX) ?
                        (uint8_t)calculated_len : 0U;
                }
            }
            continue;
        }

        if(sta == HAL_ERROR)
        {
            __HAL_UART_CLEAR_OREFLAG(huart);
            huart->ErrorCode = HAL_UART_ERROR_NONE;
        }
        break;
    }

    return (telemetry_expected_response_len > 0U &&
            telemetry_rx_len >= telemetry_expected_response_len);
}

static void telemetry_build_read_cmd(uint8_t slave_addr,
                                     uint16_t reg,
                                     uint16_t register_count,
                                     uint8_t *cmd)
{
    cmd[0] = slave_addr;
    cmd[1] = 0x03;
    cmd[2] = (uint8_t)(reg >> 8);
    cmd[3] = (uint8_t)(reg & 0xFF);
    cmd[4] = (uint8_t)(register_count >> 8);
    cmd[5] = (uint8_t)(register_count & 0xFFU);
    uint16_t crc = Modbus_CRC16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = crc >> 8;
}

static uint8_t telemetry_start_motor_query(uint8_t slave_addr, TelemetryQueryState next_state)
{
    uint8_t cmd[8];

    telemetry_reset_rx_buffer();
    telemetry_clear_uart_rx(&huart2);
    telemetry_build_read_cmd(slave_addr,
                             telemetry_query_reg,
                             telemetry_query_count,
                             cmd);

    if(RS485_SendPacketTimeout(cmd, 8, TELEMETRY_TX_TIMEOUT_MS) != HAL_OK)
    {
        telemetry_query_state = TELEMETRY_QUERY_IDLE;
        return 0;
    }

    telemetry_query_slave = slave_addr;
    telemetry_query_start_tick = HAL_GetTick();
    telemetry_expected_response_len = (uint8_t)(5U + telemetry_query_count * 2U);
    telemetry_query_state = next_state;
    return 1;
}

static uint8_t telemetry_start_lift_query(void)
{
    uint8_t cmd[8];

    telemetry_reset_rx_buffer();
    telemetry_clear_uart_rx(&huart3);
    telemetry_build_read_cmd(0x01, 0x0002, 1U, cmd);

    if(RS485_SendPacket2Timeout(cmd, 8, TELEMETRY_TX_TIMEOUT_MS) != HAL_OK)
    {
        telemetry_query_state = TELEMETRY_QUERY_IDLE;
        return 0;
    }

    telemetry_query_slave = 0x01;
    telemetry_query_start_tick = HAL_GetTick();
    telemetry_expected_response_len = 7U;
    telemetry_query_state = TELEMETRY_WAIT_LIFT;
    return 1;
}

static void telemetry_update_cached_speed(void)
{
    current_speed_rpm = (int16_t)((left - right) / 2);
}

static void telemetry_commit_legacy_speed(uint8_t slave_addr,
                                          const uint8_t *payload,
                                          uint8_t payload_len,
                                          uint32_t sample_tick)
{
    int16_t value;

    if(payload == NULL || payload_len != 2U)
    {
        return;
    }

    value = telemetry_payload_i16(payload, 0U);
    if(slave_addr == 1U)
    {
        left = value;
        left_feedback_tick = sample_tick;
    }
    else if(slave_addr == 2U)
    {
        right = value;
        right_feedback_tick = sample_tick;
        wheel_feedback_sequence++;
    }
    telemetry_update_cached_speed();
}

static int32_t telemetry_position_from_payload(const uint8_t *payload)
{
    uint32_t low=telemetry_payload_u16(payload, 0U);
    uint32_t high=telemetry_payload_u16(payload, 1U);
    return (int32_t)((high << 16) | low);
}

static void telemetry_commit_motor_pair(const uint8_t *right_payload,
                                        uint8_t right_payload_len,
                                        uint8_t right_valid,
                                        uint32_t right_tick)
{
    uint8_t both_valid=(telemetry_pair_left_valid && right_valid);

    if(telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_CONFIG)
    {
        uint32_t bit=(uint32_t)1U << telemetry_config_index;

        if(telemetry_pair_left_valid && telemetry_pair_left_length == 2U)
        {
            telemetry_config_left_values[telemetry_config_index] =
                telemetry_payload_u16(telemetry_pair_left_payload, 0U);
            telemetry_config_left_support_mask |= bit;
        }
        if(right_valid && right_payload_len == 2U)
        {
            telemetry_config_right_values[telemetry_config_index] =
                telemetry_payload_u16(right_payload, 0U);
            telemetry_config_right_support_mask |= bit;
        }
        if(both_valid && telemetry_pair_left_length == 2U && right_payload_len == 2U &&
           telemetry_config_left_values[telemetry_config_index] !=
           telemetry_config_right_values[telemetry_config_index])
        {
            telemetry_config_mismatch_mask |= bit;
        }

        telemetry_config_index++;
        if(telemetry_config_index >= MOTOR_CONFIG_COUNT)
        {
            telemetry_config_scan_done = 1U;
        }
        return;
    }

    if(!both_valid)
    {
        return;
    }

    if(telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_HIGH_RES_SPEED &&
       telemetry_pair_left_length == 2U && right_payload_len == 2U)
    {
        motor_diagnostic.left_speed_x10 =
            telemetry_payload_i16(telemetry_pair_left_payload, 0U);
        motor_diagnostic.right_speed_x10 =
            (int16_t)(-telemetry_payload_i16(right_payload, 0U));
        motor_diagnostic.left_speed_tick = telemetry_pair_left_tick;
        motor_diagnostic.right_speed_tick = right_tick;
        motor_diagnostic.speed_pair_tick = right_tick;
        motor_diagnostic.speed_pair_sequence++;
        motor_diagnostic.speed_pair_valid = 1U;
    }
    else if(telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_DIAGNOSTIC &&
            telemetry_pair_left_length == (MOTOR_DIAGNOSTIC_REG_COUNT * 2U) &&
            right_payload_len == (MOTOR_DIAGNOSTIC_REG_COUNT * 2U))
    {
        motor_diagnostic.left_torque_permille =
            telemetry_payload_i16(telemetry_pair_left_payload, 0U);
        motor_diagnostic.right_torque_permille =
            (int16_t)(-telemetry_payload_i16(right_payload, 0U));
        motor_diagnostic.left_drive_target_rpm =
            telemetry_payload_i16(telemetry_pair_left_payload, 5U);
        motor_diagnostic.right_drive_target_rpm =
            (int16_t)(-telemetry_payload_i16(right_payload, 5U));
        motor_diagnostic.left_mode =
            telemetry_payload_u16(telemetry_pair_left_payload, 7U);
        motor_diagnostic.right_mode =
            telemetry_payload_u16(right_payload, 7U);
        motor_diagnostic.left_bus_voltage =
            telemetry_payload_u16(telemetry_pair_left_payload, 8U);
        motor_diagnostic.right_bus_voltage =
            telemetry_payload_u16(right_payload, 8U);
        motor_diagnostic.left_diagnostic_tick = telemetry_pair_left_tick;
        motor_diagnostic.right_diagnostic_tick = right_tick;
        motor_diagnostic.diagnostic_pair_tick = right_tick;
        motor_diagnostic.diagnostic_pair_sequence++;
        motor_diagnostic.diagnostic_pair_valid = 1U;
    }
    else if(telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_POSITION &&
            telemetry_pair_left_length == (MOTOR_POSITION_REG_COUNT * 2U) &&
            right_payload_len == (MOTOR_POSITION_REG_COUNT * 2U))
    {
        motor_diagnostic.left_position_counts =
            telemetry_position_from_payload(telemetry_pair_left_payload);
        motor_diagnostic.right_position_counts =
            -telemetry_position_from_payload(right_payload);
        motor_diagnostic.left_position_tick = telemetry_pair_left_tick;
        motor_diagnostic.right_position_tick = right_tick;
        motor_diagnostic.position_pair_tick = right_tick;
        motor_diagnostic.position_pair_sequence++;
        motor_diagnostic.position_pair_valid = 1U;
    }
}

static void telemetry_finish_lift_query(uint8_t response_ready)
{
    uint8_t payload[2];
    uint8_t payload_len=0U;

    if(response_ready &&
       telemetry_parse_read_response(telemetry_rx_buf,
                                     telemetry_expected_response_len,
                                     0x01U,
                                     1U,
                                     payload,
                                     &payload_len))
    {
        lift_height_mm = telemetry_payload_i16(payload, 0U);
    }
}

static uint8_t telemetry_motor_query_is_pair(void)
{
    return (telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_HIGH_RES_SPEED ||
            telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_DIAGNOSTIC ||
            telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_POSITION ||
            telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_CONFIG);
}

static void telemetry_abort_pending_query(void)
{
    if(telemetry_query_state != TELEMETRY_QUERY_IDLE)
    {
        telemetry_query_state = TELEMETRY_QUERY_IDLE;
        telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
        telemetry_pair_left_valid = 0U;
        telemetry_reset_rx_buffer();
        telemetry_clear_uart_rx(&huart2);
        telemetry_clear_uart_rx(&huart3);
    }
}

static void telemetry_prepare_motor_query(TelemetryMotorQueryKind kind,
                                          uint16_t reg,
                                          uint16_t count)
{
    telemetry_motor_query_kind = kind;
    telemetry_query_reg = reg;
    telemetry_query_count = count;
    telemetry_pair_left_valid = 0U;
    telemetry_pair_left_length = 0U;

    if(kind == TELEMETRY_MOTOR_QUERY_LEGACY_RIGHT)
    {
        telemetry_query_state = TELEMETRY_SEND_RIGHT;
    }
    else
    {
        telemetry_query_state = TELEMETRY_SEND_LEFT;
    }
}

static void telemetry_schedule_next_query(uint32_t now)
{
    if((now - telemetry_legacy_query_tick) >= TELEMETRY_LEGACY_QUERY_PERIOD_MS)
    {
        telemetry_legacy_query_tick = now;
        if(telemetry_legacy_query_step == 0U)
        {
            telemetry_prepare_motor_query(TELEMETRY_MOTOR_QUERY_LEGACY_LEFT,
                                          0x5000U,
                                          1U);
            telemetry_legacy_query_step = 1U;
            return;
        }
        if(telemetry_legacy_query_step == 1U)
        {
            telemetry_prepare_motor_query(TELEMETRY_MOTOR_QUERY_LEGACY_RIGHT,
                                          0x5000U,
                                          1U);
            telemetry_legacy_query_step = 2U;
            return;
        }
        telemetry_legacy_query_step = 0U;
    }

    if((now - telemetry_high_res_query_tick) >= TELEMETRY_HIGH_RES_PAIR_PERIOD_MS)
    {
        telemetry_high_res_query_tick = now;
        telemetry_prepare_motor_query(TELEMETRY_MOTOR_QUERY_HIGH_RES_SPEED,
                                      MOTOR_HIGH_RES_SPEED_REG,
                                      1U);
        return;
    }

    if((now - telemetry_diagnostic_query_tick) >= TELEMETRY_DIAGNOSTIC_PAIR_PERIOD_MS)
    {
        telemetry_diagnostic_query_tick = now;
        telemetry_prepare_motor_query(TELEMETRY_MOTOR_QUERY_DIAGNOSTIC,
                                      MOTOR_DIAGNOSTIC_START_REG,
                                      MOTOR_DIAGNOSTIC_REG_COUNT);
        return;
    }

    if((now - telemetry_position_query_tick) >= TELEMETRY_POSITION_PAIR_PERIOD_MS)
    {
        telemetry_position_query_tick = now;
        telemetry_prepare_motor_query(TELEMETRY_MOTOR_QUERY_POSITION,
                                      MOTOR_POSITION_START_REG,
                                      MOTOR_POSITION_REG_COUNT);
        return;
    }

    if(!telemetry_config_scan_done &&
       (now - telemetry_config_query_tick) >= TELEMETRY_CONFIG_PAIR_PERIOD_MS)
    {
        telemetry_config_query_tick = now;
        telemetry_prepare_motor_query(TELEMETRY_MOTOR_QUERY_CONFIG,
                                      motor_config_registers[telemetry_config_index],
                                      1U);
        return;
    }

    if((now - telemetry_lift_query_tick) >= TELEMETRY_LIFT_QUERY_PERIOD_MS)
    {
        telemetry_lift_query_tick = now;
        telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
        telemetry_query_state = TELEMETRY_SEND_LIFT;
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
        uint8_t feedback_direction_valid = ((*left_cmd > 0 &&
                                             physical_left_rpm >= 1 &&
                                             physical_right_rpm >= 1) ||
                                            (*left_cmd < 0 &&
                                             physical_left_rpm <= -1 &&
                                             physical_right_rpm <= -1));

        straight_sync.last_feedback_sequence = wheel_feedback_sequence;
        if(feedback_direction_valid)
        {
            float error = (float)(physical_left_rpm - physical_right_rpm);
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

    *left_cmd -= straight_sync.trim;
    *right_cmd += straight_sync.trim;
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
    pc_test_cancel(PC_TEST_CANCELLED);
    straight_sync_reset();
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
    if(failsafe || frame_lost)
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

static void rc_enter_not_ready(uint32_t now)
{
    if(rc_ready || rc_not_ready_since_tick == 0U)
    {
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
        rc_not_ready_since_tick = 0;
        rc_zero_command_sent = 0;
        rc_lift_stop_sent = 0;
        rc_failsafe_stop_done = 0;
        motor_speed_command_invalidate();
    }
}

static void rc_safety_stop_update(uint32_t now)
{
    if(!rc_zero_command_sent ||
       (now - rc_last_zero_command_tick) >= RC_ZERO_REFRESH_MS)
    {
        telemetry_control_command_begin();
        speed_set(0, 0);
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
    uint8_t large_change = (abs(left_cmd - motor_last_left_cmd) >= MOTOR_COMMAND_IMMEDIATE_DELTA_RPM ||
                            abs(right_cmd - motor_last_right_cmd) >= MOTOR_COMMAND_IMMEDIATE_DELTA_RPM);
    uint8_t zero_cross = ((left_cmd == 0 && motor_last_left_cmd != 0) ||
                          (right_cmd == 0 && motor_last_right_cmd != 0) ||
                          (left_cmd != 0 && motor_last_left_cmd == 0) ||
                          (right_cmd != 0 && motor_last_right_cmd == 0));
    uint8_t urgent_command = (!motor_speed_cmd_valid || large_change || zero_cross);

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
        speed_set(left_cmd, right_cmd);
        telemetry_control_command_end();

        motor_last_left_cmd = left_cmd;
        motor_last_right_cmd = right_cmd;
        motor_speed_cmd_last_tick = HAL_GetTick();
        motor_speed_cmd_valid = 1;
    }
}

static void telemetry_service_query(uint32_t now, uint8_t allow_start)
{
    uint8_t response_ready;
    uint8_t timeout;
    uint8_t payload[TELEMETRY_PAYLOAD_MAX];
    uint8_t payload_len;
    uint8_t valid;

    switch(telemetry_query_state)
    {
        case TELEMETRY_QUERY_IDLE:
            if(allow_start &&
               (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                telemetry_schedule_next_query(now);
            }
            break;

        case TELEMETRY_SEND_LEFT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_motor_query(1, TELEMETRY_WAIT_LEFT))
                {
                    telemetry_pair_left_valid = 0U;
                    telemetry_pair_left_length = 0U;
                    if(telemetry_motor_query_is_pair())
                    {
                        telemetry_query_state = TELEMETRY_SEND_RIGHT;
                    }
                    else
                    {
                        telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
                        telemetry_query_state = TELEMETRY_QUERY_IDLE;
                    }
                }
            }
            break;

        case TELEMETRY_WAIT_LEFT:
            response_ready = telemetry_poll_uart_response(&huart2);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_MOTOR_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                payload_len = 0U;
                valid = (response_ready &&
                         telemetry_parse_read_response(telemetry_rx_buf,
                                                       telemetry_rx_len,
                                                       1U,
                                                       telemetry_query_count,
                                                       payload,
                                                       &payload_len));
                if(telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_LEGACY_LEFT)
                {
                    if(valid)
                    {
                        telemetry_commit_legacy_speed(1U, payload, payload_len, now);
                    }
                    telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
                    telemetry_query_state = TELEMETRY_QUERY_IDLE;
                }
                else
                {
                    telemetry_pair_left_valid = valid;
                    telemetry_pair_left_length = valid ? payload_len : 0U;
                    telemetry_pair_left_tick = now;
                    if(valid)
                    {
                        memcpy(telemetry_pair_left_payload, payload, payload_len);
                    }
                    telemetry_query_state = TELEMETRY_SEND_RIGHT;
                }
                telemetry_reset_rx_buffer();
            }
            break;

        case TELEMETRY_SEND_RIGHT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_motor_query(2, TELEMETRY_WAIT_RIGHT))
                {
                    if(telemetry_motor_query_is_pair())
                    {
                        telemetry_commit_motor_pair(NULL, 0U, 0U, now);
                    }
                    telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
                    telemetry_query_state = TELEMETRY_QUERY_IDLE;
                }
            }
            break;

        case TELEMETRY_WAIT_RIGHT:
            response_ready = telemetry_poll_uart_response(&huart2);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_MOTOR_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                payload_len = 0U;
                valid = (response_ready &&
                         telemetry_parse_read_response(telemetry_rx_buf,
                                                       telemetry_rx_len,
                                                       2U,
                                                       telemetry_query_count,
                                                       payload,
                                                       &payload_len));
                if(telemetry_motor_query_kind == TELEMETRY_MOTOR_QUERY_LEGACY_RIGHT)
                {
                    if(valid)
                    {
                        telemetry_commit_legacy_speed(2U, payload, payload_len, now);
                    }
                }
                else if(telemetry_motor_query_is_pair())
                {
                    telemetry_commit_motor_pair(payload, payload_len, valid, now);
                }
                telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
                telemetry_query_state = TELEMETRY_QUERY_IDLE;
                telemetry_reset_rx_buffer();
            }
            break;

        case TELEMETRY_SEND_LIFT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_lift_query())
                {
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
                telemetry_query_state = TELEMETRY_QUERY_IDLE;
                telemetry_reset_rx_buffer();
            }
            break;

        default:
            telemetry_query_state = TELEMETRY_QUERY_IDLE;
            telemetry_motor_query_kind = TELEMETRY_MOTOR_QUERY_NONE;
            telemetry_reset_rx_buffer();
            break;
    }
}

static void telemetry_send(void)
{
    static char line[TELEMETRY_LINE_MAX];
    uint32_t now=HAL_GetTick();
    uint32_t left_speed_age=(motor_diagnostic.left_speed_tick != 0U) ?
                            (now - motor_diagnostic.left_speed_tick) : 0xFFFFFFFFUL;
    uint32_t right_speed_age=(motor_diagnostic.right_speed_tick != 0U) ?
                             (now - motor_diagnostic.right_speed_tick) : 0xFFFFFFFFUL;
    uint32_t left_diagnostic_age=(motor_diagnostic.left_diagnostic_tick != 0U) ?
                                 (now - motor_diagnostic.left_diagnostic_tick) : 0xFFFFFFFFUL;
    uint32_t right_diagnostic_age=(motor_diagnostic.right_diagnostic_tick != 0U) ?
                                  (now - motor_diagnostic.right_diagnostic_tick) : 0xFFFFFFFFUL;
    uint32_t left_position_age=(motor_diagnostic.left_position_tick != 0U) ?
                               (now - motor_diagnostic.left_position_tick) : 0xFFFFFFFFUL;
    uint32_t right_position_age=(motor_diagnostic.right_position_tick != 0U) ?
                                (now - motor_diagnostic.right_position_tick) : 0xFFFFFFFFUL;
    uint32_t speed_pair_skew=(motor_diagnostic.left_speed_tick >= motor_diagnostic.right_speed_tick) ?
                             (motor_diagnostic.left_speed_tick - motor_diagnostic.right_speed_tick) :
                             (motor_diagnostic.right_speed_tick - motor_diagnostic.left_speed_tick);
    uint32_t diagnostic_pair_skew=
        (motor_diagnostic.left_diagnostic_tick >= motor_diagnostic.right_diagnostic_tick) ?
        (motor_diagnostic.left_diagnostic_tick - motor_diagnostic.right_diagnostic_tick) :
        (motor_diagnostic.right_diagnostic_tick - motor_diagnostic.left_diagnostic_tick);
    uint32_t position_pair_skew=
        (motor_diagnostic.left_position_tick >= motor_diagnostic.right_position_tick) ?
        (motor_diagnostic.left_position_tick - motor_diagnostic.right_position_tick) :
        (motor_diagnostic.right_position_tick - motor_diagnostic.left_position_tick);
    int len = snprintf(line, sizeof(line),
                       "{\"tick_ms\":%lu,\"left_rpm\":%d,\"right_rpm\":%d,\"left_cmd\":%d,\"right_cmd\":%d,\"cmd_valid\":%u,"
                       "\"target_linear\":%d,\"target_steer\":%d,\"conditioned_linear\":%d,\"conditioned_steer\":%d,"
                       "\"caster_state\":\"%s\",\"traj_tick\":%lu,\"traj_speed_x100\":%ld,\"traj_accel_x100\":%ld,"
                       "\"pc_test_active\":%u,\"pc_test_linear\":%d,\"pc_test_steer\":%d,\"pc_test_remaining_ms\":%lu,\"pc_test_status\":\"%s\","
                       "\"sync_trim\":%d,\"sync_error_x100\":%ld,\"rc_ready\":%u,\"rc_ch3\":%d,\"rc_ch4\":%d,\"rc_ch6\":%d,"
                       "\"sbus_failsafe\":%u,\"speed_rpm\":%d,\"state\":\"%s\",\"height_mm\":%d,"
                       "\"left_feedback_rpm_x10\":%d,\"right_feedback_rpm_x10\":%d,\"speed_pair_valid\":%u,\"speed_pair_sequence\":%lu,"
                       "\"left_speed_tick_ms\":%lu,\"right_speed_tick_ms\":%lu,\"speed_pair_skew_ms\":%lu,\"left_speed_age_ms\":%lu,\"right_speed_age_ms\":%lu,"
                       "\"left_cmd_physical\":%d,\"right_cmd_physical\":%d,"
                       "\"left_drive_target_rpm\":%d,\"right_drive_target_rpm\":%d,\"left_torque_permille\":%d,\"right_torque_permille\":%d,"
                       "\"left_mode\":%u,\"right_mode\":%u,\"left_bus_voltage_v\":%u,\"right_bus_voltage_v\":%u,"
                       "\"diagnostic_pair_valid\":%u,\"diagnostic_pair_sequence\":%lu,\"left_diagnostic_tick_ms\":%lu,\"right_diagnostic_tick_ms\":%lu,"
                       "\"diagnostic_pair_skew_ms\":%lu,\"left_diagnostic_age_ms\":%lu,\"right_diagnostic_age_ms\":%lu,"
                       "\"left_position_counts\":%ld,\"right_position_counts\":%ld,\"position_pair_valid\":%u,\"position_pair_sequence\":%lu,"
                       "\"left_position_tick_ms\":%lu,\"right_position_tick_ms\":%lu,\"position_pair_skew_ms\":%lu,\"left_position_age_ms\":%lu,\"right_position_age_ms\":%lu,"
                       "\"motor_write_sequence\":%lu,\"left_write_echo_ok\":%u,\"right_write_echo_ok\":%u,"
                       "\"left_write_ok_count\":%lu,\"right_write_ok_count\":%lu,\"left_write_fail_count\":%lu,\"right_write_fail_count\":%lu,"
                       "\"left_write_value\":%d,\"right_write_value\":%d,\"left_write_tick_ms\":%lu,\"right_write_tick_ms\":%lu,"
                       "\"left_write_sequence\":%lu,\"right_write_sequence\":%lu,"
                       "\"config_scan_done\":%u,\"config_left_support_mask\":%lu,\"config_right_support_mask\":%lu,\"config_mismatch_mask\":%lu,"
                       "\"left_fw_year\":%u,\"right_fw_year\":%u,\"left_fw_date\":%u,\"right_fw_date\":%u,"
                       "\"left_speed_kp\":%u,\"right_speed_kp\":%u,\"left_speed_ki\":%u,\"right_speed_ki\":%u,"
                       "\"left_zero_hold_delay_ms\":%u,\"right_zero_hold_delay_ms\":%u,\"left_accel_time_ms\":%u,\"right_accel_time_ms\":%u,"
                       "\"left_decel_time_ms\":%u,\"right_decel_time_ms\":%u,\"left_torque_limit\":%u,\"right_torque_limit\":%u,"
                       "\"left_inertia_coefficient\":%u,\"right_inertia_coefficient\":%u,\"left_pid_algorithm\":%u,\"right_pid_algorithm\":%u,"
                       "\"left_zero_speed_threshold\":%u,\"right_zero_speed_threshold\":%u,\"left_zero_speed_filter\":%u,\"right_zero_speed_filter\":%u,"
                       "\"left_torque_limit_enable\":%u,\"right_torque_limit_enable\":%u,\"left_reverse_torque_limit\":%u,\"right_reverse_torque_limit\":%u,"
                       "\"left_max_current_10ma\":%u,\"right_max_current_10ma\":%u}\r\n",
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
                       rc_lcd_debug.ch3,
                       rc_lcd_debug.ch4,
                       rc_lcd_debug.ch6,
                       (unsigned int)sbus_failsafe,
                       current_speed_rpm,
                       telemetry_state_text(),
                       lift_height_mm,
                       motor_diagnostic.left_speed_x10,
                       motor_diagnostic.right_speed_x10,
                       (unsigned int)motor_diagnostic.speed_pair_valid,
                       (unsigned long)motor_diagnostic.speed_pair_sequence,
                       (unsigned long)motor_diagnostic.left_speed_tick,
                       (unsigned long)motor_diagnostic.right_speed_tick,
                       (unsigned long)speed_pair_skew,
                       (unsigned long)left_speed_age,
                       (unsigned long)right_speed_age,
                       motor_last_left_cmd,
                       -motor_last_right_cmd,
                       motor_diagnostic.left_drive_target_rpm,
                       motor_diagnostic.right_drive_target_rpm,
                       motor_diagnostic.left_torque_permille,
                       motor_diagnostic.right_torque_permille,
                       (unsigned int)motor_diagnostic.left_mode,
                       (unsigned int)motor_diagnostic.right_mode,
                       (unsigned int)motor_diagnostic.left_bus_voltage,
                       (unsigned int)motor_diagnostic.right_bus_voltage,
                       (unsigned int)motor_diagnostic.diagnostic_pair_valid,
                       (unsigned long)motor_diagnostic.diagnostic_pair_sequence,
                       (unsigned long)motor_diagnostic.left_diagnostic_tick,
                       (unsigned long)motor_diagnostic.right_diagnostic_tick,
                       (unsigned long)diagnostic_pair_skew,
                       (unsigned long)left_diagnostic_age,
                       (unsigned long)right_diagnostic_age,
                       (long)motor_diagnostic.left_position_counts,
                       (long)motor_diagnostic.right_position_counts,
                       (unsigned int)motor_diagnostic.position_pair_valid,
                       (unsigned long)motor_diagnostic.position_pair_sequence,
                       (unsigned long)motor_diagnostic.left_position_tick,
                       (unsigned long)motor_diagnostic.right_position_tick,
                       (unsigned long)position_pair_skew,
                       (unsigned long)left_position_age,
                       (unsigned long)right_position_age,
                       (unsigned long)motor_write_diagnostics.sequence,
                       (unsigned int)motor_write_diagnostics.left_last_ok,
                       (unsigned int)motor_write_diagnostics.right_last_ok,
                       (unsigned long)motor_write_diagnostics.left_ok_count,
                       (unsigned long)motor_write_diagnostics.right_ok_count,
                       (unsigned long)motor_write_diagnostics.left_fail_count,
                       (unsigned long)motor_write_diagnostics.right_fail_count,
                       motor_write_diagnostics.left_last_value,
                       motor_write_diagnostics.right_last_value,
                       (unsigned long)motor_write_diagnostics.left_last_tick,
                       (unsigned long)motor_write_diagnostics.right_last_tick,
                       (unsigned long)motor_write_diagnostics.left_last_sequence,
                       (unsigned long)motor_write_diagnostics.right_last_sequence,
                       (unsigned int)telemetry_config_scan_done,
                       (unsigned long)telemetry_config_left_support_mask,
                       (unsigned long)telemetry_config_right_support_mask,
                       (unsigned long)telemetry_config_mismatch_mask,
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_FW_YEAR],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_FW_YEAR],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_FW_DATE],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_FW_DATE],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_SPEED_KP],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_SPEED_KP],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_SPEED_KI],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_SPEED_KI],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_ZERO_HOLD_DELAY],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_ZERO_HOLD_DELAY],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_ACCEL_TIME],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_ACCEL_TIME],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_DECEL_TIME],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_DECEL_TIME],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_FORWARD_TORQUE_LIMIT],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_FORWARD_TORQUE_LIMIT],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_INERTIA],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_INERTIA],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_PID_ALGORITHM],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_PID_ALGORITHM],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_ZERO_SPEED_THRESHOLD],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_ZERO_SPEED_THRESHOLD],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_ZERO_SPEED_FILTER],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_ZERO_SPEED_FILTER],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_TORQUE_LIMIT_ENABLE],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_TORQUE_LIMIT_ENABLE],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_REVERSE_TORQUE_LIMIT],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_REVERSE_TORQUE_LIMIT],
                       (unsigned int)telemetry_config_left_values[MOTOR_CONFIG_MAX_CURRENT],
                       (unsigned int)telemetry_config_right_values[MOTOR_CONFIG_MAX_CURRENT]);

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
		SBUS_TimeoutCheck();
		if(sbus_failsafe)
		{
				rc_enter_not_ready(now);
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
				RcFrameTrustResult frame_result = rc_frame_trust_result(ch, local_failsafe, local_frame_lost);
				uint8_t frame_accepted = (frame_result == RC_FRAME_ACCEPTED);
				if(frame_accepted)
				{
						rc_accept_trustworthy_frame(ch, now);
				}
				else if(frame_result == RC_FRAME_REJECTED)
				{
						rc_enter_not_ready(now);
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
			rc_enter_not_ready(now);
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

				caster_alignment_update(requested_linear,
				                        requested_steer,
				                        HAL_GetTick(),
				                        &motor_conditioned_linear,
				                        &motor_conditioned_steer);

				/* 固定周期推进轨迹，不再依赖是否恰好收到一帧新的 SBUS 数据。 */
				if(caster_alignment.state == CASTER_ALIGN_FAILED)
				{
					straight_sync_reset();
					motor_trajectory_reset();
					motor_speed_control_update(0, 0);
				}
				else
				{
					motor_trajectory_update(motor_conditioned_linear,
					                        motor_conditioned_steer,
					                        &commanded_left_rpm,
					                        &commanded_right_rpm);
					straight_sync_apply(&commanded_left_rpm,
					                    &commanded_right_rpm,
					                    HAL_GetTick());
					motor_speed_control_update(commanded_left_rpm, -commanded_right_rpm);
				}
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

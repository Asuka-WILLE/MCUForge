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
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TELEMETRY_PERIOD_MS 200U
#define TELEMETRY_LINE_MAX  160U
#define TELEMETRY_RESPONSE_LEN 7U
#define TELEMETRY_MOTOR_TIMEOUT_MS 10U
#define TELEMETRY_LIFT_TIMEOUT_MS  15U
#define TELEMETRY_TX_TIMEOUT_MS    5U
#define TELEMETRY_CONTROL_HOLDOFF_MS 5U

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
    if (abs(diff) < deadzone)
        return 0;
    return diff;
}

/**
 * @brief  加速度限制
 * @param  current 当前速度
 * @param  desired 期望速度
 * @param  max_accel 每周期最大加速度
 * @retval 限制后的速度
 */
static int16_t accel_limit(int16_t current, int16_t desired, int16_t max_accel)
{
    int16_t diff = desired - current;
    if (diff > max_accel)
        return current + max_accel;
    else if (diff < -max_accel)
        return current - max_accel;
    else
        return desired;
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

static const char *telemetry_state_text(void)
{
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
    return "RUN";
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
        }
        else if(telemetry_query_slave == 2)
        {
            right = value;
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

static void telemetry_control_command_begin(void)
{
    /* 遥控/电机/升降写命令优先：发控制命令前丢弃后台读取，避免旧回包污染下一帧。 */
    telemetry_abort_pending_query();
}

static void telemetry_control_command_end(void)
{
    telemetry_control_last_tick = HAL_GetTick();
}

static void telemetry_service_query(uint32_t now, uint8_t allow_start)
{
    uint8_t response_ready;
    uint8_t timeout;

    switch(telemetry_query_state)
    {
        case TELEMETRY_QUERY_IDLE:
            if((now - telemetry_last_query_tick) >= TELEMETRY_PERIOD_MS &&
               (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                telemetry_last_query_tick = now;
                telemetry_query_state = TELEMETRY_SEND_LEFT;
            }
            break;

        case TELEMETRY_SEND_LEFT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_motor_query(1, TELEMETRY_WAIT_LEFT))
                {
                    telemetry_query_state = TELEMETRY_SEND_RIGHT;
                }
            }
            break;

        case TELEMETRY_WAIT_LEFT:
            response_ready = telemetry_poll_uart_response(&huart2);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_MOTOR_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                telemetry_finish_motor_query(response_ready);
                telemetry_query_state = TELEMETRY_SEND_RIGHT;
            }
            break;

        case TELEMETRY_SEND_RIGHT:
            if(allow_start && (now - telemetry_control_last_tick) >= TELEMETRY_CONTROL_HOLDOFF_MS)
            {
                if(!telemetry_start_motor_query(2, TELEMETRY_WAIT_RIGHT))
                {
                    telemetry_query_state = TELEMETRY_SEND_LIFT;
                }
            }
            break;

        case TELEMETRY_WAIT_RIGHT:
            response_ready = telemetry_poll_uart_response(&huart2);
            timeout = ((now - telemetry_query_start_tick) >= TELEMETRY_MOTOR_TIMEOUT_MS);
            if(response_ready || timeout)
            {
                telemetry_finish_motor_query(response_ready);
                telemetry_query_state = TELEMETRY_SEND_LIFT;
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
            telemetry_reset_rx_buffer();
            break;
    }
}

static void telemetry_send(void)
{
    char line[TELEMETRY_LINE_MAX];
    int len = snprintf(line, sizeof(line),
                       "{\"left_rpm\":%d,\"right_rpm\":%d,\"speed_rpm\":%d,\"state\":\"%s\",\"height_mm\":%d}\r\n",
                       left,
                       right,
                       current_speed_rpm,
                       telemetry_state_text(),
                       lift_height_mm);

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
	// 开启LCD背光
	HAL_Delay(10);
	LCD_Init();//LCD初始化
	LCD_Fill(0,0,LCD_W, LCD_H,BLACK);	
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
	HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_val,1);	// 读取ADC按键键值
	HAL_TIM_Base_Start_IT(&htim2);
	
	HAL_Delay(1000);
	SBUS_Init();
  //	change_station();  //手动切换站号
	//	motor_start_init();  //手动使能
	//speed_set(10, 10);   // 左电机 10rpm，右电机 10rpm
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
		lift_up();
		HAL_Delay(100);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {	
		
		telemetry_process_poll_only();
		
		SBUS_TimeoutCheck();
		if(sbus_frame_ok)
		{
				__disable_irq();
				uint8_t local_buf[SBUS_FRAME_LEN];
				memcpy(local_buf, (void*)sbus_buf, SBUS_FRAME_LEN);
				uint8_t local_failsafe = sbus_failsafe;
				telemetry_failsafe = local_failsafe;
				sbus_frame_ok = 0;
				__enable_irq();

				int16_t ch[16] = {0};
				SBUS_ParseChannels(local_buf, ch);
				
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

			

				if(!local_failsafe)
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
								if(ch[5] < 500 && (emergency_stop || !en_flag))
								{
										telemetry_control_command_begin();
										motor_clear_emergency_stop();
										telemetry_control_command_end();
								}

								// ===================== 第二步：电机驱动 =====================
								int16_t desired_speed, desired_steer;
								int16_t desired_left_rpm, desired_right_rpm;
							//	int16_t allowed_left_rpm, allowed_right_rpm;

								// 正确顺序
								desired_speed =  (ch[2] - 992) / 32;  // 前后油门 CH3
								desired_steer = (ch[3] - 988)/ 32;  // 左右转向	CH4

								desired_left_rpm  = desired_speed + desired_steer / 4;
								desired_right_rpm = desired_speed - desired_steer / 4;

								telemetry_control_command_begin();
								speed_set(desired_left_rpm,-desired_right_rpm);
								telemetry_control_command_end();

								// ===================== 第三步：升降控制 =====================
								LiftState desired_lift_state;  // 用户想要的状态
						//		LiftState allowed_lift_state;  // 系统允许的状态

								// 3.1 先计算用户想要的状态（只看摇杆）
								if(ch[7] < 500)
										desired_lift_state = LIFT_UP;
								else if(ch[7] > 1500)
										desired_lift_state = LIFT_DOWN;
								else
										desired_lift_state = LIFT_STOP;

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
						//motor_emergency_stop();
						//lift_stop();
						//current_lift_state = LIFT_STOP;
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
		telemetry_failsafe = sbus_failsafe;
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

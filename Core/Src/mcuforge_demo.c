#include "mcuforge_demo.h"

#include "lcd.h"
#include "usbd_cdc_if.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MCUFORGE_FRAME_HEADER_0 0xAAU
#define MCUFORGE_FRAME_HEADER_1 0x55U
#define MCUFORGE_PROTOCOL_VERSION 0x01U
#define MCUFORGE_FRAME_TYPE_CONTROL 0x01U
#define MCUFORGE_FLAG_ENABLE 0x01U
#define MCUFORGE_FLAG_ESTOP 0x02U
#define MCUFORGE_COMMAND_LIMIT 1000
#define MCUFORGE_RX_RING_SIZE 128U
#define MCUFORGE_SEQUENCE_RESYNC_MS 150U
#define MCUFORGE_TELEMETRY_PERIOD_MS 50U
#define MCUFORGE_LCD_REFRESH_MS 100U
#define MCUFORGE_LCD_FONT_SIZE 16U
#define MCUFORGE_LCD_ROW_STEP 20U
#define MCUFORGE_LCD_ROWS 8U
#define MCUFORGE_LCD_LINE_CHARS (LCD_W / (MCUFORGE_LCD_FONT_SIZE / 2U))

typedef struct
{
    uint16_t sequence;
    int16_t throttle;
    int16_t steering;
    uint8_t enabled;
    uint8_t emergency_stop_requested;
    uint8_t frame_seen;
    int16_t virtual_left;
    int16_t virtual_right;
    uint32_t last_valid_tick;
    uint32_t valid_frame_count;
    uint32_t invalid_frame_count;
    uint32_t crc_error_count;
    uint32_t range_error_count;
    uint32_t sequence_error_count;
} MCUForgeDemoState;

static volatile uint8_t rx_ring[MCUFORGE_RX_RING_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint32_t rx_overflow_count;
static uint8_t frame_buffer[MCUFORGE_CONTROL_FRAME_SIZE];
static uint8_t frame_length;
static MCUForgeDemoState demo_state;
static uint32_t telemetry_last_tick;
static uint32_t lcd_last_tick;
static char lcd_line_cache[MCUFORGE_LCD_ROWS][MCUFORGE_LCD_LINE_CHARS + 1U];
static uint16_t lcd_color_cache[MCUFORGE_LCD_ROWS];
static uint8_t lcd_cache_valid[MCUFORGE_LCD_ROWS];

static uint16_t mcuforge_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for(index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for(bit = 0U; bit < 8U; ++bit)
        {
            if((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static int16_t mcuforge_read_i16_le(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint16_t mcuforge_read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static int16_t mcuforge_clamp_command(int32_t value)
{
    if(value > MCUFORGE_COMMAND_LIMIT)
    {
        return MCUFORGE_COMMAND_LIMIT;
    }
    if(value < -MCUFORGE_COMMAND_LIMIT)
    {
        return -MCUFORGE_COMMAND_LIMIT;
    }
    return (int16_t)value;
}

static uint8_t mcuforge_sequence_is_newer(uint16_t sequence, uint32_t now)
{
    uint16_t difference;

    if(!demo_state.frame_seen ||
       (now - demo_state.last_valid_tick) > MCUFORGE_SEQUENCE_RESYNC_MS)
    {
        return 1U;
    }
    difference = (uint16_t)(sequence - demo_state.sequence);
    return (difference != 0U && difference < 0x8000U) ? 1U : 0U;
}

static void mcuforge_accept_frame(const uint8_t frame[MCUFORGE_CONTROL_FRAME_SIZE], uint32_t now)
{
    int16_t throttle = mcuforge_read_i16_le(&frame[6]);
    int16_t steering = mcuforge_read_i16_le(&frame[8]);
    uint8_t flags = frame[10];

    demo_state.sequence = mcuforge_read_u16_le(&frame[4]);
    demo_state.throttle = throttle;
    demo_state.steering = steering;
    demo_state.enabled = ((flags & MCUFORGE_FLAG_ENABLE) != 0U) ? 1U : 0U;
    demo_state.emergency_stop_requested = ((flags & MCUFORGE_FLAG_ESTOP) != 0U) ? 1U : 0U;
    demo_state.frame_seen = 1U;
    demo_state.last_valid_tick = now;
    demo_state.valid_frame_count++;

    if(demo_state.enabled)
    {
        demo_state.virtual_left = mcuforge_clamp_command((int32_t)throttle + steering);
        demo_state.virtual_right = mcuforge_clamp_command((int32_t)throttle - steering);
    }
    else
    {
        demo_state.virtual_left = 0;
        demo_state.virtual_right = 0;
    }

    /*
     * No timeout, recovery gate, or emergency-stop action is implemented in
     * this baseline. Fixed FS tests must expose those missing behaviors so the
     * final AgentTeams task has a real implementation target.
     */
}

static void mcuforge_validate_frame(uint32_t now)
{
    uint16_t expected_crc;
    uint16_t actual_crc;
    uint16_t sequence;
    int16_t throttle;
    int16_t steering;

    if(frame_buffer[2] != MCUFORGE_PROTOCOL_VERSION ||
       frame_buffer[3] != MCUFORGE_FRAME_TYPE_CONTROL ||
       (frame_buffer[10] & 0xFCU) != 0U ||
       frame_buffer[11] != 0U)
    {
        demo_state.invalid_frame_count++;
        return;
    }

    expected_crc = mcuforge_crc16(frame_buffer, MCUFORGE_CONTROL_FRAME_SIZE - 2U);
    actual_crc = mcuforge_read_u16_le(&frame_buffer[MCUFORGE_CONTROL_FRAME_SIZE - 2U]);
    if(expected_crc != actual_crc)
    {
        demo_state.invalid_frame_count++;
        demo_state.crc_error_count++;
        return;
    }

    throttle = mcuforge_read_i16_le(&frame_buffer[6]);
    steering = mcuforge_read_i16_le(&frame_buffer[8]);
    if(throttle < -MCUFORGE_COMMAND_LIMIT || throttle > MCUFORGE_COMMAND_LIMIT ||
       steering < -MCUFORGE_COMMAND_LIMIT || steering > MCUFORGE_COMMAND_LIMIT)
    {
        demo_state.invalid_frame_count++;
        demo_state.range_error_count++;
        return;
    }

    sequence = mcuforge_read_u16_le(&frame_buffer[4]);
    if(!mcuforge_sequence_is_newer(sequence, now))
    {
        demo_state.invalid_frame_count++;
        demo_state.sequence_error_count++;
        return;
    }

    mcuforge_accept_frame(frame_buffer, now);
}

static void mcuforge_parse_byte(uint8_t byte, uint32_t now)
{
    if(frame_length == 0U)
    {
        if(byte == MCUFORGE_FRAME_HEADER_0)
        {
            frame_buffer[0] = byte;
            frame_length = 1U;
        }
        return;
    }

    if(frame_length == 1U)
    {
        if(byte == MCUFORGE_FRAME_HEADER_1)
        {
            frame_buffer[1] = byte;
            frame_length = 2U;
        }
        else if(byte != MCUFORGE_FRAME_HEADER_0)
        {
            frame_length = 0U;
        }
        return;
    }

    frame_buffer[frame_length++] = byte;
    if(frame_length == MCUFORGE_CONTROL_FRAME_SIZE)
    {
        mcuforge_validate_frame(now);
        frame_length = 0U;
    }
}

static void mcuforge_process_rx(uint32_t now)
{
    while(rx_tail != rx_head)
    {
        uint8_t byte = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % MCUFORGE_RX_RING_SIZE);
        mcuforge_parse_byte(byte, now);
    }
}

static void mcuforge_lcd_show_line(uint8_t row, uint16_t color, const char *format, ...)
{
    char line[MCUFORGE_LCD_LINE_CHARS + 1U];
    size_t length;
    va_list args;

    if(row >= MCUFORGE_LCD_ROWS)
    {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    length = strlen(line);
    if(length < MCUFORGE_LCD_LINE_CHARS)
    {
        memset(&line[length], ' ', MCUFORGE_LCD_LINE_CHARS - length);
        line[MCUFORGE_LCD_LINE_CHARS] = '\0';
    }
    else
    {
        line[MCUFORGE_LCD_LINE_CHARS] = '\0';
    }

    if(lcd_cache_valid[row] && lcd_color_cache[row] == color &&
       strcmp(lcd_line_cache[row], line) == 0)
    {
        return;
    }

    LCD_ShowString(0U, (uint16_t)(row * MCUFORGE_LCD_ROW_STEP),
                   (const uint8_t *)line, color, BLACK,
                   MCUFORGE_LCD_FONT_SIZE, 0U);
    strcpy(lcd_line_cache[row], line);
    lcd_color_cache[row] = color;
    lcd_cache_valid[row] = 1U;
}

static void mcuforge_lcd_process(uint32_t now)
{
    if((now - lcd_last_tick) < MCUFORGE_LCD_REFRESH_MS)
    {
        return;
    }
    lcd_last_tick = now;

    mcuforge_lcd_show_line(0U, CYAN, "MCUFORGE DEMO BASE");
    if(!demo_state.frame_seen)
    {
        mcuforge_lcd_show_line(1U, YELLOW, "Waiting PC USB frame...");
        mcuforge_lcd_show_line(2U, WHITE, "No motor / virtual only");
        mcuforge_lcd_show_line(3U, WHITE, "SEQ:-- VALID:0");
        mcuforge_lcd_show_line(4U, WHITE, "THR:0 STR:0");
        mcuforge_lcd_show_line(5U, WHITE, "VLEFT:0 VRIGHT:0");
    }
    else
    {
        mcuforge_lcd_show_line(1U, GREEN, "INPUT:PC USB SEQ:%u", demo_state.sequence);
        mcuforge_lcd_show_line(2U, WHITE, "EN:%u EST_REQ:%u", demo_state.enabled,
                              demo_state.emergency_stop_requested);
        mcuforge_lcd_show_line(3U, WHITE, "THR:%d STR:%d", demo_state.throttle,
                              demo_state.steering);
        mcuforge_lcd_show_line(4U, WHITE, "VLEFT:%d", demo_state.virtual_left);
        mcuforge_lcd_show_line(5U, WHITE, "VRIGHT:%d", demo_state.virtual_right);
    }
    mcuforge_lcd_show_line(6U, WHITE, "VALID:%lu BAD:%lu",
                          (unsigned long)demo_state.valid_frame_count,
                          (unsigned long)demo_state.invalid_frame_count);
    mcuforge_lcd_show_line(7U, YELLOW, "BASELINE: NO FAILSAFE");
}

static void mcuforge_telemetry_process(uint32_t now)
{
    char line[512];
    int length;
    uint32_t frame_age = demo_state.frame_seen ? (now - demo_state.last_valid_tick) : 0U;

    if((now - telemetry_last_tick) < MCUFORGE_TELEMETRY_PERIOD_MS)
    {
        return;
    }
    telemetry_last_tick = now;

    length = snprintf(line, sizeof(line),
                      "{\"tick_ms\":%lu,\"demo_mode\":1,\"input_source\":\"PC_USB\","
                      "\"pc_frame_seen\":%u,\"pc_frame_age_ms\":%lu,\"pc_sequence\":%u,\"pc_throttle\":%d,"
                      "\"pc_steering\":%d,\"pc_enabled\":%u,\"pc_estop_requested\":%u,"
                      "\"left_cmd\":%d,\"right_cmd\":%d,\"cmd_valid\":%u,"
                      "\"state\":\"DEMO_BASELINE\",\"pc_valid_frame_count\":%lu,"
                      "\"pc_invalid_frame_count\":%lu,\"pc_crc_error_count\":%lu,"
                      "\"pc_range_error_count\":%lu,\"pc_sequence_error_count\":%lu,"
                      "\"pc_rx_overflow_count\":%lu}\r\n",
                      (unsigned long)now,
                      (unsigned int)demo_state.frame_seen,
                      (unsigned long)frame_age,
                      (unsigned int)demo_state.sequence,
                      demo_state.throttle,
                      demo_state.steering,
                      (unsigned int)demo_state.enabled,
                      (unsigned int)demo_state.emergency_stop_requested,
                      demo_state.virtual_left,
                      demo_state.virtual_right,
                      (unsigned int)(demo_state.frame_seen && demo_state.enabled),
                      (unsigned long)demo_state.valid_frame_count,
                      (unsigned long)demo_state.invalid_frame_count,
                      (unsigned long)demo_state.crc_error_count,
                      (unsigned long)demo_state.range_error_count,
                      (unsigned long)demo_state.sequence_error_count,
                      (unsigned long)rx_overflow_count);

    if(length > 0 && length < (int)sizeof(line))
    {
        (void)CDC_Transmit_HS((uint8_t *)line, (uint16_t)length);
    }
}

void MCUForge_Demo_Init(void)
{
    memset(&demo_state, 0, sizeof(demo_state));
    memset(frame_buffer, 0, sizeof(frame_buffer));
    memset(lcd_cache_valid, 0, sizeof(lcd_cache_valid));
    rx_head = 0U;
    rx_tail = 0U;
    rx_overflow_count = 0U;
    frame_length = 0U;
    telemetry_last_tick = 0U;
    lcd_last_tick = 0U;

    LCD_Fill(0U, 0U, LCD_W, LCD_H, BLACK);
    LCD_DrawRectangle(0U, 0U, LCD_W - 1U, LCD_H - 1U, WHITE);
    LCD_Fill(0U, LCD_H - 16U, LCD_W / 4U, LCD_H, RED);
    LCD_Fill(LCD_W / 4U, LCD_H - 16U, LCD_W / 2U, LCD_H, GREEN);
    LCD_Fill(LCD_W / 2U, LCD_H - 16U, (LCD_W * 3U) / 4U, LCD_H, BLUE);
    LCD_Fill((LCD_W * 3U) / 4U, LCD_H - 16U, LCD_W, LCD_H, YELLOW);
    mcuforge_lcd_show_line(0U, CYAN, "MCUFORGE DEMO BASE");
    mcuforge_lcd_show_line(1U, YELLOW, "Waiting PC USB frame...");
}

void MCUForge_Demo_ReceiveBytes(const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if(data == NULL)
    {
        return;
    }

    for(index = 0U; index < length; ++index)
    {
        uint16_t next_head = (uint16_t)((rx_head + 1U) % MCUFORGE_RX_RING_SIZE);
        if(next_head == rx_tail)
        {
            rx_overflow_count++;
            break;
        }
        rx_ring[rx_head] = data[index];
        rx_head = next_head;
    }
}

void MCUForge_Demo_Process(uint32_t now)
{
    mcuforge_process_rx(now);
    mcuforge_lcd_process(now);
    mcuforge_telemetry_process(now);
}

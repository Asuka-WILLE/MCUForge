#ifndef __SBUS_H
#define __SBUS_H

#include "main.h"

#define SBUS_FRAME_LEN 25	
#define SBUS_CHANNEL_MIN 172
#define SBUS_CHANNEL_MID 992
#define SBUS_CHANNEL_MAX 1811
#define SBUS_NUM_CHANNELS 16

extern uint16_t sbus_ch[16];
extern volatile uint8_t sbus_frame_ok;

void SBUS_Init(void);
void SBUS_Decode(void);
void SBUS_TimeoutCheck(void);
void SBUS_ParseChannels(const uint8_t sbus_buf[SBUS_FRAME_LEN], int16_t channels[SBUS_NUM_CHANNELS]);

#endif
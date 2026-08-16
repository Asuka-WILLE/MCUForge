#ifndef MCUFORGE_DEMO_H
#define MCUFORGE_DEMO_H

#include <stdint.h>

/* Competition mode bypasses production SBUS and RS485 motor paths. */
#define MCUFORGE_DEMO_MODE 1U

#define MCUFORGE_CONTROL_FRAME_SIZE 14U

void MCUForge_Demo_Init(void);
void MCUForge_Demo_ReceiveBytes(const uint8_t *data, uint32_t length);
void MCUForge_Demo_Process(uint32_t now);

#endif /* MCUFORGE_DEMO_H */

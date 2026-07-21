#ifndef __IMU_H__
#define __IMU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
    IMU_STATUS_OK = 0,
    IMU_STATUS_NOT_INITIALIZED,
    IMU_STATUS_INVALID_ARGUMENT,
    IMU_STATUS_SPI_ERROR,
    IMU_STATUS_ACCEL_NOT_FOUND,
    IMU_STATUS_GYRO_NOT_FOUND,
    IMU_STATUS_ACCEL_CONFIG_ERROR,
    IMU_STATUS_GYRO_CONFIG_ERROR
} IMU_Status;

typedef struct
{
    /* BMI088 native sensor axes; board/body-axis mapping is intentionally deferred. */
    float accel_mps2[3];
    float gyro_rad_s[3];
    float temperature_c;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint8_t valid;
} IMU_Sample;

/*
 * Reserved interface for the later USB/upper-computer integration.
 * The calls in main.c remain commented out for the current release.
 */
IMU_Status IMU_Init(void);
IMU_Status IMU_Poll(uint32_t now_ms);
IMU_Status IMU_GetLastStatus(void);
uint8_t IMU_IsInitialized(void);
uint8_t IMU_CopyLatest(IMU_Sample *sample);

#ifdef __cplusplus
}
#endif

#endif /* __IMU_H__ */

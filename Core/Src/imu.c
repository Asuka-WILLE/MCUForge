#include "imu.h"

#include "main.h"
#include "spi.h"

#include <string.h>

#define BMI088_SPI_TIMEOUT_MS           5U
#define BMI088_POLL_PERIOD_MS           10U
#define BMI088_ACCEL_CHIP_ID_REG        0x00U
#define BMI088_ACCEL_CHIP_ID_VALUE      0x1EU
#define BMI088_ACCEL_DATA_REG           0x12U
#define BMI088_ACCEL_TEMP_REG           0x22U
#define BMI088_ACCEL_CONF_REG           0x40U
#define BMI088_ACCEL_RANGE_REG          0x41U
#define BMI088_ACCEL_INT1_IO_REG        0x53U
#define BMI088_ACCEL_INT_MAP_REG        0x58U
#define BMI088_ACCEL_PWR_CONF_REG       0x7CU
#define BMI088_ACCEL_PWR_CTRL_REG       0x7DU
#define BMI088_ACCEL_SOFTRESET_REG      0x7EU
#define BMI088_GYRO_CHIP_ID_REG         0x00U
#define BMI088_GYRO_CHIP_ID_VALUE       0x0FU
#define BMI088_GYRO_DATA_REG            0x02U
#define BMI088_GYRO_RANGE_REG           0x0FU
#define BMI088_GYRO_BANDWIDTH_REG       0x10U
#define BMI088_GYRO_LPM1_REG            0x11U
#define BMI088_GYRO_SOFTRESET_REG       0x14U
#define BMI088_GYRO_CTRL_REG            0x15U
#define BMI088_GYRO_INT_IO_REG          0x16U
#define BMI088_GYRO_INT_MAP_REG         0x18U
#define BMI088_SOFTRESET_VALUE          0xB6U
#define BMI088_READ_BIT                 0x80U
#define BMI088_ACCEL_SCALE_MPS2         0.0008974358974f
#define BMI088_GYRO_SCALE_RAD_S         0.0010652644360f
#define BMI088_TEMP_SCALE_C             0.125f
#define BMI088_TEMP_OFFSET_C            23.0f

typedef enum
{
    BMI088_SENSOR_ACCEL = 0,
    BMI088_SENSOR_GYRO
} BMI088_Sensor;

typedef struct
{
    uint8_t reg;
    uint8_t value;
    uint8_t delay_ms;
} BMI088_RegisterConfig;

static const BMI088_RegisterConfig bmi088_accel_config[] =
{
    {BMI088_ACCEL_PWR_CTRL_REG, 0x04U, 5U},
    {BMI088_ACCEL_PWR_CONF_REG, 0x00U, 5U},
    {BMI088_ACCEL_CONF_REG, 0xABU, 1U},       /* normal filter, 800 Hz ODR */
    {BMI088_ACCEL_RANGE_REG, 0x00U, 1U},      /* +/-3 g */
    {BMI088_ACCEL_INT1_IO_REG, 0x08U, 1U},    /* data-ready output reserved */
    {BMI088_ACCEL_INT_MAP_REG, 0x04U, 1U}
};

static const BMI088_RegisterConfig bmi088_gyro_config[] =
{
    {BMI088_GYRO_RANGE_REG, 0x00U, 1U},       /* +/-2000 dps */
    {BMI088_GYRO_BANDWIDTH_REG, 0x82U, 1U},   /* 1000 Hz ODR, 116 Hz BW */
    {BMI088_GYRO_LPM1_REG, 0x00U, 1U},        /* normal mode */
    {BMI088_GYRO_CTRL_REG, 0x80U, 1U},        /* data-ready enabled */
    {BMI088_GYRO_INT_IO_REG, 0x00U, 1U},
    {BMI088_GYRO_INT_MAP_REG, 0x01U, 1U}
};

static IMU_Sample imu_latest_sample;
static IMU_Status imu_last_status = IMU_STATUS_NOT_INITIALIZED;
static uint32_t imu_last_poll_tick;
static uint8_t imu_initialized;

static void bmi088_select(BMI088_Sensor sensor, GPIO_PinState state)
{
    if(sensor == BMI088_SENSOR_ACCEL)
    {
        HAL_GPIO_WritePin(IMU_ACCEL_CS_GPIO_Port, IMU_ACCEL_CS_Pin, state);
    }
    else
    {
        HAL_GPIO_WritePin(IMU_GYRO_CS_GPIO_Port, IMU_GYRO_CS_Pin, state);
    }
}

static HAL_StatusTypeDef bmi088_transfer(BMI088_Sensor sensor,
                                         uint8_t *tx,
                                         uint8_t *rx,
                                         uint16_t length)
{
    HAL_StatusTypeDef status;

    bmi088_select(sensor, GPIO_PIN_RESET);
    status = HAL_SPI_TransmitReceive(&hspi2, tx, rx, length, BMI088_SPI_TIMEOUT_MS);
    bmi088_select(sensor, GPIO_PIN_SET);

    return status;
}

static HAL_StatusTypeDef bmi088_write_register(BMI088_Sensor sensor,
                                                uint8_t reg,
                                                uint8_t value)
{
    uint8_t tx[2] = {reg, value};
    uint8_t rx[2] = {0U, 0U};

    return bmi088_transfer(sensor, tx, rx, 2U);
}

static HAL_StatusTypeDef bmi088_read_register(BMI088_Sensor sensor,
                                               uint8_t reg,
                                               uint8_t *value)
{
    uint8_t tx[3] = {(uint8_t)(reg | BMI088_READ_BIT), 0U, 0U};
    uint8_t rx[3] = {0U, 0U, 0U};
    uint16_t length = (sensor == BMI088_SENSOR_ACCEL) ? 3U : 2U;
    HAL_StatusTypeDef status;

    if(value == NULL)
    {
        return HAL_ERROR;
    }

    status = bmi088_transfer(sensor, tx, rx, length);
    if(status == HAL_OK)
    {
        *value = rx[length - 1U];
    }

    return status;
}

static HAL_StatusTypeDef bmi088_read_block(BMI088_Sensor sensor,
                                            uint8_t start_reg,
                                            uint8_t *data,
                                            uint16_t data_length)
{
    uint8_t tx[8] = {0U};
    uint8_t rx[8] = {0U};
    uint16_t prefix_length = (sensor == BMI088_SENSOR_ACCEL) ? 2U : 1U;
    uint16_t transfer_length = prefix_length + data_length;
    HAL_StatusTypeDef status;

    if(data == NULL || data_length == 0U || transfer_length > sizeof(tx))
    {
        return HAL_ERROR;
    }

    tx[0] = (uint8_t)(start_reg | BMI088_READ_BIT);
    status = bmi088_transfer(sensor, tx, rx, transfer_length);
    if(status == HAL_OK)
    {
        memcpy(data, &rx[prefix_length], data_length);
    }

    return status;
}

static IMU_Status bmi088_apply_config(BMI088_Sensor sensor,
                                       const BMI088_RegisterConfig *config,
                                       uint32_t count,
                                       IMU_Status config_error)
{
    uint32_t index;

    for(index = 0U; index < count; ++index)
    {
        uint8_t readback = 0U;

        if(bmi088_write_register(sensor, config[index].reg, config[index].value) != HAL_OK)
        {
            return IMU_STATUS_SPI_ERROR;
        }
        HAL_Delay(config[index].delay_ms);
        if(bmi088_read_register(sensor, config[index].reg, &readback) != HAL_OK)
        {
            return IMU_STATUS_SPI_ERROR;
        }
        if(readback != config[index].value)
        {
            return config_error;
        }
    }

    return IMU_STATUS_OK;
}

static IMU_Status bmi088_init_accel(void)
{
    uint8_t chip_id = 0U;
    IMU_Status status;

    /* The first accelerometer read switches the device from I2C to SPI mode. */
    if(bmi088_read_register(BMI088_SENSOR_ACCEL, BMI088_ACCEL_CHIP_ID_REG, &chip_id) != HAL_OK ||
       bmi088_read_register(BMI088_SENSOR_ACCEL, BMI088_ACCEL_CHIP_ID_REG, &chip_id) != HAL_OK)
    {
        return IMU_STATUS_SPI_ERROR;
    }
    if(chip_id != BMI088_ACCEL_CHIP_ID_VALUE)
    {
        return IMU_STATUS_ACCEL_NOT_FOUND;
    }

    if(bmi088_write_register(BMI088_SENSOR_ACCEL,
                             BMI088_ACCEL_SOFTRESET_REG,
                             BMI088_SOFTRESET_VALUE) != HAL_OK)
    {
        return IMU_STATUS_SPI_ERROR;
    }
    HAL_Delay(80U);

    if(bmi088_read_register(BMI088_SENSOR_ACCEL, BMI088_ACCEL_CHIP_ID_REG, &chip_id) != HAL_OK ||
       bmi088_read_register(BMI088_SENSOR_ACCEL, BMI088_ACCEL_CHIP_ID_REG, &chip_id) != HAL_OK)
    {
        return IMU_STATUS_SPI_ERROR;
    }
    if(chip_id != BMI088_ACCEL_CHIP_ID_VALUE)
    {
        return IMU_STATUS_ACCEL_NOT_FOUND;
    }

    status = bmi088_apply_config(BMI088_SENSOR_ACCEL,
                                 bmi088_accel_config,
                                 sizeof(bmi088_accel_config) / sizeof(bmi088_accel_config[0]),
                                 IMU_STATUS_ACCEL_CONFIG_ERROR);
    return status;
}

static IMU_Status bmi088_init_gyro(void)
{
    uint8_t chip_id = 0U;

    if(bmi088_read_register(BMI088_SENSOR_GYRO, BMI088_GYRO_CHIP_ID_REG, &chip_id) != HAL_OK)
    {
        return IMU_STATUS_SPI_ERROR;
    }
    if(chip_id != BMI088_GYRO_CHIP_ID_VALUE)
    {
        return IMU_STATUS_GYRO_NOT_FOUND;
    }

    if(bmi088_write_register(BMI088_SENSOR_GYRO,
                             BMI088_GYRO_SOFTRESET_REG,
                             BMI088_SOFTRESET_VALUE) != HAL_OK)
    {
        return IMU_STATUS_SPI_ERROR;
    }
    HAL_Delay(80U);

    if(bmi088_read_register(BMI088_SENSOR_GYRO, BMI088_GYRO_CHIP_ID_REG, &chip_id) != HAL_OK)
    {
        return IMU_STATUS_SPI_ERROR;
    }
    if(chip_id != BMI088_GYRO_CHIP_ID_VALUE)
    {
        return IMU_STATUS_GYRO_NOT_FOUND;
    }

    return bmi088_apply_config(BMI088_SENSOR_GYRO,
                               bmi088_gyro_config,
                               sizeof(bmi088_gyro_config) / sizeof(bmi088_gyro_config[0]),
                               IMU_STATUS_GYRO_CONFIG_ERROR);
}

static int16_t bmi088_decode_s16(const uint8_t *bytes)
{
    return (int16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

IMU_Status IMU_Init(void)
{
    IMU_Status status;

    memset(&imu_latest_sample, 0, sizeof(imu_latest_sample));
    imu_initialized = 0U;
    imu_last_poll_tick = 0U;

    HAL_GPIO_WritePin(IMU_ACCEL_CS_GPIO_Port, IMU_ACCEL_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(IMU_GYRO_CS_GPIO_Port, IMU_GYRO_CS_Pin, GPIO_PIN_SET);
    HAL_Delay(1U);

    status = bmi088_init_accel();
    if(status == IMU_STATUS_OK)
    {
        status = bmi088_init_gyro();
    }

    imu_last_status = status;
    if(status == IMU_STATUS_OK)
    {
        imu_initialized = 1U;
    }

    return status;
}

IMU_Status IMU_Poll(uint32_t now_ms)
{
    uint8_t accel_raw[6];
    uint8_t gyro_raw[6];
    uint8_t temp_raw[2];
    int16_t temperature_raw;
    uint32_t axis;

    if(!imu_initialized)
    {
        imu_last_status = IMU_STATUS_NOT_INITIALIZED;
        return imu_last_status;
    }
    if((now_ms - imu_last_poll_tick) < BMI088_POLL_PERIOD_MS)
    {
        return IMU_STATUS_OK;
    }
    imu_last_poll_tick = now_ms;

    if(bmi088_read_block(BMI088_SENSOR_ACCEL,
                         BMI088_ACCEL_DATA_REG,
                         accel_raw,
                         sizeof(accel_raw)) != HAL_OK ||
       bmi088_read_block(BMI088_SENSOR_GYRO,
                         BMI088_GYRO_DATA_REG,
                         gyro_raw,
                         sizeof(gyro_raw)) != HAL_OK ||
       bmi088_read_block(BMI088_SENSOR_ACCEL,
                         BMI088_ACCEL_TEMP_REG,
                         temp_raw,
                         sizeof(temp_raw)) != HAL_OK)
    {
        imu_latest_sample.valid = 0U;
        imu_last_status = IMU_STATUS_SPI_ERROR;
        return imu_last_status;
    }

    for(axis = 0U; axis < 3U; ++axis)
    {
        imu_latest_sample.accel_mps2[axis] =
            bmi088_decode_s16(&accel_raw[axis * 2U]) * BMI088_ACCEL_SCALE_MPS2;
        imu_latest_sample.gyro_rad_s[axis] =
            bmi088_decode_s16(&gyro_raw[axis * 2U]) * BMI088_GYRO_SCALE_RAD_S;
    }

    temperature_raw = (int16_t)(((uint16_t)temp_raw[0] << 3) | (temp_raw[1] >> 5));
    if(temperature_raw > 1023)
    {
        temperature_raw -= 2048;
    }
    imu_latest_sample.temperature_c =
        temperature_raw * BMI088_TEMP_SCALE_C + BMI088_TEMP_OFFSET_C;
    imu_latest_sample.timestamp_ms = now_ms;
    imu_latest_sample.sequence++;
    imu_latest_sample.valid = 1U;
    imu_last_status = IMU_STATUS_OK;

    return imu_last_status;
}

IMU_Status IMU_GetLastStatus(void)
{
    return imu_last_status;
}

uint8_t IMU_IsInitialized(void)
{
    return imu_initialized;
}

uint8_t IMU_CopyLatest(IMU_Sample *sample)
{
    if(sample == NULL || !imu_latest_sample.valid)
    {
        return 0U;
    }

    *sample = imu_latest_sample;
    return 1U;
}

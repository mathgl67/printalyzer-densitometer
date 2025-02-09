/*
 * Functions for performing various higher level operations with the
 * light sensor, and data types for interacting with sensor data.
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include <cmsis_os.h>

#include "stm32l0xx_hal.h"
#include "tsl2591.h"

/**
 * Sensor read light selection.
 */
typedef enum {
    SENSOR_LIGHT_OFF = 0,
    SENSOR_LIGHT_REFLECTION,
    SENSOR_LIGHT_TRANSMISSION
} sensor_light_t;

typedef enum {
    SENSOR_GAIN_CALIBRATION_STATUS_INIT = 0,
    SENSOR_GAIN_CALIBRATION_STATUS_MEDIUM,
    SENSOR_GAIN_CALIBRATION_STATUS_HIGH,
    SENSOR_GAIN_CALIBRATION_STATUS_MAXIMUM,
    SENSOR_GAIN_CALIBRATION_STATUS_FAILED,
    SENSOR_GAIN_CALIBRATION_STATUS_LED,
    SENSOR_GAIN_CALIBRATION_STATUS_COOLDOWN,
    SENSOR_GAIN_CALIBRATION_STATUS_DONE
} sensor_gain_calibration_status_t;

/**
 * Sensor reading data structure.
 */
typedef struct {
    uint16_t ch0_val;       /*!< CH0 light reading */
    uint16_t ch1_val;       /*!< CH1 light reading */
    tsl2591_gain_t gain;    /*!< Sensor ADC gain */
    tsl2591_time_t time;    /*!< Sensor ADC integration time */
    uint32_t reading_ticks; /*!< Tick time when the integration cycle finished */
    uint32_t light_ticks;   /*!< Tick time when the light state last changed */
    uint32_t reading_count; /*!< Number of integration cycles since the sensor was enabled */
} sensor_reading_t;

typedef bool (*sensor_gain_calibration_callback_t)(sensor_gain_calibration_status_t status, int param, void *user_data);
typedef bool (*sensor_time_calibration_callback_t)(tsl2591_time_t time, void *user_data);
typedef void (*sensor_read_callback_t)(void *user_data);

/**
 * Run the sensor gain calibration process.
 *
 * This function will run the sensor and transmission LED through a series of
 * measurements to determine optimal measurement brightness and the actual
 * gain values that correspond to each gain setting on the sensor.
 * The results will be saved for use in future sensor data calculations.
 *
 * @param callback Callback to monitor progress of the calibration
 * @return osOK on success
 */
osStatus_t sensor_gain_calibration(sensor_gain_calibration_callback_t callback, void *user_data);

#ifdef TEST_LIGHT_CAL
/**
 * Run the sensor light source calibration process.
 *
 * This function will turn on the selected LED and keep the sensor at constant
 * settings. It will then measure the intensity of the light over time, run a
 * logarithmic regression on the results, and save the resulting drop factor.
 *
 * @param light_source Light source to calibrate
 * @param callback Callback to monitor progress of the calibration
 * @return osOK on success
 */
osStatus_t sensor_light_calibration(sensor_light_t light_source, sensor_light_calibration_callback_t callback, void *user_data);
#endif

/**
 * Perform a target reading with the sensor.
 *
 * This function will turn on the selected LED and take a series of readings,
 * using automatic gain adjustment to arrive at a result in basic counts
 * from which target density can be calculated.
 *
 * @param light_source Light source to use for target measurement
 * @param ch0_result Channel 0 result, in basic counts
 * @param ch1_result Channel 1 result, in basic counts
 * @return osOK on success
 */
osStatus_t sensor_read_target(sensor_light_t light_source,
    float *ch0_result, float *ch1_result,
    sensor_read_callback_t callback, void *user_data);

/**
 * Perform a repeatable raw target reading with the sensor.
 *
 * This function will turn on the selected LED and take a series of readings,
 * using the exact sensor settings provided. It will return a result that
 * averages across the readings. This function is intended to be used
 * for repeatable device characterization measurements, where initial
 * conditions are set in advance and data processing happens elsewhere.
 *
 * If the sensor is saturated, then the function will return early
 * with the results set to USHRT_MAX.
 *
 * @param light_source Light source to use for target measurement
 * @param gain Sensor gain
 * @param time Sensor integration time
 * @param ch0_result Channel 0 result, in raw sensor counts
 * @param ch1_result Channel 1 result, in raw sensor counts
 * @return osOK on success
 */
osStatus_t sensor_read_target_raw(sensor_light_t light_source,
    tsl2591_gain_t gain, tsl2591_time_t time,
    uint16_t *ch0_result, uint16_t *ch1_result);

/**
 * Check the sensor reading to see if the sensor is saturated.
 *
 * @param reading Reading to check
 * @return True if saturated, false otherwise
 */
bool sensor_is_reading_saturated(const sensor_reading_t *reading);

/**
 * Convert sensor readings from raw counts to basic counts.
 *
 * Basic counts are normalized based on the sensor gain, integration time,
 * and various system constants. This allows them to be compared across
 * multiple readings and different device settings. All actual light
 * calculations shall be performed in terms of basic counts.
 *
 * @param reading Reading structure with all raw values
 * @param ch0_basic Basic count output for channel 0
 * @param ch1_basic Basic count output for channel 1
 */
void sensor_convert_to_basic_counts(const sensor_reading_t *reading, float *ch0_basic, float *ch1_basic);

/**
 * Apply the configured slope correction formula to a sensor reading.
 *
 * The input value is in basic counts, with the channels combined as
 * is normally done as part of the measurement process.
 *
 * If the slope correction values are not correctly configured, then
 * the input will be returned unmodified.
 *
 * @param basic_reading Sensor reading in combined basic counts
 * @return Slope corrected sensor reading
 */
float sensor_apply_slope_calibration(float basic_reading);

#endif /* SENSOR_H */

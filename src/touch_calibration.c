/* Touch calibration tables. */

#include "ss321.h"

/* From the 128 sensor thresholds work out, for every sensor, the gain (233 / (0x514 - threshold))
 * and the bias (253 - 1300 * gain) that the decoder uses to turn a raw reading into a pad value. */
void touch_calibration_compute(void)
{
    uint32_t i;

    for (i = 0; i < PAD_COUNT * SENSORS_PER_PAD; i++) {
        touch_gain[i] = 233.0f / (float)(int32_t)(0x514 - (uint32_t)settings.sensor_threshold[i]);
        touch_bias[i] = (float)(253.0 - (double)(touch_gain[i] * 1300.0f));
    }
}

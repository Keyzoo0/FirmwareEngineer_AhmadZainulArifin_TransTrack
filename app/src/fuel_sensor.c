/**
 * @file  fuel_sensor.c
 * @brief ADC acquisition for the fuel sender (oversampling + moving average).
 */
#include "fuel_sensor.h"
#include "bsp.h"
#include "app_config.h"

#define FUEL_VREFINT_CAL   ((const uint16_t *)0x1FFF7A2AUL)   /* F407: raw at 3.3 V, 30 C */

static uint32_t s_hist[FUEL_AVG_WINDOW];
static uint32_t s_hist_n, s_hist_i;

void fuel_sensor_init(void)
{
    s_hist_n = 0;
    s_hist_i = 0;
}

static int sample(uint32_t channel, uint32_t *avg)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel = channel;
    c.Rank = 1;
    c.SamplingTime = ADC_SAMPLETIME_480CYCLES;   /* high source impedance (10k||10k = 5 k) + VREFINT >= 10 us */
    if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) return -1;

    uint32_t sum = 0;
    for (uint32_t i = 0; i < FUEL_OVERSAMPLE; i++) {
        if (HAL_ADC_Start(&hadc1) != HAL_OK) return -1;
        if (HAL_ADC_PollForConversion(&hadc1, 5) != HAL_OK) {
            (void)HAL_ADC_Stop(&hadc1);
            return -1;
        }
        sum += HAL_ADC_GetValue(&hadc1);
    }
    (void)HAL_ADC_Stop(&hadc1);
    *avg = sum / FUEL_OVERSAMPLE;
    return 0;
}

int fuel_sensor_read(fuel_reading_t *out)
{
    uint32_t raw_ch, raw_vref;

    ADC->CCR |= ADC_CCR_TSVREFE;                    /* enable VREFINT */
    int rc = sample(ADC_CHANNEL_VREFINT, &raw_vref);
    if (rc == 0) rc = sample(ADC_CHANNEL_1, &raw_ch);
    ADC->CCR &= ~ADC_CCR_TSVREFE;                   /* save ~10 uA */
    if (rc != 0) return -1;

    s_hist[s_hist_i] = raw_ch;
    s_hist_i = (s_hist_i + 1U) % FUEL_AVG_WINDOW;
    if (s_hist_n < FUEL_AVG_WINDOW) s_hist_n++;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < s_hist_n; i++) sum += s_hist[i];

    /* Fault detection uses the instantaneous value (a broken wire must not be averaged away),
     * the level uses the moving average to suppress fuel slosh. */
    fuel_reading_t inst = fuel_calc(raw_ch, raw_vref, *FUEL_VREFINT_CAL,
                                    (uint32_t)(FUEL_DIVIDER_RATIO * 1e6f), (uint32_t)(FUEL_SHUNT_OHMS * 1000.0f));
    fuel_reading_t avg  = fuel_calc(sum / s_hist_n, raw_vref, *FUEL_VREFINT_CAL,
                                    (uint32_t)(FUEL_DIVIDER_RATIO * 1e6f), (uint32_t)(FUEL_SHUNT_OHMS * 1000.0f));
    if (inst.status == FUEL_FAULT_LOW || inst.status == FUEL_FAULT_HIGH) {
        s_hist_n = 0;                                /* restart the average after a fault */
        *out = inst;
    } else {
        *out = avg;
    }
    return 0;
}

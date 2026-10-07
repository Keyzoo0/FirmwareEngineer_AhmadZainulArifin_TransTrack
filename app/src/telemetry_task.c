/**
 * @file  telemetry_task.c
 * @brief Telemetry task: runs the INIT/IDLE/READ/TRANSMIT/ERROR state machine every 5 s.
 *
 * Degraded mode: a single failing sensor does not stop telemetry. Each cycle reports what
 * could be read plus an error bitmask; only a cycle where *nothing* could be read counts as
 * a failure for the state machine.
 */
#include "tasks.h"
#include "app_config.h"
#include "bme280.h"
#include "boot_control.h"
#include "bsp.h"
#include "event_log.h"
#include "fuel_sensor.h"
#include "gps.h"
#include "host_link.h"
#include "power_manager.h"
#include "sensor_sm.h"
#include "sys_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>

#define ERR_BME280   0x01U
#define ERR_GPS      0x02U
#define ERR_FUEL     0x04U

typedef struct {
    bme280_data_t  env;
    gps_status_t   gps;
    fuel_reading_t fuel;
    uint8_t        errors;
} sample_t;

static sm_ctx_t s_sm;
static uint32_t s_seq;
static bool     s_bme_ok;

static bool init_sensors(void)
{
    s_bme_ok = (bme280_init(bme280_hal_bus()) == BME280_OK);
    fuel_sensor_init();
    gps_start();
    /* GPS and fuel have no "probe"; the BME280 is the only device we can verify at init.
     * Telemetry continues in degraded mode without it, but INIT must at least reach the bus. */
    return true;
}

static bool read_sensors(sample_t *s)
{
    s->errors = 0;
    bsp_led(LED_ACTIVITY_PIN, true);

    if (!s_bme_ok) s_bme_ok = (bme280_init(bme280_hal_bus()) == BME280_OK);   /* hot-plug retry */
    bme280_status_t bst = s_bme_ok ? bme280_read(&s->env) : BME280_ERR_BUS;
    if (bst != BME280_OK) {
        s->errors |= ERR_BME280;
        s_bme_ok = false;
        event_log_write(EVT_SENSOR_FAIL, 1, bst);
    }

    gps_get(&s->gps);
    if (!s->gps.fix) s->errors |= ERR_GPS;   /* no fix is reported, not logged (normal indoors) */
    bsp_led(LED_GPS_PIN, s->gps.fix);
    if (s->gps.fix) power_note_activity();

    if (fuel_sensor_read(&s->fuel) != 0 || s->fuel.status == FUEL_FAULT_LOW || s->fuel.status == FUEL_FAULT_HIGH) {
        s->errors |= ERR_FUEL;
    }
    bsp_led(LED_ACTIVITY_PIN, false);

    /* Failure = no data source produced anything usable */
    return (s->errors & (ERR_BME280 | ERR_FUEL)) != (ERR_BME280 | ERR_FUEL) || s->gps.fix;
}

static int fmt_coord(char *b, size_t n, int32_t e7)
{
    uint32_t a = (uint32_t)(e7 < 0 ? -e7 : e7);
    return snprintf(b, n, "%s%lu.%07lu", e7 < 0 ? "-" : "", (unsigned long)(a / 10000000U),
                    (unsigned long)(a % 10000000U));
}

static bool transmit(const sample_t *s)
{
    char line[384], lat[16], lon[16];
    int n = snprintf(line, sizeof(line),
                     "{\"seq\":%lu,\"up\":%lu,\"fw\":\"%d.%d.%d\",\"slot\":\"%c\",\"state\":\"%s\",\"err\":%u",
                     (unsigned long)s_seq++, (unsigned long)sys_monitor_uptime_s(),
                     FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH, 'A' + APP_SLOT,
                     sm_state_str(s_sm.state), s->errors);
    if ((s->errors & ERR_BME280) == 0U) {
        int32_t t = s->env.temp_c_x100;
        uint32_t ta = (uint32_t)abs(t);
        uint32_t h10 = (s->env.hum_x1024 * 10U + 512U) / 1024U;
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      ",\"env\":{\"t\":%s%lu.%02lu,\"p\":%lu.%02lu,\"h\":%lu.%lu}",
                      t < 0 ? "-" : "", (unsigned long)(ta / 100U), (unsigned long)(ta % 100U),
                      (unsigned long)(s->env.press_pa / 100U), (unsigned long)(s->env.press_pa % 100U),
                      (unsigned long)(h10 / 10U), (unsigned long)(h10 % 10U));
    } else {
        n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"env\":null");
    }
    if (s->gps.fix) {
        fmt_coord(lat, sizeof(lat), s->gps.rmc.lat_e7);
        fmt_coord(lon, sizeof(lon), s->gps.rmc.lon_e7);
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      ",\"gps\":{\"fix\":true,\"lat\":%s,\"lon\":%s,\"kn\":%lu.%02lu,\"utc\":\"%02u:%02u:%02u\"}",
                      lat, lon, (unsigned long)(s->gps.rmc.speed_knots_x100 / 100U),
                      (unsigned long)(s->gps.rmc.speed_knots_x100 % 100U),
                      s->gps.rmc.hh, s->gps.rmc.mm, s->gps.rmc.ss);
    } else {
        n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"gps\":{\"fix\":false,\"cksum_err\":%lu}",
                      (unsigned long)s->gps.checksum_errors);
    }
    n += snprintf(line + n, sizeof(line) - (size_t)n,
                  ",\"fuel\":{\"pct\":%u.%u,\"ma\":%lu.%02lu,\"vdda\":%lu,\"st\":\"%s\"}}\r\n",
                  s->fuel.level_x10 / 10U, s->fuel.level_x10 % 10U,
                  (unsigned long)(s->fuel.loop_ua / 1000U), (unsigned long)((s->fuel.loop_ua % 1000U) / 10U),
                  (unsigned long)s->fuel.vdda_mv, fuel_status_str(s->fuel.status));
    if (n <= 0 || (size_t)n >= sizeof(line)) return false;
    return host_link_send(line, (size_t)n) == 0;
}

static bool recover(void)
{
    bsp_i2c_bus_recover();
    bsp_adc_init();
    return init_sensors() && bme280_init(bme280_hal_bus()) == BME280_OK;
}

void telemetry_task(void *arg)
{
    (void)arg;
    sample_t s;
    TickType_t last_read = xTaskGetTickCount();

    sm_init(&s_sm);
    sm_dispatch(&s_sm, init_sensors() ? SM_EV_INIT_OK : SM_EV_INIT_FAIL, MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);

    for (;;) {
        sys_monitor_heartbeat(HB_TELEMETRY);

        switch (s_sm.state) {
        case SM_IDLE:
            if (update_session_active()) {
                vTaskDelay(pdMS_TO_TICKS(200));       /* host link is busy with an update */
                break;
            }
            if ((xTaskGetTickCount() - last_read) >= pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS)) {
                last_read = xTaskGetTickCount();
                sm_dispatch(&s_sm, SM_EV_TIMER, MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);
                break;
            }
            gps_status_t g;
            gps_get(&g);
            if (power_stop_allowed(g.fix, update_session_active(), boot_control_is_trial())) {
                event_log_write(EVT_STOP_ENTER, 0, 0);
                (void)power_enter_stop();
                last_read = xTaskGetTickCount() - pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS);  /* sample now */
                vTaskDelay(pdMS_TO_TICKS(200));       /* listen window for host traffic after wake */
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;

        case SM_READ:
            sm_dispatch(&s_sm, read_sensors(&s) ? SM_EV_READ_OK : SM_EV_READ_FAIL,
                        MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);
            if (s_sm.state == SM_ERROR) event_log_write(EVT_STATE_ERROR, s.errors, s_sm.consecutive_errors);
            break;

        case SM_TRANSMIT:
            sm_dispatch(&s_sm, transmit(&s) ? SM_EV_TX_DONE : SM_EV_TX_FAIL, MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);
            break;

        case SM_ERROR:
            bsp_led(LED_ERROR_PIN, true);
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (recover()) {
                sm_dispatch(&s_sm, SM_EV_RECOVERED, MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);
                bsp_led(LED_ERROR_PIN, false);
                event_log_write(EVT_RECOVERED, 0, 0);
            } else {
                sm_dispatch(&s_sm, SM_EV_RECOVERY_FAIL, MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);
                if (s_sm.reset_requested) {
                    event_log_write(EVT_CONTROLLED_RESET, s_sm.recovery_attempts, 0);
                    NVIC_SystemReset();
                }
            }
            break;

        case SM_INIT:
        default:
            sm_dispatch(&s_sm, init_sensors() ? SM_EV_INIT_OK : SM_EV_INIT_FAIL, MAX_ERROR_COUNT, MAX_RECOVERY_ATTEMPTS);
            break;
        }
    }
}

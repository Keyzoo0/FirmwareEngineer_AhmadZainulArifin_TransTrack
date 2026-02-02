/**
 ******************************************************************************
 * @file    power_manager.h
 * @brief   Power Management Header for STM32 STOP mode
 * @details Handles RTC, STOP mode entry/exit, and wakeup sources
 ******************************************************************************
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Wakeup Sources */
typedef enum {
    WAKEUP_RTC = 0,           // RTC alarm
    WAKEUP_UART,              // UART RX (GPS data)
    WAKEUP_BUTTON,            // User button
    WAKEUP_TIMEOUT,           // Timeout wakeup
} wakeup_source_t;

/* Power Mode */
typedef enum {
    POWER_MODE_RUN = 0,       // Normal run mode
    POWER_MODE_SLEEP,         // Sleep mode (CPU stopped, peripherals running)
    POWER_MODE_STOP,          // STOP mode (most clocks stopped)
    POWER_MODE_STANDBY,       // STANDBY mode (lowest power)
} power_mode_t;

/* Power Manager Structure */
typedef struct {
    power_mode_t current_mode;
    power_mode_t previous_mode;
    wakeup_source_t last_wakeup_source;
    uint32_t stop_mode_entry_count;
    uint32_t last_wakeup_time;
    bool rtc_initialized;
    bool initialized;
} power_manager_t;

/* RTC Alarm Configuration */
typedef struct {
    uint32_t period_ms;        // Wake period in milliseconds
    bool enabled;
} rtc_alarm_config_t;

/* Function Prototypes */

/**
 * @brief Initialize power manager (RTC, wakeup pins)
 * @param pm Pointer to power manager structure
 * @return 0 on success, negative error code on failure
 */
int power_manager_init(power_manager_t *pm);

/**
 * @brief Configure RTC alarm for periodic wakeup
 * @param pm Pointer to power manager structure
 * @param period_ms Wake period in milliseconds
 * @return 0 on success, negative error code on failure
 */
int power_manager_configure_rtc_wakeup(power_manager_t *pm, uint32_t period_ms);

/**
 * @brief Enter STOP mode
 * @param pm Pointer to power manager structure
 * @return 0 on success (wakeup), negative error code on failure
 */
int power_manager_enter_stop_mode(power_manager_t *pm);

/**
 * @brief Exit STOP mode and restore peripherals
 * @param pm Pointer to power manager structure
 * @return 0 on success, negative error code on failure
 */
int power_manager_exit_stop_mode(power_manager_t *pm);

/**
 * @brief Request entry to STOP mode
 * @param pm Pointer to power manager structure
 * @return 0 if entering STOP mode, -1 if conditions not met
 */
int power_manager_request_stop_mode(power_manager_t *pm);

/**
 * @brief Get last wakeup source
 * @param pm Pointer to power manager structure
 * @return Wakeup source
 */
wakeup_source_t power_manager_get_wakeup_source(const power_manager_t *pm);

/**
 * @brief Check if system should enter STOP mode
 * @param last_activity_time Last activity timestamp
 * @param timeout_ms Timeout in milliseconds
 * @return true if STOP mode should be entered
 */
bool power_manager_should_enter_stop(uint32_t last_activity_time, uint32_t timeout_ms);

/**
 * @brief Enable/disable wakeup pin
 * @param pin_number Pin number (0-15 for most STM32)
 * @param enable true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int power_manager_config_wakeup_pin(uint32_t pin_number, bool enable);

/**
 * @brief Configure wakeup on UART RX
 * @param enable true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int power_manager_config_wakeup_uart(bool enable);

/**
 * @brief Get current power mode name
 * @param mode Power mode
 * @return Mode name string
 */
const char* power_manager_get_mode_name(power_mode_t mode);

/**
 * @brief Initialize system clocks after STOP mode wakeup
 * @return 0 on success, negative error code on failure
 */
int power_manager_restore_clocks(void);

/**
 * @brief Save context before entering STOP mode
 * @param pm Pointer to power manager structure
 * @return 0 on success, negative error code on failure
 */
int power_manager_save_context(power_manager_t *pm);

/**
 * @brief Restore context after exiting STOP mode
 * @param pm Pointer to power manager structure
 * @return 0 on success, negative error code on failure
 */
int power_manager_restore_context(power_manager_t *pm);

/* HAL Callback Hooks */
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H */

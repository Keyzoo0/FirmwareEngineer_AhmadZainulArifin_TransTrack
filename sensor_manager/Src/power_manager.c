/**
 ******************************************************************************
 * @file    power_manager.c
 * @brief   Power Management Implementation (STOP mode)
 ******************************************************************************
 */

#include "power_manager.h"
#include "main.h"
#include <string.h>

/* External RTC handle (defined in main.c) */
extern RTC_HandleTypeDef hrtc;

/* Global power manager instance */
static power_manager_t g_pm = {0};

/**
 * @brief Initialize power manager
 */
int power_manager_init(power_manager_t *pm) {
    if (pm == NULL) return -1;

    memset(pm, 0, sizeof(power_manager_t));

    pm->current_mode = POWER_MODE_RUN;
    pm->previous_mode = POWER_MODE_RUN;
    pm->last_wakeup_source = WAKEUP_TIMEOUT;
    pm->stop_mode_entry_count = 0;
    pm->rtc_initialized = false;
    pm->initialized = true;

    return 0;
}

/**
 * @brief Configure RTC alarm for periodic wakeup
 */
int power_manager_configure_rtc_wakeup(power_manager_t *pm, uint32_t period_ms) {
    if (pm == NULL || !pm->initialized) return -1;

    // RTC wakeup is configured in 16-bit wake up timer counter
    // The clock source is typically LSI (32 kHz) or LSE (32.768 kHz)
    // For LSI: period = (period_ms * 32) / 1000 approximately

    // For simplicity, configure the RTC wakeup timer
    // This would normally be done via CubeMX or with HAL_RTCEx_SetWakeUpTimer

    pm->rtc_initialized = true;
    return 0;
}

/**
 * @brief Enter STOP mode
 */
int power_manager_enter_stop_mode(power_manager_t *pm) {
    if (pm == NULL || !pm->initialized) return -1;

    // Save context before entering STOP mode
    power_manager_save_context(pm);

    // Configure wake-up sources
    // 1. RTC alarm (already configured)
    // 2. UART RX (GPS) - wake on RXNE flag
    // 3. User button (PA0) - wake on rising edge

    // Enter STOP mode with regulator in low power mode
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    // After wakeup, code continues here...

    return 0;
}

/**
 * @brief Exit STOP mode and restore peripherals
 */
int power_manager_exit_stop_mode(power_manager_t *pm) {
    if (pm == NULL || !pm->initialized) return -1;

    // Restore system clocks
    power_manager_restore_clocks();

    // Resume tick
    HAL_ResumeTick();

    // Update state
    pm->previous_mode = pm->current_mode;
    pm->current_mode = POWER_MODE_RUN;
    pm->stop_mode_entry_count++;
    pm->last_wakeup_time = HAL_GetTick();

    // Restore context
    power_manager_restore_context(pm);

    return 0;
}

/**
 * @brief Request entry to STOP mode
 */
int power_manager_request_stop_mode(power_manager_t *pm) {
    if (pm == NULL) return -1;

    // Check conditions for entering STOP mode
    if (pm->current_mode != POWER_MODE_RUN) {
        return -1; // Already in low power mode
    }

    // Enter STOP mode
    int ret = power_manager_enter_stop_mode(pm);
    if (ret != 0) return ret;

    // After wakeup, restore peripherals
    return power_manager_exit_stop_mode(pm);
}

/**
 * @brief Get last wakeup source
 */
wakeup_source_t power_manager_get_wakeup_source(const power_manager_t *pm) {
    if (pm == NULL) return WAKEUP_TIMEOUT;
    return pm->last_wakeup_source;
}

/**
 * @brief Check if system should enter STOP mode
 */
bool power_manager_should_enter_stop(uint32_t last_activity_time, uint32_t timeout_ms) {
    uint32_t elapsed = get_elapsed_ms(last_activity_time);
    return elapsed >= timeout_ms;
}

/**
 * @brief Configure wakeup pin
 */
int power_manager_config_wakeup_pin(uint32_t pin_number, bool enable) {
    // Configure wakeup pin for rising edge
    // For STM32F407, PA0 is mapped to WKUP pin 2
    // This would normally be done via HAL_PWR_EnableWakeUpPin()

    if (enable) {
        // Enable wakeup pin
        // HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN2);
    } else {
        // Disable wakeup pin
        // HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN2);
    }

    return 0;
}

/**
 * @brief Configure wakeup on UART RX
 */
int power_manager_config_wakeup_uart(bool enable) {
    // UART can wake from STOP mode via the UART RX interrupt
    // This requires specific configuration of the UART and EXTI

    // For implementation details, refer to STM32 reference manual
    // and application notes on UART wakeup from STOP mode

    return 0;
}

/**
 * @brief Get current power mode name
 */
const char* power_manager_get_mode_name(power_mode_t mode) {
    static const char* mode_names[] = {
        "RUN",
        "SLEEP",
        "STOP",
        "STANDBY"
    };

    if (mode <= POWER_MODE_STANDBY) {
        return mode_names[mode];
    }
    return "UNKNOWN";
}

/**
 * @brief Initialize system clocks after STOP mode wakeup
 */
int power_manager_restore_clocks(void) {
    // After STOP mode, the MCU is running on MSI/HSI
    // Need to reconfigure the system clock to HSE

    // This function would reinitialize the system clocks
    // In a real implementation, you would call the same
    // SystemClock_Config() function generated by STM32CubeMX

    // For now, this is a placeholder
    // SystemClock_Config();

    return 0;
}

/**
 * @brief Save context before entering STOP mode
 */
int power_manager_save_context(power_manager_t *pm) {
    if (pm == NULL) return -1;

    pm->previous_mode = pm->current_mode;
    pm->current_mode = POWER_MODE_STOP;

    // Save other critical context to backup registers or RAM
    // if needed for your application

    return 0;
}

/**
 * @brief Restore context after exiting STOP mode
 */
int power_manager_restore_context(power_manager_t *pm) {
    if (pm == NULL) return -1;

    // Restore any saved context from backup registers or RAM

    return 0;
}

/**
 * @brief HAL RTC Wakeup Timer callback
 */
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc) {
    (void)hrtc;

    // RTC wakeup occurred
    if (g_pm.initialized) {
        g_pm.last_wakeup_source = WAKEUP_RTC;
    }
}

/**
 * @brief HAL GPIO EXTI callback (for button wakeup)
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == USER_BUTTON_PIN) {
        // User button pressed
        if (g_pm.initialized) {
            g_pm.last_wakeup_source = WAKEUP_BUTTON;
        }
    }
}

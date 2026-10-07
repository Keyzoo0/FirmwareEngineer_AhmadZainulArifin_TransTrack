/**
 * @file  boot_control.c
 * @brief Writes CONFIRMED / UPDATE_STAGED records to the shared boot journal.
 */
#include "boot_control.h"
#include "boot_journal_f4.h"
#include "flash_layout.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t s_mutex;
static volatile bool s_trial;

void boot_control_init(void)
{
    boot_state_t st;
    s_mutex = xSemaphoreCreateMutex();
    s_trial = journal_read(boot_journal_f4(), &st) &&
              st.pending_slot == APP_SLOT && st.trial_active != 0U;
}

bool boot_control_is_trial(void)       { return s_trial; }
uint8_t boot_control_running_slot(void) { return (uint8_t)APP_SLOT; }
uint8_t boot_control_inactive_slot(void){ return (APP_SLOT == SLOT_A) ? SLOT_B : SLOT_A; }

bool boot_control_state(boot_state_t *out)
{
    return journal_read(boot_journal_f4(), out);
}

static int modify(void (*fn)(boot_state_t *st, uint8_t arg), uint8_t arg)
{
    boot_state_t st;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!journal_read(boot_journal_f4(), &st)) {
        st = (boot_state_t){ .active_slot = APP_SLOT, .pending_slot = SLOT_NONE };
    }
    fn(&st, arg);
    int rc = journal_write(boot_journal_f4(), &st);
    xSemaphoreGive(s_mutex);
    return rc;
}

static void do_confirm(boot_state_t *st, uint8_t arg)
{
    (void)arg;
    st->active_slot = APP_SLOT;
    st->pending_slot = SLOT_NONE;
    st->trial_active = 0;
    st->last_event = BOOT_EVT_CONFIRMED;
}

static void do_stage(boot_state_t *st, uint8_t slot)
{
    st->pending_slot = slot;
    st->trial_active = 0;
    st->last_event = BOOT_EVT_UPDATE_STAGED;
}

int boot_control_confirm(void)
{
    if (!s_trial) return 0;
    int rc = modify(do_confirm, 0);
    if (rc == 0) s_trial = false;
    return rc;
}

int boot_control_stage_update(uint8_t slot)
{
    return modify(do_stage, slot);
}

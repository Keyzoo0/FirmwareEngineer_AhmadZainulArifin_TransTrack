/**
 * @file  update_task.c
 * @brief Receives a firmware image over USART1 and writes it to the *inactive* slot.
 *
 * Power-loss safety of the update:
 *  - the running slot is never touched; the bootloader only switches after the new image
 *    has been fully written, verified (CRC-32 + SHA-256) and staged in the journal
 *  - the 512-byte image header is written LAST. A slot whose write was interrupted has no
 *    valid header, so the bootloader can never mistake a half-written image for a good one
 *  - staging is a single 32-byte journal append (atomic: CRC-protected record)
 *  - after the reset the bootloader runs the new image on trial; it must confirm itself
 *    within 10 s or it is rolled back automatically
 */
#include "tasks.h"
#include "app_config.h"
#include "boot_control.h"
#include "bsp.h"
#include "event_log.h"
#include "flash_f4.h"
#include "flash_layout.h"
#include "fw_image.h"
#include "host_link.h"
#include "power_manager.h"
#include "sys_monitor.h"
#include "update_proto.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

typedef struct {
    bool              active;
    uint8_t           slot;
    uint32_t          next_offset;
    fw_image_header_t hdr;
    TickType_t        last_frame;
} session_t;

static up_decoder_t s_dec;          /* ~1 KB: static, not on the task stack */
static session_t    s_sess;
static uint8_t      s_txbuf[64];

bool update_session_active(void)
{
    return s_sess.active;
}

static void reply(uint8_t type, uint8_t seq, up_status_t st, uint32_t arg)
{
    uint8_t p[5] = { (uint8_t)st, (uint8_t)arg, (uint8_t)(arg >> 8), (uint8_t)(arg >> 16), (uint8_t)(arg >> 24) };
    uint16_t n = up_encode(type, seq, p, sizeof(p), s_txbuf);
    (void)host_link_send(s_txbuf, n);
}

static void fail(uint8_t seq, up_status_t st, uint32_t arg)
{
    if (s_sess.active) event_log_write(EVT_UPDATE_FAILED, (uint16_t)st, arg);
    s_sess.active = false;
    bsp_led(LED_ACTIVITY_PIN, false);
    reply(UP_NAK, seq, st, arg);
}

static up_status_t erase_slot(uint8_t slot)
{
    if (flash_f4_unlock() != 0) return UP_E_FLASH;
    up_status_t st = UP_OK;
    for (uint32_t i = 0; i < SLOT_SECTOR_COUNT && st == UP_OK; i++) {
        /* A 128 KB sector erase stalls the CPU for 1-2 s (single bank). Refresh the IWDG
         * between sectors; the monitor task cannot run while the bus is stalled. */
        bsp_iwdg_refresh();
        if (flash_f4_erase_sector(slot_first_sector(slot) + i) != 0) st = UP_E_FLASH;
        bsp_iwdg_refresh();
        sys_monitor_resync();
    }
    flash_f4_lock();
    return st;
}

static void on_start(const up_frame_t *f)
{
    if (f->len != sizeof(fw_image_header_t)) { fail(f->seq, UP_E_SIZE, f->len); return; }
    fw_image_header_t h;
    memcpy(&h, f->payload, sizeof(h));
    if (h.magic != FW_IMAGE_MAGIC) { fail(f->seq, UP_E_HEADER, h.magic); return; }
    uint8_t target = boot_control_inactive_slot();
    if (h.target_slot != target) { fail(f->seq, UP_E_SLOT, h.target_slot); return; }
    if (h.image_size == 0U || h.image_size > SLOT_MAX_IMAGE_SIZE) { fail(f->seq, UP_E_SIZE, h.image_size); return; }

    s_sess = (session_t){ .active = true, .slot = target, .next_offset = 0, .hdr = h,
                          .last_frame = xTaskGetTickCount() };
    bsp_led(LED_ACTIVITY_PIN, true);
    event_log_write(EVT_UPDATE_START, target, fw_image_version(&h));

    up_status_t st = erase_slot(target);
    if (st != UP_OK) { fail(f->seq, st, 0); return; }
    reply(UP_ACK, f->seq, UP_OK, target);
}

static void on_data(const up_frame_t *f)
{
    if (!s_sess.active) { fail(f->seq, UP_E_STATE, 0); return; }
    if (f->len < 8U || ((f->len - 4U) & 3U) != 0U) { fail(f->seq, UP_E_SIZE, f->len); return; }
    uint32_t off;
    memcpy(&off, f->payload, 4);
    uint32_t n = f->len - 4U;

    if (off + n <= s_sess.next_offset) {             /* duplicate (ACK was lost): re-ACK */
        reply(UP_ACK, f->seq, UP_OK, s_sess.next_offset);
        return;
    }
    if (off != s_sess.next_offset || off + n > s_sess.hdr.image_size + 3U) {
        fail(f->seq, UP_E_OFFSET, s_sess.next_offset);
        return;
    }
    uint32_t addr = slot_base(s_sess.slot) + IMAGE_HEADER_SIZE + off;
    int rc = flash_f4_unlock();
    if (rc == 0) rc = flash_f4_program(addr, &f->payload[4], n);
    flash_f4_lock();
    if (rc != 0) { fail(f->seq, UP_E_FLASH, addr); return; }

    s_sess.next_offset += n;
    reply(UP_ACK, f->seq, UP_OK, s_sess.next_offset);
}

static void on_end(const up_frame_t *f)
{
    if (!s_sess.active || s_sess.next_offset < s_sess.hdr.image_size) {
        fail(f->seq, UP_E_STATE, s_sess.next_offset);
        return;
    }
    /* Header last: from here on the slot is self-describing */
    uint32_t base = slot_base(s_sess.slot);
    int rc = flash_f4_unlock();
    if (rc == 0) rc = flash_f4_program(base, &s_sess.hdr, sizeof(s_sess.hdr));
    flash_f4_lock();
    if (rc != 0) { fail(f->seq, UP_E_FLASH, base); return; }

    bsp_iwdg_refresh();
    fw_image_status_t vs = fw_image_verify((const uint8_t *)base, base, SLOT_SIZE, s_sess.slot,
                                           0x20000000UL, 0x20020000UL, true, NULL);
    if (vs != FW_IMAGE_OK) { fail(f->seq, UP_E_VERIFY, vs); return; }

    if (boot_control_stage_update(s_sess.slot) != 0) { fail(f->seq, UP_E_FLASH, 0); return; }
    event_log_write(EVT_UPDATE_STAGED, s_sess.slot, fw_image_version(&s_sess.hdr));
    reply(UP_ACK, f->seq, UP_OK, fw_image_version(&s_sess.hdr));

    vTaskDelay(pdMS_TO_TICKS(100));                  /* let the ACK leave the UART */
    event_log_write(EVT_CONTROLLED_RESET, 0xA0, s_sess.slot);
    NVIC_SystemReset();                              /* bootloader starts the trial boot */
}

static void on_info(const up_frame_t *f)
{
    boot_state_t st;
    uint8_t p[16] = {0};
    p[0] = (uint8_t)APP_SLOT;
    p[1] = FW_VERSION_MAJOR;
    p[2] = FW_VERSION_MINOR;
    p[3] = FW_VERSION_PATCH;
    p[4] = boot_control_is_trial() ? 1U : 0U;
    if (boot_control_state(&st)) {
        p[5] = st.active_slot;
        p[6] = st.pending_slot;
        p[7] = st.last_event;
        memcpy(&p[8], &st.boot_count, 4);
        memcpy(&p[12], &st.rollback_count, 2);
    }
    uint16_t n = up_encode(UP_INFO, f->seq, p, sizeof(p), s_txbuf);
    (void)host_link_send(s_txbuf, n);
}

void update_task(void *arg)
{
    (void)arg;
    uint8_t chunk[64];
    up_decoder_init(&s_dec);

    for (;;) {
        sys_monitor_heartbeat(HB_UPDATE);
        size_t n = host_link_receive(chunk, sizeof(chunk), 1000);

        for (size_t i = 0; i < n; i++) {
            up_dec_result_t r = up_decoder_feed(&s_dec, chunk[i]);
            if (r == UP_DEC_CRC_ERROR) {
                reply(UP_NAK, s_dec.frame.seq, UP_E_CRC, 0);   /* host retransmits */
                continue;
            }
            if (r != UP_DEC_FRAME) continue;
            const up_frame_t *f = &s_dec.frame;
            s_sess.last_frame = xTaskGetTickCount();
            power_note_activity();
            switch (f->type) {
            case UP_START: on_start(f); break;
            case UP_DATA:  on_data(f);  break;
            case UP_END:   on_end(f);   break;
            case UP_INFO:  on_info(f);  break;
            case UP_ABORT:
                s_sess.active = false;
                bsp_led(LED_ACTIVITY_PIN, false);
                reply(UP_ACK, f->seq, UP_OK, 0);
                break;
            default:
                reply(UP_NAK, f->seq, UP_E_STATE, f->type);
                break;
            }
        }

        /* Abandon a session the host stopped talking to (cable pulled, tool crashed) */
        if (s_sess.active && (xTaskGetTickCount() - s_sess.last_frame) > pdMS_TO_TICKS(30000)) {
            event_log_write(EVT_UPDATE_FAILED, UP_E_STATE, s_sess.next_offset);
            s_sess.active = false;
            bsp_led(LED_ACTIVITY_PIN, false);
        }
    }
}

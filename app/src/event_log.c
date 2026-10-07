/**
 * @file  event_log.c
 * @brief Flash-backed event log (sectors 2/3) with per-record CRC.
 */
#include "event_log.h"
#include "crc32.h"
#include "flash_f4.h"
#include "flash_layout.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <stdbool.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    event_rec_t rec;       /* 16 bytes */
    uint32_t    crc;       /* CRC-32 of rec */
    uint8_t     pad[12];   /* keep records 32-byte aligned, leaves room for future fields */
} log_slot_t;

#define SLOT_SIZE_B     ((uint32_t)sizeof(log_slot_t))
#define SLOTS_PER_PAGE  (LOG_PAGE_SIZE / SLOT_SIZE_B)

static const uint32_t page_addr[2]   = { LOG_PAGE_A_ADDR, LOG_PAGE_B_ADDR };
static const uint32_t page_sector[2] = { LOG_PAGE_A_SECTOR, LOG_PAGE_B_SECTOR };

static SemaphoreHandle_t s_mutex;
static uint8_t  s_page;        /* active page */
static uint32_t s_next_idx;    /* next free slot in active page */
static uint32_t s_seq;

static bool slot_erased(const log_slot_t *s)
{
    const uint32_t *w = (const uint32_t *)s;
    for (uint32_t i = 0; i < SLOT_SIZE_B / 4U; i++) {
        if (w[i] != 0xFFFFFFFFUL) return false;
    }
    return true;
}

static bool slot_valid(const log_slot_t *s)
{
    return crc32_compute(&s->rec, sizeof(s->rec)) == s->crc;
}

/* Scan a page: returns highest seq found (0 if none) and first erased index */
static uint32_t scan_page(uint8_t page, uint32_t *first_free)
{
    const log_slot_t *p = (const log_slot_t *)page_addr[page];
    uint32_t max_seq = 0;
    *first_free = SLOTS_PER_PAGE;
    for (uint32_t i = 0; i < SLOTS_PER_PAGE; i++) {
        if (slot_erased(&p[i])) {
            *first_free = i;
            break;
        }
        if (slot_valid(&p[i]) && p[i].rec.seq > max_seq) max_seq = p[i].rec.seq;
    }
    return max_seq;
}

static void erase_page(uint8_t page)
{
    if (flash_f4_unlock() == 0) {
        (void)flash_f4_erase_sector(page_sector[page]);
        flash_f4_lock();
    }
}

void event_log_init(void)
{
    uint32_t free_a, free_b;
    uint32_t seq_a = scan_page(0, &free_a);
    uint32_t seq_b = scan_page(1, &free_b);

    s_page = (seq_b > seq_a) ? 1U : 0U;
    s_next_idx = (s_page == 0U) ? free_a : free_b;
    s_seq = (seq_a > seq_b ? seq_a : seq_b) + 1U;
    s_mutex = xSemaphoreCreateMutex();
}

void event_log_write(event_id_t id, uint16_t arg, uint32_t data)
{
    bool sched = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    if (sched && s_mutex != NULL) xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_next_idx >= SLOTS_PER_PAGE) {
        /* Rotate: erase the older page and continue there. A power loss during the erase
         * leaves the full page intact, so at most the oldest page is ever lost. */
        s_page ^= 1U;
        erase_page(s_page);
        s_next_idx = 0;
    }

    log_slot_t s;
    memset(&s, 0xFF, sizeof(s));
    s.rec.seq = s_seq++;
    s.rec.uptime_s = sched ? (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ) : 0U;
    s.rec.id = (uint16_t)id;
    s.rec.arg = arg;
    s.rec.data = data;
    s.crc = crc32_compute(&s.rec, sizeof(s.rec));

    uint32_t addr = page_addr[s_page] + s_next_idx * SLOT_SIZE_B;
    if (flash_f4_unlock() == 0) {
        /* CRC is written together with the record; a torn write fails the CRC check */
        (void)flash_f4_program(addr, &s, sizeof(s.rec) + sizeof(s.crc));
        flash_f4_lock();
    }
    s_next_idx++;

    if (sched && s_mutex != NULL) xSemaphoreGive(s_mutex);
}

uint32_t event_log_dump(void (*cb)(const event_rec_t *rec, void *ctx), void *ctx)
{
    uint32_t n = 0;
    uint8_t order[2] = { (uint8_t)(s_page ^ 1U), s_page };   /* older page first */
    for (int k = 0; k < 2; k++) {
        const log_slot_t *p = (const log_slot_t *)page_addr[order[k]];
        for (uint32_t i = 0; i < SLOTS_PER_PAGE && !slot_erased(&p[i]); i++) {
            if (slot_valid(&p[i])) {
                cb(&p[i].rec, ctx);
                n++;
            }
        }
    }
    return n;
}

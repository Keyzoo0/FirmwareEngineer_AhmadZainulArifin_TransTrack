/**
 * @file  boot_journal.c
 * @brief Append-only boot-control journal + boot decision logic.
 */
#include "boot_journal.h"
#include "crc32.h"
#include "flash_layout.h"
#include <stddef.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    uint32_t     magic;
    boot_state_t st;           /* 16 bytes */
    uint32_t     reserved[2];
    uint32_t     crc;          /* CRC-32 of bytes 0..27 */
} journal_record_t;

_Static_assert(sizeof(journal_record_t) == JOURNAL_RECORD_SIZE, "journal record must be 32 bytes");

static bool record_is_erased(const uint8_t *p)
{
    for (uint32_t i = 0; i < JOURNAL_RECORD_SIZE; i++) {
        if (p[i] != 0xFFU) return false;
    }
    return true;
}

static bool record_is_valid(const journal_record_t *r)
{
    return r->magic == JOURNAL_RECORD_MAGIC &&
           crc32_compute(r, offsetof(journal_record_t, crc)) == r->crc;
}

bool journal_read(const journal_flash_t *fl, boot_state_t *out)
{
    bool found = false;
    uint32_t best_seq = 0;

    for (uint32_t off = 0; off + JOURNAL_RECORD_SIZE <= fl->size; off += JOURNAL_RECORD_SIZE) {
        const uint8_t *p = fl->base + off;
        if (record_is_erased(p)) {
            break;                       /* records are appended in order: first blank = end */
        }
        journal_record_t r;
        memcpy(&r, p, sizeof(r));
        if (record_is_valid(&r) && (!found || r.st.sequence >= best_seq)) {
            *out = r.st;
            best_seq = r.st.sequence;
            found = true;
        }
    }
    return found;
}

int journal_write(const journal_flash_t *fl, boot_state_t *st)
{
    boot_state_t prev;
    uint32_t next_seq = journal_read(fl, &prev) ? prev.sequence + 1U : 1U;

    /* Find the first erased record slot (skip torn/invalid records, never rewrite them) */
    uint32_t off = 0;
    while (off + JOURNAL_RECORD_SIZE <= fl->size && !record_is_erased(fl->base + off)) {
        off += JOURNAL_RECORD_SIZE;
    }
    if (off + JOURNAL_RECORD_SIZE > fl->size) {
        if (fl->erase() != 0) {
            return -1;
        }
        off = 0;
    }

    st->sequence = next_seq;
    journal_record_t r;
    memset(&r, 0, sizeof(r));
    r.magic = JOURNAL_RECORD_MAGIC;
    r.st = *st;
    r.reserved[0] = 0xFFFFFFFFUL;
    r.reserved[1] = 0xFFFFFFFFUL;
    r.crc = crc32_compute(&r, offsetof(journal_record_t, crc));

    if (fl->program(off, &r, sizeof(r)) != 0) {
        return -2;
    }
    /* Read back: a record only counts once it is verifiably in flash */
    return memcmp(fl->base + off, &r, sizeof(r)) == 0 ? 0 : -3;
}

static uint8_t other_slot(uint8_t s)
{
    return (s == SLOT_A) ? SLOT_B : SLOT_A;
}

boot_decision_t boot_decide(bool have_state, const boot_state_t *st, const slot_info_t slots[2])
{
    boot_decision_t d;
    memset(&d, 0, sizeof(d));

    if (!have_state || (st->active_slot != SLOT_A && st->active_slot != SLOT_B)) {
        /* Journal empty or erased by an interrupted maintenance erase: rebuild from slots */
        memset(&d.new_state, 0, sizeof(d.new_state));
        d.new_state.pending_slot = SLOT_NONE;
        d.new_state.last_event = BOOT_EVT_FACTORY;
        if (slots[SLOT_A].valid && slots[SLOT_B].valid) {
            d.new_state.active_slot = (slots[SLOT_B].version > slots[SLOT_A].version) ? SLOT_B : SLOT_A;
        } else if (slots[SLOT_B].valid) {
            d.new_state.active_slot = SLOT_B;
        } else {
            d.new_state.active_slot = SLOT_A;
        }
        d.write_state = slots[SLOT_A].valid || slots[SLOT_B].valid;
        d.boot_slot = slots[d.new_state.active_slot].valid ? d.new_state.active_slot : SLOT_NONE;
        return d;
    }

    d.new_state = *st;
    uint8_t active = st->active_slot;

    if (st->pending_slot == SLOT_A || st->pending_slot == SLOT_B) {
        uint8_t pending = st->pending_slot;
        if (st->trial_active) {
            /* We already jumped to the trial image and it did not confirm within 10 s
             * (watchdog reset, hard fault reset or power loss) -> roll back to Bank 1. */
            d.new_state.pending_slot = SLOT_NONE;
            d.new_state.trial_active = 0;
            d.new_state.last_event = BOOT_EVT_ROLLED_BACK;
            d.new_state.rollback_count++;
            d.write_state = true;
        } else if (slots[pending].valid) {
            d.new_state.trial_active = 1;
            d.new_state.last_event = BOOT_EVT_TRIAL_STARTED;
            d.write_state = true;
            d.boot_slot = pending;
            return d;
        } else {
            d.new_state.pending_slot = SLOT_NONE;
            d.new_state.last_event = BOOT_EVT_PENDING_INVALID;
            d.write_state = true;
        }
    }

    if (slots[active].valid) {
        d.boot_slot = active;
    } else if (slots[other_slot(active)].valid) {
        d.new_state.active_slot = other_slot(active);
        d.new_state.last_event = BOOT_EVT_FALLBACK;
        d.write_state = true;
        d.boot_slot = other_slot(active);
    } else {
        d.boot_slot = SLOT_NONE;
    }
    return d;
}

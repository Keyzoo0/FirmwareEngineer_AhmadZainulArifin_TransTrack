/**
 * @file  boot_journal.h
 * @brief Power-loss-safe boot control state stored as an append-only journal in flash
 *        sector 4, plus the pure boot-decision logic used by the bootloader.
 *
 * Why a journal and not a single "config word":
 *  - A record is only valid if its CRC matches, so a write interrupted by power loss is
 *    simply ignored and the previous record stays in effect.
 *  - The sector is erased only when full (~2000 updates). If power fails during that
 *    erase the journal is empty and boot_decide() rebuilds the state from the slots
 *    themselves (highest valid version wins), so the device always boots.
 */
#ifndef BOOT_JOURNAL_H
#define BOOT_JOURNAL_H

#include <stdint.h>
#include <stdbool.h>

#define JOURNAL_RECORD_MAGIC 0xB007CAFEUL
#define JOURNAL_RECORD_SIZE  32U

typedef enum {
    BOOT_EVT_NONE = 0,
    BOOT_EVT_FACTORY,          /* journal rebuilt from slot contents */
    BOOT_EVT_UPDATE_STAGED,    /* app wrote a verified image to the inactive slot */
    BOOT_EVT_TRIAL_STARTED,    /* bootloader jumped to the pending slot */
    BOOT_EVT_CONFIRMED,        /* new image ran 10 s and marked itself good */
    BOOT_EVT_ROLLED_BACK,      /* trial image reset before confirming -> back to old slot */
    BOOT_EVT_PENDING_INVALID,  /* staged image failed CRC/SHA in the bootloader */
    BOOT_EVT_FALLBACK,         /* active slot corrupt, booted the other valid slot */
} boot_event_t;

typedef struct {
    uint32_t sequence;
    uint8_t  active_slot;      /* SLOT_A / SLOT_B */
    uint8_t  pending_slot;     /* SLOT_NONE or slot waiting for its trial boot */
    uint8_t  trial_active;     /* 1 = bootloader already started the trial of pending_slot */
    uint8_t  last_event;       /* boot_event_t */
    uint16_t rollback_count;
    uint16_t reserved;
    uint32_t boot_count;
} boot_state_t;

/** Flash access used by the journal (implemented with HAL on target, RAM-backed in tests). */
typedef struct {
    const uint8_t *base;       /* memory-mapped start of the journal sector */
    uint32_t size;             /* sector size in bytes */
    int (*erase)(void);        /* erase the whole journal sector, 0 on success */
    int (*program)(uint32_t offset, const void *data, uint32_t len); /* word-aligned writes */
} journal_flash_t;

/** Latest valid record, false if the journal holds none. */
bool journal_read(const journal_flash_t *fl, boot_state_t *out);

/** Append a record (sequence is assigned automatically). Returns 0 on success. */
int journal_write(const journal_flash_t *fl, boot_state_t *st);

typedef struct {
    bool    valid;
    uint32_t version;          /* fw_image_version(), only meaningful if valid */
} slot_info_t;

typedef struct {
    uint8_t      boot_slot;    /* SLOT_A / SLOT_B / SLOT_NONE (stay in recovery) */
    bool         write_state;  /* new_state must be persisted before jumping */
    boot_state_t new_state;
} boot_decision_t;

/** Pure decision function (no I/O) so the rollback rules are unit-tested on the host. */
boot_decision_t boot_decide(bool have_state, const boot_state_t *st, const slot_info_t slots[2]);

#endif /* BOOT_JOURNAL_H */

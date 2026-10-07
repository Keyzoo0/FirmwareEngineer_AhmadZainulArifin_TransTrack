/**
 * @file    flash_layout.h
 * @brief   STM32F407VG (1 MB, single bank) flash partitioning shared by the
 *          bootloader, the application and the host tools.
 *
 *  Sector | Address     | Size  | Use
 *  -------+-------------+-------+------------------------------------------
 *  0..1   | 0x0800_0000 | 2x16K | Bootloader (32 KB)
 *  2..3   | 0x0800_8000 | 2x16K | Persistent event log (ping-pong pages)
 *  4      | 0x0801_0000 | 64K   | Boot-control journal (append-only)
 *  5..7   | 0x0802_0000 | 3x128K| Slot A  ("Bank 1", known-good image)
 *  8..10  | 0x0808_0000 | 3x128K| Slot B  ("Bank 2", update / trial image)
 *  11     | 0x080E_0000 | 128K  | Reserved (factory data / future use)
 *
 * The F407 has a single physical flash bank, so "Bank 1 / Bank 2" from the
 * assessment are implemented as two equally sized slots.  Each slot starts with
 * a 512-byte image header followed by the vector table (VTOR requires 512-byte
 * alignment on Cortex-M4 with 98 vectors).
 */
#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#define FLASH_BASE_ADDR          0x08000000UL

#define BOOTLOADER_ADDR          0x08000000UL
#define BOOTLOADER_SIZE          (32UL * 1024UL)

#define LOG_PAGE_A_ADDR          0x08008000UL   /* sector 2 */
#define LOG_PAGE_B_ADDR          0x0800C000UL   /* sector 3 */
#define LOG_PAGE_SIZE            (16UL * 1024UL)
#define LOG_PAGE_A_SECTOR        2U
#define LOG_PAGE_B_SECTOR        3U

#define JOURNAL_ADDR             0x08010000UL   /* sector 4 */
#define JOURNAL_SIZE             (64UL * 1024UL)
#define JOURNAL_SECTOR           4U

#define SLOT_A_ADDR              0x08020000UL   /* sectors 5..7 */
#define SLOT_B_ADDR              0x08080000UL   /* sectors 8..10 */
#define SLOT_SIZE                (384UL * 1024UL)
#define SLOT_A_FIRST_SECTOR      5U
#define SLOT_B_FIRST_SECTOR      8U
#define SLOT_SECTOR_COUNT        3U

#define IMAGE_HEADER_SIZE        0x200UL         /* 512 bytes, keeps VTOR aligned */
#define SLOT_MAX_IMAGE_SIZE      (SLOT_SIZE - IMAGE_HEADER_SIZE)

#define SLOT_A                   0U
#define SLOT_B                   1U
#define SLOT_NONE                0xFFU

static inline unsigned long slot_base(unsigned slot)
{
    return (slot == SLOT_B) ? SLOT_B_ADDR : SLOT_A_ADDR;
}

static inline unsigned slot_first_sector(unsigned slot)
{
    return (slot == SLOT_B) ? SLOT_B_FIRST_SECTOR : SLOT_A_FIRST_SECTOR;
}

#endif /* FLASH_LAYOUT_H */

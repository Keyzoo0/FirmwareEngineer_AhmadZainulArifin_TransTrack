/**
 * Host unit tests for the hardware-independent modules:
 * CRC-32 / SHA-256, image header verification, boot journal + rollback decisions,
 * NMEA parser, 4-20 mA conversion, BME280 compensation, state machine, update protocol.
 */
#include "unity_lite.h"
#include "boot_journal.h"
#include "bme280.h"
#include "crc32.h"
#include "flash_layout.h"
#include "fuel_calc.h"
#include "fw_image.h"
#include "nmea.h"
#include "sensor_sm.h"
#include "sha256.h"
#include "update_proto.h"
#include <string.h>

/* ------------------------------------------------------------ crypto */
static void test_crc32_sha256_vectors(void)
{
    CHECK_EQ(crc32_compute("123456789", 9), 0xCBF43926UL);          /* standard check value */
    uint32_t c = crc32_update(0, "1234", 4);
    CHECK_EQ(crc32_update(c, "56789", 5), 0xCBF43926UL);            /* incremental == one-shot */

    uint8_t d[32];
    sha256_compute("abc", 3, d);
    static const uint8_t abc[4] = { 0xba, 0x78, 0x16, 0xbf };
    CHECK(memcmp(d, abc, 4) == 0);
    CHECK_EQ(d[31], 0xad);
    sha256_compute("", 0, d);
    CHECK_EQ(d[0], 0xe3);
    CHECK_EQ(d[31], 0x55);
}

/* ------------------------------------------------------------ image header */
static uint8_t g_slot[64 * 1024];

static void make_image(uint8_t slot, uint32_t size, uint8_t major)
{
    uint32_t base = slot_base(slot);
    memset(g_slot, 0xFF, sizeof(g_slot));
    uint8_t *img = g_slot + IMAGE_HEADER_SIZE;
    for (uint32_t i = 0; i < size; i++) img[i] = (uint8_t)(i * 7U);
    uint32_t sp = 0x20020000UL, reset = (base + IMAGE_HEADER_SIZE + 0x189U) | 1U;
    memcpy(img, &sp, 4);
    memcpy(img + 4, &reset, 4);

    fw_image_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = FW_IMAGE_MAGIC;
    h.header_version = FW_IMAGE_HEADER_VERSION;
    h.target_slot = slot;
    h.ver_major = major;
    h.image_size = size;
    h.image_crc32 = crc32_compute(img, size);
    sha256_compute(img, size, h.sha256);
    h.header_crc32 = crc32_compute(&h, offsetof(fw_image_header_t, header_crc32));
    memcpy(g_slot, &h, sizeof(h));
}

static fw_image_status_t verify(uint8_t slot)
{
    return fw_image_verify(g_slot, slot_base(slot), sizeof(g_slot), slot, 0x20000000UL, 0x20020000UL, true, NULL);
}

static void test_fw_image_verify(void)
{
    make_image(SLOT_B, 4096, 1);
    CHECK_EQ(verify(SLOT_B), FW_IMAGE_OK);
    CHECK_EQ(verify(SLOT_A), FW_IMAGE_ERR_SLOT);                 /* linked for B, placed in A */

    g_slot[IMAGE_HEADER_SIZE + 100] ^= 1;                        /* single bit flip in code */
    CHECK_EQ(verify(SLOT_B), FW_IMAGE_ERR_CRC);

    make_image(SLOT_B, 4096, 1);
    g_slot[8] ^= 1;                                              /* header corrupted */
    CHECK_EQ(verify(SLOT_B), FW_IMAGE_ERR_HEADER_CRC);

    make_image(SLOT_B, 4096, 1);
    memset(g_slot, 0xFF, IMAGE_HEADER_SIZE);                     /* interrupted update: no header */
    CHECK_EQ(verify(SLOT_B), FW_IMAGE_ERR_MAGIC);
}

/* Image produced by scripts/prepare-firmware.py must be accepted by the C verifier */
static void test_python_image_compat(void)
{
    FILE *f = fopen("python_image.img", "rb");
    if (f == NULL) {
        printf("  (skipped: python_image.img not generated)\n");
        return;
    }
    memset(g_slot, 0xFF, sizeof(g_slot));
    size_t n = fread(g_slot, 1, sizeof(g_slot), f);
    fclose(f);
    CHECK(n > IMAGE_HEADER_SIZE);
    fw_image_header_t h;
    CHECK_EQ(fw_image_verify(g_slot, slot_base(SLOT_B), sizeof(g_slot), SLOT_B,
                             0x20000000UL, 0x20020000UL, true, &h), FW_IMAGE_OK);
    CHECK_EQ(h.ver_major, 1);
    CHECK_EQ(h.ver_minor, 2);
    CHECK_EQ(h.ver_patch, 3);
}

/* ------------------------------------------------------------ boot journal (RAM flash) */
static uint8_t g_jflash[1024];
static int g_erase_count;
static int g_fail_program_after = -1;   /* simulate power loss: write only N bytes */

static int ram_erase(void)
{
    memset(g_jflash, 0xFF, sizeof(g_jflash));
    g_erase_count++;
    return 0;
}

static int ram_program(uint32_t off, const void *data, uint32_t len)
{
    const uint8_t *p = data;
    for (uint32_t i = 0; i < len; i++) {
        if (g_fail_program_after >= 0 && (int)i >= g_fail_program_after) return -1;
        g_jflash[off + i] &= p[i];      /* NOR flash can only clear bits */
    }
    return 0;
}

static const journal_flash_t g_jf = { g_jflash, sizeof(g_jflash), ram_erase, ram_program };

static void test_journal_append_and_wrap(void)
{
    boot_state_t st = {0}, rd;
    ram_erase();
    g_erase_count = 0;
    CHECK(!journal_read(&g_jf, &rd));

    for (int i = 0; i < 100; i++) {          /* 1024/32 = 32 records per "sector" -> wraps */
        st.active_slot = SLOT_A;
        st.pending_slot = SLOT_NONE;
        st.boot_count = (uint32_t)i;
        CHECK_EQ(journal_write(&g_jf, &st), 0);
    }
    CHECK(journal_read(&g_jf, &rd));
    CHECK_EQ(rd.boot_count, 99);
    CHECK_EQ(rd.sequence, 100);
    CHECK(g_erase_count >= 3);
}

static void test_journal_torn_write(void)
{
    boot_state_t st = { .active_slot = SLOT_A, .pending_slot = SLOT_NONE, .boot_count = 7 }, rd;
    ram_erase();
    CHECK_EQ(journal_write(&g_jf, &st), 0);

    st.active_slot = SLOT_B;
    st.boot_count = 8;
    g_fail_program_after = 13;               /* power lost in the middle of the record */
    CHECK(journal_write(&g_jf, &st) != 0);
    g_fail_program_after = -1;

    CHECK(journal_read(&g_jf, &rd));         /* previous record still in effect */
    CHECK_EQ(rd.active_slot, SLOT_A);
    CHECK_EQ(rd.boot_count, 7);

    st.boot_count = 9;                       /* next write skips the torn slot */
    CHECK_EQ(journal_write(&g_jf, &st), 0);
    CHECK(journal_read(&g_jf, &rd));
    CHECK_EQ(rd.boot_count, 9);
}

/* ------------------------------------------------------------ boot decisions */
static void test_boot_update_confirm_and_rollback(void)
{
    slot_info_t slots[2] = { { true, 0x01010000 }, { true, 0x01020000 } };
    boot_state_t st = { .active_slot = SLOT_A, .pending_slot = SLOT_B };

    /* 1st boot after staging: trial of B */
    boot_decision_t d = boot_decide(true, &st, slots);
    CHECK_EQ(d.boot_slot, SLOT_B);
    CHECK(d.write_state);
    CHECK_EQ(d.new_state.trial_active, 1);

    /* B crashed / watchdog before confirming -> back to A, counted */
    st = d.new_state;
    d = boot_decide(true, &st, slots);
    CHECK_EQ(d.boot_slot, SLOT_A);
    CHECK_EQ(d.new_state.pending_slot, SLOT_NONE);
    CHECK_EQ(d.new_state.last_event, BOOT_EVT_ROLLED_BACK);
    CHECK_EQ(d.new_state.rollback_count, 1);

    /* steady state afterwards: A, no further writes */
    st = d.new_state;
    d = boot_decide(true, &st, slots);
    CHECK_EQ(d.boot_slot, SLOT_A);
    CHECK(!d.write_state);

    /* app confirmed B (active=B, pending none) -> B is booted from now on */
    st.active_slot = SLOT_B;
    d = boot_decide(true, &st, slots);
    CHECK_EQ(d.boot_slot, SLOT_B);
}

static void test_boot_corrupt_cases(void)
{
    slot_info_t slots[2] = { { true, 0x01010000 }, { false, 0 } };
    boot_state_t st = { .active_slot = SLOT_A, .pending_slot = SLOT_B };

    /* staged image corrupted before the bootloader saw it */
    boot_decision_t d = boot_decide(true, &st, slots);
    CHECK_EQ(d.boot_slot, SLOT_A);
    CHECK_EQ(d.new_state.last_event, BOOT_EVT_PENDING_INVALID);

    /* active slot corrupted (e.g. flash wear) -> fall back to the other slot */
    slot_info_t s2[2] = { { false, 0 }, { true, 0x01000000 } };
    st = (boot_state_t){ .active_slot = SLOT_A, .pending_slot = SLOT_NONE };
    d = boot_decide(true, &st, s2);
    CHECK_EQ(d.boot_slot, SLOT_B);
    CHECK_EQ(d.new_state.last_event, BOOT_EVT_FALLBACK);

    /* nothing bootable -> stay in bootloader */
    slot_info_t none[2] = { { false, 0 }, { false, 0 } };
    d = boot_decide(true, &st, none);
    CHECK_EQ(d.boot_slot, SLOT_NONE);

    /* journal lost (power cut during sector erase) -> newest valid slot */
    slot_info_t both[2] = { { true, 0x01000000 }, { true, 0x01010000 } };
    d = boot_decide(false, NULL, both);
    CHECK_EQ(d.boot_slot, SLOT_B);
    CHECK_EQ(d.new_state.last_event, BOOT_EVT_FACTORY);
}

/* ------------------------------------------------------------ NMEA */
static void feed_str(nmea_parser_t *p, const char *s, nmea_rmc_t *out, nmea_result_t *last)
{
    for (; *s; s++) {
        nmea_result_t r = nmea_feed(p, *s, out);
        if (r != NMEA_NONE) *last = r;
    }
}

static void test_nmea(void)
{
    nmea_parser_t p;
    nmea_rmc_t r;
    nmea_result_t last = NMEA_NONE;
    nmea_init(&p);

    feed_str(&p, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", &r, &last);
    CHECK_EQ(last, NMEA_RMC);
    CHECK(r.valid);
    CHECK_EQ(r.lat_e7, 481173000);           /* 48 deg 07.038' = 48.1173 */
    CHECK_EQ(r.lon_e7, 115166666);           /* 11 deg 31.000' = 11.516666 */
    CHECK_EQ(r.speed_knots_x100, 2240);
    CHECK_EQ(r.hh, 12);
    CHECK_EQ(r.year, 94);

    /* corrupted byte -> checksum error, data unchanged */
    last = NMEA_NONE;
    feed_str(&p, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B\r\n", &r, &last);
    CHECK_EQ(last, NMEA_BAD_CHECKSUM);
    CHECK_EQ(p.checksum_errors, 1);

    /* missing checksum is rejected, not accepted */
    CHECK_EQ(nmea_parse_sentence("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", &r),
             NMEA_MALFORMED);

    /* GN talker, southern/western hemisphere */
    CHECK_EQ(nmea_parse_sentence("$GNRMC,083559.00,A,0758.15520,S,11237.51630,E,0.004,,081026,,,A*76", &r),
             NMEA_RMC);
    CHECK(r.valid);
    CHECK(r.lat_e7 < 0);
    CHECK_NEAR(r.lat_e7, -79692533, 1);

    /* no fix: valid sentence, empty fields */
    CHECK_EQ(nmea_parse_sentence("$GPRMC,,V,,,,,,,,,,N*53", &r), NMEA_RMC);
    CHECK(!r.valid);

    /* other sentence types are recognised but not decoded */
    CHECK_EQ(nmea_parse_sentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", &r),
             NMEA_OTHER);

    /* garbage without CRLF does not overflow */
    nmea_init(&p);
    last = NMEA_NONE;
    for (int i = 0; i < 300; i++) {
        nmea_result_t x = nmea_feed(&p, i == 0 ? '$' : 'X', &r);
        if (x != NMEA_NONE) last = x;
    }
    CHECK_EQ(last, NMEA_MALFORMED);
    CHECK_EQ(p.overflow_errors, 1);
}

/* ------------------------------------------------------------ 4-20 mA */
static uint32_t raw_for_ma(double ma)
{
    double pin_v = ma / 1000.0 * 250.0 * 0.5;
    return (uint32_t)(pin_v / 3.3 * 4095.0 + 0.5);
}

static void test_fuel_calc(void)
{
    const uint32_t ratio = 500000, shunt = 250000;
    const uint16_t cal = 1500;
    fuel_reading_t r = fuel_calc(raw_for_ma(12.0), cal, cal, ratio, shunt);
    CHECK_EQ(r.status, FUEL_OK);
    CHECK_NEAR(r.loop_ua, 12000, 30);
    CHECK_NEAR(r.level_x10, 500, 3);

    r = fuel_calc(raw_for_ma(4.0), cal, cal, ratio, shunt);
    CHECK_NEAR(r.level_x10, 0, 3);
    r = fuel_calc(raw_for_ma(20.0), cal, cal, ratio, shunt);
    CHECK_NEAR(r.level_x10, 1000, 3);

    CHECK_EQ(fuel_calc(0, cal, cal, ratio, shunt).status, FUEL_FAULT_LOW);              /* broken wire */
    CHECK_EQ(fuel_calc(raw_for_ma(23.0), cal, cal, ratio, shunt).status, FUEL_FAULT_HIGH);
    CHECK_EQ(fuel_calc(raw_for_ma(3.7), cal, cal, ratio, shunt).status, FUEL_UNDER_RANGE);

    /* VDDA = 3.0 V instead of 3.3 V: VREFINT reads higher, result must not shift */
    uint32_t raw_vref_3v0 = cal * 33U / 30U;
    uint32_t raw_ch_3v0 = (uint32_t)(12.0 / 1000.0 * 250.0 * 0.5 / 3.0 * 4095.0 + 0.5);
    r = fuel_calc(raw_ch_3v0, raw_vref_3v0, cal, ratio, shunt);
    CHECK_NEAR(r.vdda_mv, 3000, 3);
    CHECK_NEAR(r.loop_ua, 12000, 40);
}

/* ------------------------------------------------------------ BME280 compensation */
static void test_bme280_compensation(void)
{
    /* Reference trimming values and raw readings from the Bosch BMP280/BME280 datasheet example */
    bme280_calib_t k;
    memset(&k, 0, sizeof(k));
    k.dig_T1 = 27504; k.dig_T2 = 26435; k.dig_T3 = -1000;
    k.dig_P1 = 36477; k.dig_P2 = -10685; k.dig_P3 = 3024; k.dig_P4 = 2855; k.dig_P5 = 140;
    k.dig_P6 = -7; k.dig_P7 = 15500; k.dig_P8 = -14600; k.dig_P9 = 6000;

    int32_t t_fine;
    CHECK_EQ(bme280_comp_temp(&k, 519888, &t_fine), 2508);       /* 25.08 C */
    CHECK_EQ(t_fine, 128422);
    CHECK_EQ(bme280_comp_press(&k, 415148, t_fine), 100653);     /* 1006.53 hPa */

    /* dig_H4/H5 share register 0xE5: check nibble unpacking incl. sign */
    uint8_t c1[26] = {0}, c2[7] = { 0x6A, 0x01, 0x00, 0x13, 0x25, 0x03, 0x1E };
    bme280_parse_calib(c1, c2, &k);
    CHECK_EQ(k.dig_H2, 362);
    CHECK_EQ(k.dig_H4, (0x13 << 4) | 0x5);
    CHECK_EQ(k.dig_H5, (0x03 << 4) | 0x2);
    CHECK_EQ(k.dig_H6, 30);
    c2[3] = 0xFF;                                               /* negative dig_H4 */
    bme280_parse_calib(c1, c2, &k);
    CHECK(k.dig_H4 < 0);
}

/* ------------------------------------------------------------ state machine */
static void test_state_machine(void)
{
    sm_ctx_t sm;
    sm_init(&sm);
    CHECK_EQ(sm_dispatch(&sm, SM_EV_TIMER, 5, 5), SM_INIT);       /* ignored in INIT */
    CHECK_EQ(sm_dispatch(&sm, SM_EV_INIT_OK, 5, 5), SM_IDLE);
    CHECK_EQ(sm_dispatch(&sm, SM_EV_TIMER, 5, 5), SM_READ);
    CHECK_EQ(sm_dispatch(&sm, SM_EV_READ_OK, 5, 5), SM_TRANSMIT);
    CHECK_EQ(sm_dispatch(&sm, SM_EV_TX_DONE, 5, 5), SM_IDLE);

    /* 4 failures stay in IDLE, the 5th consecutive one enters ERROR */
    for (int i = 0; i < 4; i++) {
        sm_dispatch(&sm, SM_EV_TIMER, 5, 5);
        CHECK_EQ(sm_dispatch(&sm, SM_EV_READ_FAIL, 5, 5), SM_IDLE);
    }
    sm_dispatch(&sm, SM_EV_TIMER, 5, 5);
    CHECK_EQ(sm_dispatch(&sm, SM_EV_READ_FAIL, 5, 5), SM_ERROR);

    /* recovery fails 4x -> still ERROR, 5th requests reset */
    for (int i = 0; i < 4; i++) sm_dispatch(&sm, SM_EV_RECOVERY_FAIL, 5, 5);
    CHECK(!sm.reset_requested);
    sm_dispatch(&sm, SM_EV_RECOVERY_FAIL, 5, 5);
    CHECK(sm.reset_requested);

    sm_init(&sm);
    sm_dispatch(&sm, SM_EV_INIT_FAIL, 5, 5);
    CHECK_EQ(sm_dispatch(&sm, SM_EV_RECOVERED, 5, 5), SM_IDLE);
    CHECK_EQ(sm.consecutive_errors, 0);

    /* a success in between resets the consecutive error counter */
    sm_dispatch(&sm, SM_EV_TIMER, 5, 5);
    sm_dispatch(&sm, SM_EV_READ_FAIL, 5, 5);
    sm_dispatch(&sm, SM_EV_TIMER, 5, 5);
    sm_dispatch(&sm, SM_EV_READ_OK, 5, 5);
    sm_dispatch(&sm, SM_EV_TX_DONE, 5, 5);
    CHECK_EQ(sm.consecutive_errors, 0);
}

/* ------------------------------------------------------------ update protocol */
static void test_update_proto(void)
{
    uint8_t buf[UP_MAX_PAYLOAD + UP_OVERHEAD], payload[300];
    for (int i = 0; i < 300; i++) payload[i] = (uint8_t)i;
    uint16_t n = up_encode(UP_DATA, 42, payload, sizeof(payload), buf);
    CHECK_EQ(n, 310);

    up_decoder_t d;
    up_decoder_init(&d);
    /* leading noise, including a false SOF, must be skipped */
    const uint8_t noise[] = { 0x00, 0xA5, 0x11, '{', 'x', '}' };
    for (size_t i = 0; i < sizeof(noise); i++) CHECK_EQ(up_decoder_feed(&d, noise[i]), UP_DEC_NONE);

    up_dec_result_t r = UP_DEC_NONE;
    for (uint16_t i = 0; i < n; i++) r = up_decoder_feed(&d, buf[i]);
    CHECK_EQ(r, UP_DEC_FRAME);
    CHECK_EQ(d.frame.type, UP_DATA);
    CHECK_EQ(d.frame.seq, 42);
    CHECK_EQ(d.frame.len, 300);
    CHECK(memcmp(d.frame.payload, payload, 300) == 0);

    buf[100] ^= 0x40;                       /* corrupted in transit */
    for (uint16_t i = 0; i < n; i++) r = up_decoder_feed(&d, buf[i]);
    CHECK_EQ(r, UP_DEC_CRC_ERROR);
    CHECK_EQ(d.crc_errors, 1);

    /* zero-length frame */
    n = up_encode(UP_END, 1, NULL, 0, buf);
    for (uint16_t i = 0; i < n; i++) r = up_decoder_feed(&d, buf[i]);
    CHECK_EQ(r, UP_DEC_FRAME);
    CHECK_EQ(d.frame.type, UP_END);
}

int main(void)
{
    RUN(test_crc32_sha256_vectors);
    RUN(test_fw_image_verify);
    RUN(test_python_image_compat);
    RUN(test_journal_append_and_wrap);
    RUN(test_journal_torn_write);
    RUN(test_boot_update_confirm_and_rollback);
    RUN(test_boot_corrupt_cases);
    RUN(test_nmea);
    RUN(test_fuel_calc);
    RUN(test_bme280_compensation);
    RUN(test_state_machine);
    RUN(test_update_proto);
    return REPORT();
}

/**
 * @file  nmea.c
 * @brief NMEA 0183 RMC parser with mandatory checksum validation.
 *
 * Changes compared with the February version:
 *  - accepts any talker ID ($GPRMC, $GNRMC, ...): multi-GNSS receivers send $GNRMC, which the
 *    old "$GPRMC" string compare silently ignored (no position at all)
 *  - checksum is still mandatory; the field count and every field's syntax are now validated too
 *  - integer fixed-point (1e-7 degree) instead of double math, so it is cheap enough for an ISR
 *  - bounded work per byte, '$' always resynchronises, overlong lines are counted and dropped
 */
#include "nmea.h"
#include <string.h>

void nmea_init(nmea_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Split into fields in place-free manner: returns pointers into s, fields end at ',' or '*' */
#define MAX_FIELDS 20
static int split_fields(const char *s, const char *fields[MAX_FIELDS], uint8_t lens[MAX_FIELDS])
{
    int n = 0;
    const char *start = s;
    for (const char *q = s; ; q++) {
        if (*q == ',' || *q == '*' || *q == '\0') {
            if (n < MAX_FIELDS) {
                fields[n] = start;
                lens[n] = (uint8_t)(q - start);
                n++;
            }
            if (*q != ',') break;
            start = q + 1;
        }
    }
    return n;
}

/* Parse unsigned decimal "123.45" into value scaled by 10^decimals. Returns false if not a number. */
static bool parse_fixed(const char *f, uint8_t len, uint8_t decimals, uint32_t *out)
{
    uint32_t v = 0;
    int frac = -1;
    if (len == 0) return false;
    for (uint8_t i = 0; i < len; i++) {
        char c = f[i];
        if (c == '.') {
            if (frac >= 0) return false;
            frac = 0;
            continue;
        }
        if (c < '0' || c > '9') return false;
        if (frac >= 0) {
            if (frac >= decimals) continue;     /* drop extra precision */
            frac++;
        }
        v = v * 10U + (uint32_t)(c - '0');
    }
    for (int k = (frac < 0 ? 0 : frac); k < decimals; k++) {
        v *= 10U;
    }
    *out = v;
    return true;
}

/* ddmm.mmmm / dddmm.mmmm -> degrees * 1e7 */
static bool parse_coord(const char *f, uint8_t len, uint8_t deg_digits, char hemi, int32_t *out)
{
    if (len < deg_digits + 2U) return false;
    uint32_t deg = 0;
    for (uint8_t i = 0; i < deg_digits; i++) {
        if (f[i] < '0' || f[i] > '9') return false;
        deg = deg * 10U + (uint32_t)(f[i] - '0');
    }
    uint32_t min_e5;                     /* minutes * 1e5 */
    if (!parse_fixed(f + deg_digits, (uint8_t)(len - deg_digits), 5, &min_e5) || min_e5 >= 6000000U) {
        return false;
    }
    /* minutes/60 * 1e7 = min_e5 * 100 / 60 */
    int32_t v = (int32_t)(deg * 10000000U + (min_e5 * 10U) / 6U);
    if (hemi == 'S' || hemi == 'W') v = -v;
    else if (hemi != 'N' && hemi != 'E') return false;
    *out = v;
    return true;
}

static bool two_digits(const char *f, uint8_t *out)
{
    if (f[0] < '0' || f[0] > '9' || f[1] < '0' || f[1] > '9') return false;
    *out = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
    return true;
}

nmea_result_t nmea_parse_sentence(const char *s, nmea_rmc_t *out)
{
    size_t n = strlen(s);
    if (n < 9 || s[0] != '$') return NMEA_MALFORMED;

    const char *star = strchr(s, '*');
    if (star == NULL || (size_t)(star - s) + 3U != n) return NMEA_MALFORMED;

    uint8_t sum = 0;
    for (const char *q = s + 1; q < star; q++) sum ^= (uint8_t)*q;
    int hi = hexval(star[1]), lo = hexval(star[2]);
    if (hi < 0 || lo < 0) return NMEA_MALFORMED;
    if (sum != (uint8_t)((hi << 4) | lo)) return NMEA_BAD_CHECKSUM;

    if (memcmp(s + 3, "RMC,", 4) != 0) return NMEA_OTHER;

    const char *f[MAX_FIELDS];
    uint8_t l[MAX_FIELDS];
    int nf = split_fields(s + 1, f, l);
    /* $--RMC,time,status,lat,N,lon,E,spd,cog,date,... */
    if (nf < 10) return NMEA_MALFORMED;

    nmea_rmc_t r;
    memset(&r, 0, sizeof(r));
    if (l[1] >= 6 && !(two_digits(f[1], &r.hh) && two_digits(f[1] + 2, &r.mm) && two_digits(f[1] + 4, &r.ss))) {
        return NMEA_MALFORMED;
    }
    if (l[9] == 6 && !(two_digits(f[9], &r.day) && two_digits(f[9] + 2, &r.month) && two_digits(f[9] + 4, &r.year))) {
        return NMEA_MALFORMED;
    }
    r.valid = (l[2] == 1 && f[2][0] == 'A');
    if (r.valid) {
        if (l[4] != 1 || l[6] != 1 ||
            !parse_coord(f[3], l[3], 2, f[4][0], &r.lat_e7) ||
            !parse_coord(f[5], l[5], 3, f[6][0], &r.lon_e7)) {
            return NMEA_MALFORMED;
        }
        if (!parse_fixed(f[7], l[7], 2, &r.speed_knots_x100)) r.speed_knots_x100 = 0;
        if (!parse_fixed(f[8], l[8], 2, &r.course_deg_x100)) r.course_deg_x100 = 0;
    }
    *out = r;
    return NMEA_RMC;
}

nmea_result_t nmea_feed(nmea_parser_t *p, char c, nmea_rmc_t *out)
{
    if (c == '$') {                      /* start of sentence always resynchronises */
        p->buf[0] = '$';
        p->len = 1;
        p->in_sentence = true;
        return NMEA_NONE;
    }
    if (!p->in_sentence) return NMEA_NONE;

    if (c == '\r' || c == '\n') {
        p->in_sentence = false;
        p->buf[p->len] = '\0';
        nmea_result_t r = nmea_parse_sentence(p->buf, out);
        if (r == NMEA_RMC || r == NMEA_OTHER) p->sentences_ok++;
        else if (r == NMEA_BAD_CHECKSUM) p->checksum_errors++;
        return r;
    }
    if (p->len >= NMEA_MAX_SENTENCE - 1U) {
        p->in_sentence = false;          /* garbage / missing CRLF: drop and wait for next '$' */
        p->overflow_errors++;
        return NMEA_MALFORMED;
    }
    p->buf[p->len++] = c;
    return NMEA_NONE;
}

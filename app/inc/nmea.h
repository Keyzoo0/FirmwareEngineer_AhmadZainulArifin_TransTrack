/**
 * @file  nmea.h
 * @brief Byte-wise NMEA 0183 sentence assembler and RMC parser (no HAL, unit-tested on host).
 */
#ifndef NMEA_H
#define NMEA_H

#include <stdbool.h>
#include <stdint.h>

#define NMEA_MAX_SENTENCE 96U   /* spec max is 82 incl. $ and CRLF */

typedef struct {
    bool     valid;           /* status 'A' */
    int32_t  lat_e7;          /* degrees * 1e7, negative = south */
    int32_t  lon_e7;          /* degrees * 1e7, negative = west */
    uint32_t speed_knots_x100;
    uint32_t course_deg_x100;
    uint8_t  hh, mm, ss;
    uint8_t  day, month, year; /* year: 2-digit */
} nmea_rmc_t;

typedef struct {
    char     buf[NMEA_MAX_SENTENCE];
    uint8_t  len;
    bool     in_sentence;
    uint32_t sentences_ok;
    uint32_t checksum_errors;
    uint32_t overflow_errors;
} nmea_parser_t;

typedef enum {
    NMEA_NONE = 0,            /* need more bytes */
    NMEA_RMC,                 /* *out updated with a checksum-valid RMC sentence */
    NMEA_OTHER,               /* valid sentence of another type (GGA, GSV, ...) */
    NMEA_BAD_CHECKSUM,
    NMEA_MALFORMED,
} nmea_result_t;

void nmea_init(nmea_parser_t *p);

/** Feed one byte. Returns NMEA_RMC when a complete, checksum-valid $xxRMC sentence was parsed. */
nmea_result_t nmea_feed(nmea_parser_t *p, char c, nmea_rmc_t *out);

/** Parse a complete sentence starting with '$' (without CRLF). Exposed for tests. */
nmea_result_t nmea_parse_sentence(const char *s, nmea_rmc_t *out);

#endif /* NMEA_H */

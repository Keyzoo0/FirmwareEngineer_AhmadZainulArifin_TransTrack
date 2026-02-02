/**
 ******************************************************************************
 * @file    gps.c
 * @brief   GPS NMEA Parser Implementation ($GPRMC)
 ******************************************************************************
 */

#include "gps.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* External UART handle (defined in main.c) */
extern UART_HandleTypeDef huart2;

/* Private helper functions */
static const char* gps_get_next_field(const char *sentence, char *buffer, size_t max_len);
static int gps_parse_time(const char *field, gps_datetime_t *dt);
static int gps_parse_date(const char *field, gps_datetime_t *dt);
static int gps_parse_position(const char *lat_field, const char *lat_dir,
                              const char *lon_field, const char *lon_dir,
                              gps_position_t *pos);
static uint8_t gps_nibble_to_hex(uint8_t nibble);

/**
 * @brief Initialize GPS module
 */
int gps_init(gps_t *gps) {
    if (gps == NULL) return -1;

    memset(gps, 0, sizeof(gps_t));
    gps->rx_index = 0;
    gps->initialized = true;

    return 0;
}

/**
 * @brief Process received GPS data byte
 */
int gps_process_byte(gps_t *gps, uint8_t byte) {
    if (gps == NULL || !gps->initialized) return -1;

    // Add byte to buffer
    gps->rx_buffer[gps->rx_index] = byte;

    // Check for end of sentence (\n)
    if (byte == '\n') {
        // Null-terminate
        gps->rx_buffer[gps->rx_index] = '\0';

        // Check if it's a $GPRMC sentence
        if (gps->rx_index > 6 && strncmp((char*)gps->rx_buffer, "$GPRMC", 6) == 0) {
            int ret = gps_parse_gprmc(gps, (char*)gps->rx_buffer);
            gps->rx_index = 0;
            return ret;
        }
        gps->rx_index = 0;
        return 0; // Not our sentence
    }

    // Check buffer overflow
    if (gps->rx_index >= GPS_BUFFER_SIZE - 1) {
        gps->rx_index = 0;
        return -2; // Buffer overflow
    }

    gps->rx_index++;
    return 0;
}

/**
 * @brief Parse $GPRMC sentence
 */
int gps_parse_gprmc(gps_t *gps, const char *sentence) {
    if (gps == NULL || sentence == NULL) return -1;

    char field[32];
    const char *ptr = sentence;

    // Validate checksum first
    if (!gps_validate_checksum(sentence)) {
        gps->data.checksum_errors++;
        return -2; // Checksum error
    }

    // Skip sentence type ($GPRMC)
    ptr = gps_get_next_field(ptr, field, sizeof(field));

    // Parse time (field 1)
    ptr = gps_get_next_field(ptr, field, sizeof(field));
    if (field[0] != '\0' && field[0] != ',') {
        gps_parse_time(field, &gps->data.datetime);
    }

    // Parse status (field 2) - V=Warning, A=Valid
    ptr = gps_get_next_field(ptr, field, sizeof(field));
    if (field[0] == GPS_STATUS_VALID) {
        gps->data.valid = true;
        gps->data.fix_acquired = true;
    } else {
        gps->data.valid = false;
        gps->data.fix_acquired = false;
    }

    // Parse latitude (field 3)
    char lat_str[16], lat_dir_str[2];
    ptr = gps_get_next_field(ptr, lat_str, sizeof(lat_str));

    // Parse latitude direction (field 4)
    ptr = gps_get_next_field(ptr, lat_dir_str, sizeof(lat_dir_str));

    // Parse longitude (field 5)
    char lon_str[16], lon_dir_str[2];
    ptr = gps_get_next_field(ptr, lon_str, sizeof(lon_str));

    // Parse longitude direction (field 6)
    ptr = gps_get_next_field(ptr, lon_dir_str, sizeof(lon_dir_str));

    // Parse position if valid
    if (lat_str[0] != '\0' && lat_str[0] != ',') {
        gps_parse_position(lat_str, lat_dir_str, lon_str, lon_dir_str, &gps->data.position);
    }

    // Parse speed in knots (field 7)
    ptr = gps_get_next_field(ptr, field, sizeof(field));
    if (field[0] != '\0' && field[0] != ',') {
        gps->data.speed_knots = atof(field);
    }

    // Parse track angle (field 8)
    ptr = gps_get_next_field(ptr, field, sizeof(field));
    if (field[0] != '\0' && field[0] != ',') {
        gps->data.track_angle = atof(field);
    }

    // Parse date (field 9)
    ptr = gps_get_next_field(ptr, field, sizeof(field));
    if (field[0] != '\0' && field[0] != ',') {
        gps_parse_date(field, &gps->data.datetime);
    }

    gps->data.last_update_ms = HAL_GetTick();
    gps->data.sentence_count++;

    return 0;
}

/**
 * @brief Validate NMEA checksum
 */
bool gps_validate_checksum(const char *sentence) {
    if (sentence == NULL || sentence[0] != '$') return false;

    uint8_t checksum = 0;
    const char *ptr = sentence + 1; // Skip $

    // Calculate XOR of all bytes between $ and *
    while (*ptr != '*' && *ptr != '\0') {
        checksum ^= *ptr++;
    }

    if (*ptr != '*') return false; // No checksum found

    // Parse hex checksum after *
    uint8_t received_checksum = 0;
    ptr++; // Skip *

    if (ptr[0] == '\0' || ptr[1] == '\0') return false;

    received_checksum = (gps_nibble_to_hex(ptr[0]) << 4) | gps_nibble_to_hex(ptr[1]);

    return checksum == received_checksum;
}

/**
 * @brief Convert NMEA coordinate to decimal degrees
 */
double gps_nmea_to_decimal(double nmea_coord, gps_direction_t dir) {
    uint32_t degrees = (uint32_t)(nmea_coord / 100.0);
    double minutes = nmea_coord - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (dir == GPS_DIR_SOUTH || dir == GPS_DIR_WEST) {
        decimal = -decimal;
    }

    return decimal;
}

/**
 * @brief Check if GPS data is fresh
 */
bool gps_is_data_fresh(const gps_t *gps, uint32_t timeout_ms) {
    if (gps == NULL) return false;
    return get_elapsed_ms(gps->data.last_update_ms) < timeout_ms;
}

/* Private helper functions */

static const char* gps_get_next_field(const char *sentence, char *buffer, size_t max_len) {
    if (sentence == NULL || buffer == NULL) return NULL;

    // Skip current position to next comma or end
    const char *ptr = sentence;
    while (*ptr != ',' && *ptr != '\0' && *ptr != '*' && *ptr != '\r' && *ptr != '\n') {
        ptr++;
    }

    // Copy field content
    size_t len = ptr - sentence;
    if (len >= max_len) len = max_len - 1;
    memcpy(buffer, sentence, len);
    buffer[len] = '\0';

    // Skip comma
    if (*ptr == ',') ptr++;

    return ptr;
}

static int gps_parse_time(const char *field, gps_datetime_t *dt) {
    if (field == NULL || dt == NULL || strlen(field) < 6) return -1;

    // Format: hhmmss.sss
    char time_str[16];
    strncpy(time_str, field, sizeof(time_str) - 1);
    time_str[sizeof(time_str) - 1] = '\0';

    // Parse hours
    char hh[3] = {0};
    hh[0] = time_str[0];
    hh[1] = time_str[1];
    dt->hours = atoi(hh);

    // Parse minutes
    char mm[3] = {0};
    mm[0] = time_str[2];
    mm[1] = time_str[3];
    dt->minutes = atoi(mm);

    // Parse seconds
    char ss[3] = {0};
    ss[0] = time_str[4];
    ss[1] = time_str[5];
    dt->seconds = atoi(ss);

    // Parse milliseconds (if present)
    if (strlen(time_str) > 7) {
        char ms_str[4] = {0};
        ms_str[0] = time_str[7];
        ms_str[1] = time_str[8];
        ms_str[2] = time_str[9];
        dt->milliseconds = atoi(ms_str);
    } else {
        dt->milliseconds = 0;
    }

    return 0;
}

static int gps_parse_date(const char *field, gps_datetime_t *dt) {
    if (field == NULL || dt == NULL || strlen(field) < 6) return -1;

    // Format: ddmmyy
    char dd[3] = {field[0], field[1], 0};
    char mm[3] = {field[2], field[3], 0};
    char yy[3] = {field[4], field[5], 0};

    dt->day = atoi(dd);
    dt->month = atoi(mm);
    dt->year = 2000 + atoi(yy); // Y2K compliance!

    return 0;
}

static int gps_parse_position(const char *lat_field, const char *lat_dir,
                              const char *lon_field, const char *lon_dir,
                              gps_position_t *pos) {
    if (lat_field == NULL || lat_dir == NULL ||
        lon_field == NULL || lon_dir == NULL || pos == NULL) {
        return -1;
    }

    double lat = atof(lat_field);
    double lon = atof(lon_field);

    pos->lat_dir = (gps_direction_t)lat_dir[0];
    pos->lon_dir = (gps_direction_t)lon_dir[0];

    pos->latitude = gps_nmea_to_decimal(lat, pos->lat_dir);
    pos->longitude = gps_nmea_to_decimal(lon, pos->lon_dir);

    return 0;
}

static uint8_t gps_nibble_to_hex(uint8_t nibble) {
    if (nibble >= '0' && nibble <= '9') {
        return nibble - '0';
    } else if (nibble >= 'A' && nibble <= 'F') {
        return nibble - 'A' + 10;
    } else if (nibble >= 'a' && nibble <= 'f') {
        return nibble - 'a' + 10;
    }
    return 0;
}

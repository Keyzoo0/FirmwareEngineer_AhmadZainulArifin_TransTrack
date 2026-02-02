/**
 ******************************************************************************
 * @file    gps.h
 * @brief   GPS NMEA Parser Header (specifically $GPRMC)
 * @details UART-based GPS with $GPRMC parsing and checksum validation
 ******************************************************************************
 */

#ifndef GPS_H
#define GPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* GPS Configuration */
#define GPS_BUFFER_SIZE          256
#define GPS_MAX_SENTENCE_LEN     128
#define GPS_TIMEOUT_MS           5000

/* GPS Status */
typedef enum {
    GPS_STATUS_INVALID = 'V',    // Warning/Void
    GPS_STATUS_VALID   = 'A'     // Valid/Active
} gps_status_t;

/* GPS Direction indicators */
typedef enum {
    GPS_DIR_NORTH = 'N',
    GPS_DIR_SOUTH = 'S',
    GPS_DIR_EAST  = 'E',
    GPS_DIR_WEST  = 'W'
} gps_direction_t;

/* GPS Nautical Speed */
// 1 knot = 1.852 km/h
typedef float gps_speed_t;

/* GPS Date and Time */
typedef struct {
    uint8_t hours;           // HH (UTC)
    uint8_t minutes;         // MM (UTC)
    uint8_t seconds;         // SS (UTC)
    uint16_t milliseconds;   // sss (fractional seconds)
    uint8_t day;             // DD
    uint8_t month;           // MM
    uint16_t year;           // YY (2000 + YY)
} gps_datetime_t;

/* GPS Position */
typedef struct {
    double latitude;         // Decimal degrees
    double longitude;        // Decimal degrees
    gps_direction_t lat_dir;
    gps_direction_t lon_dir;
} gps_position_t;

/* GPS $GPRMC Sentence Structure */
/*
 * $GPRMC,hhmmss.ss,V,ddmm.mmm,N,dddmm.mmm,E,ddd.dd,ddd.dd,ddmmyy,,,A*hh<CR><LF>
 *
 * Fields:
 * 0. $GPRMC - Sentence type
 * 1. hhmmss.ss - UTC Time
 * 2. V - Status (V=Warning, A=Valid)
 * 3. ddmm.mmm - Latitude (degrees + minutes)
 * 4. N - Latitude direction (N/S)
 * 5. dddmm.mmm - Longitude (degrees + minutes)
 * 6. E - Longitude direction (E/W)
 * 7. ddd.dd - Speed over ground (knots)
 * 8. ddd.dd - Track angle (degrees)
 * 9. ddmmyy - Date
 * 10. - Magnetic variation (blank)
 * 11. - Magnetic variation direction (blank)
 * 12. A - Mode indicator (A=Autonomous, D=Differential, etc.)
 * 13. hh - Checksum
 */

/* GPS Data Structure */
typedef struct {
    bool valid;                  // True if data is valid
    bool fix_acquired;           // True if GPS has a fix
    gps_datetime_t datetime;     // UTC date/time
    gps_position_t position;     // Latitude/longitude
    gps_speed_t speed_knots;     // Speed in knots
    float track_angle;           // Track angle in degrees
    uint32_t last_update_ms;     // Last valid update timestamp
    uint32_t sentence_count;     // Total sentences parsed
    uint32_t checksum_errors;    // Checksum validation failures
} gps_data_t;

/* GPS Device Structure */
typedef struct {
    gps_data_t data;
    uint8_t rx_buffer[GPS_BUFFER_SIZE];
    volatile uint16_t rx_index;
    bool initialized;
} gps_t;

/* Function Prototypes */

/**
 * @brief Initialize GPS module
 * @param gps Pointer to GPS structure
 * @return 0 on success, negative error code on failure
 */
int gps_init(gps_t *gps);

/**
 * @brief Process received GPS data (call from UART RX interrupt or DMA)
 * @param gps Pointer to GPS structure
 * @param byte Received byte
 * @return 0 if more data needed, 1 if complete sentence parsed, negative on error
 */
int gps_process_byte(gps_t *gps, uint8_t byte);

/**
 * @brief Parse $GPRMC sentence
 * @param gps Pointer to GPS structure
 * @param sentence Pointer to null-terminated sentence string
 * @return 0 on success, negative error code on failure
 */
int gps_parse_gprmc(gps_t *gps, const char *sentence);

/**
 * @brief Validate NMEA checksum
 * @param sentence Pointer to sentence string (including $ and *hh)
 * @return true if checksum valid, false otherwise
 */
bool gps_validate_checksum(const char *sentence);

/**
 * @brief Convert NMEA latitude/longitude to decimal degrees
 * @param nmea_coord NMEA format coordinate (ddmm.mmmm or dddmm.mmmm)
 * @param dir Direction (N/S/E/W)
 * @return Decimal degrees
 */
double gps_nmea_to_decimal(double nmea_coord, gps_direction_t dir);

/**
 * @brief Check if GPS data is fresh (within timeout)
 * @param gps Pointer to GPS structure
 * @param timeout_ms Timeout in milliseconds
 * @return true if data is fresh, false otherwise
 */
bool gps_is_data_fresh(const gps_t *gps, uint32_t timeout_ms);

/**
 * @brief Get speed in km/h from knots
 * @param knots Speed in knots
 * @return Speed in km/h
 */
static inline float gps_knots_to_kph(gps_speed_t knots) {
    return knots * 1.852f;
}

/**
 * @brief Get speed in m/s from knots
 * @param knots Speed in knots
 * @return Speed in m/s
 */
static inline float gps_knots_to_mps(gps_speed_t knots) {
    return knots * 0.514444f;
}

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */

/**
 * @file    bsp_gps.c
 * @brief   GPS NMEA 0183 parser implementation
 *
 *          Parses $GPRMC (position/time) and $GPGGA (altitude/satellites).
 *          Byte-fed state machine — call BSP_GPS_FeedByte() per UART byte.
 */
#include "bsp_gps.h"
#include <string.h>
#include <stdlib.h>

/* ========================= Internal State ========================= */

#define NMEA_MAX_LEN  120

static UART_HandleTypeDef *s_huart;
static GPS_Data s_gps_data;
static char s_nmea_buf[NMEA_MAX_LEN];
static uint8_t s_nmea_idx;
static uint8_t s_in_sentence;

/* ========================= NMEA Parser ========================= */

/**
 * @brief  Parse NMEA latitude (ddmm.mmmm) to decimal degrees
 */
static float parse_lat(const char *s, char ns)
{
    if (!s || !*s) return 0.0f;
    float raw = (float)atof(s);
    int deg = (int)(raw / 100.0f);
    float min = raw - deg * 100.0f;
    float result = deg + min / 60.0f;
    if (ns == 'S') result = -result;
    return result;
}

/**
 * @brief  Parse NMEA longitude (dddmm.mmmm) to decimal degrees
 */
static float parse_lon(const char *s, char ew)
{
    if (!s || !*s) return 0.0f;
    float raw = (float)atof(s);
    int deg = (int)(raw / 100.0f);
    float min = raw - deg * 100.0f;
    float result = deg + min / 60.0f;
    if (ew == 'W') result = -result;
    return result;
}

/**
 * @brief  Split NMEA sentence into comma-separated fields
 */
static int split_fields(char *sentence, char **fields, int max_fields)
{
    int count = 0;
    fields[count++] = sentence;
    while (*sentence && count < max_fields) {
        if (*sentence == ',') {
            *sentence = '\0';
            fields[count++] = sentence + 1;
        }
        sentence++;
    }
    return count;
}

/**
 * @brief  Parse $GPRMC sentence
 *         $GPRMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed,course,ddmmyy,...
 */
static void parse_rmc(char *sentence)
{
    char *fields[15];
    int n = split_fields(sentence, fields, 15);
    if (n < 10) return;

    /* Time: hhmmss.ss */
    if (strlen(fields[1]) >= 6) {
        s_gps_data.hour = (fields[1][0] - '0') * 10 + (fields[1][1] - '0');
        s_gps_data.min  = (fields[1][2] - '0') * 10 + (fields[1][3] - '0');
        s_gps_data.sec  = (fields[1][4] - '0') * 10 + (fields[1][5] - '0');
    }

    /* Status */
    s_gps_data.valid = fields[2][0];

    /* Latitude */
    s_gps_data.latitude = parse_lat(fields[3], fields[4][0]);

    /* Longitude */
    s_gps_data.longitude = parse_lon(fields[5], fields[6][0]);

    /* Speed */
    if (fields[7][0]) s_gps_data.speed_knots = (float)atof(fields[7]);

    /* Course */
    if (fields[8][0]) s_gps_data.course = (float)atof(fields[8]);

    /* Date: ddmmyy */
    if (strlen(fields[9]) >= 6) {
        s_gps_data.day   = (fields[9][0] - '0') * 10 + (fields[9][1] - '0');
        s_gps_data.month = (fields[9][2] - '0') * 10 + (fields[9][3] - '0');
        s_gps_data.year  = 2000 + (fields[9][4] - '0') * 10 + (fields[9][5] - '0');
    }
}

/**
 * @brief  Parse $GPGGA sentence
 *         $GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,fix,sats,hdop,alt,M,...
 */
static void parse_gga(char *sentence)
{
    char *fields[15];
    int n = split_fields(sentence, fields, 15);
    if (n < 10) return;

    /* Satellites */
    if (fields[7][0]) s_gps_data.satellites = (uint8_t)atoi(fields[7]);

    /* HDOP */
    if (fields[8][0]) s_gps_data.hdop = (float)atof(fields[8]);

    /* Altitude */
    if (fields[9][0]) s_gps_data.altitude = (float)atof(fields[9]);
}

/**
 * @brief  Process a complete NMEA sentence
 */
static void process_sentence(char *sentence)
{
    /* Skip '$' */
    if (sentence[0] == '$') sentence++;

    /* Identify sentence type */
    if (strncmp(sentence, "GPRMC,", 6) == 0 ||
        strncmp(sentence, "GNRMC,", 6) == 0) {
        parse_rmc(sentence);
    } else if (strncmp(sentence, "GPGGA,", 6) == 0 ||
               strncmp(sentence, "GNGGA,", 6) == 0) {
        parse_gga(sentence);
    }
}

/* ========================= Public API ========================= */

void BSP_GPS_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    memset(&s_gps_data, 0, sizeof(GPS_Data));
    s_nmea_idx = 0;
    s_in_sentence = 0;
}

void BSP_GPS_FeedByte(uint8_t byte)
{
    if (byte == '$') {
        /* Start of new sentence */
        s_nmea_idx = 0;
        s_in_sentence = 1;
        s_nmea_buf[s_nmea_idx++] = (char)byte;
    } else if (s_in_sentence) {
        if (byte == '\n' || byte == '\r') {
            /* End of sentence */
            s_nmea_buf[s_nmea_idx] = '\0';
            s_in_sentence = 0;
            if (s_nmea_idx > 6) {
                process_sentence(s_nmea_buf);
            }
        } else if (s_nmea_idx < NMEA_MAX_LEN - 1) {
            s_nmea_buf[s_nmea_idx++] = (char)byte;
        } else {
            s_in_sentence = 0;  /* Buffer overflow, discard */
        }
    }
}

int BSP_GPS_GetData(GPS_Data *data)
{
    *data = s_gps_data;
    return (s_gps_data.valid == 'A') ? 0 : -1;
}

int BSP_GPS_HasFix(void)
{
    return (s_gps_data.valid == 'A') ? 1 : 0;
}

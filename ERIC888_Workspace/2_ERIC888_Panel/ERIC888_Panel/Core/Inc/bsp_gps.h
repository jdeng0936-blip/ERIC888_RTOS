/**
 * @file    bsp_gps.h
 * @brief   GPS NMEA parser driver for ERIC888 B-board
 *
 *          Hardware: UART1 (PA9=TX, PA10=RX)
 *          Protocol: NMEA 0183 ($GPRMC, $GPGGA)
 */
#ifndef BSP_GPS_H
#define BSP_GPS_H

#include "stm32f4xx_hal.h"

/* GPS fix data */
typedef struct {
    uint8_t  valid;          /* 'A'=valid, 'V'=invalid */
    float    latitude;       /* degrees (+N, -S) */
    float    longitude;      /* degrees (+E, -W) */
    float    speed_knots;    /* speed over ground */
    float    course;         /* course over ground (degrees) */
    uint8_t  hour, min, sec; /* UTC time */
    uint8_t  day, month;     /* UTC date */
    uint16_t year;           /* UTC year */
    uint8_t  satellites;     /* number of satellites in use */
    float    altitude;       /* altitude above MSL (meters) */
    float    hdop;           /* horizontal dilution of precision */
} GPS_Data;

/**
 * @brief  Initialize GPS module (start UART reception)
 * @param  huart: UART handle (UART1)
 */
void BSP_GPS_Init(UART_HandleTypeDef *huart);

/**
 * @brief  Feed one byte from UART to the NMEA parser
 *         Call this from UART RX callback or polling loop
 * @param  byte: received byte
 */
void BSP_GPS_FeedByte(uint8_t byte);

/**
 * @brief  Get latest GPS fix data
 * @param  data: output struct (copied from internal state)
 * @retval 0=valid fix, -1=no fix
 */
int BSP_GPS_GetData(GPS_Data *data);

/**
 * @brief  Check if GPS has a valid fix
 * @retval 1=valid, 0=no fix
 */
int BSP_GPS_HasFix(void);

#endif /* BSP_GPS_H */

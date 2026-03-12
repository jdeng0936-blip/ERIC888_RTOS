/**
 * @file    bsp_w5500_socket.h
 * @brief   W5500 TCP/UDP Socket layer (on top of bsp_w5500 register driver)
 *
 *          Provides BSD-like socket API for W5500:
 *          - Socket open/close
 *          - TCP server: listen/accept
 *          - TCP client: connect
 *          - Data send/recv
 *
 *          W5500 supports 8 hardware sockets (Sn, n=0..7)
 */
#ifndef BSP_W5500_SOCKET_H
#define BSP_W5500_SOCKET_H

#include "bsp_w5500.h"

/* W5500 Socket Register Block Select Bits (BSB) */
#define W5500_Sn_BSB(n)     (((n) * 4 + 1) << 3)   /* Socket n register */
#define W5500_Sn_TX_BSB(n)  (((n) * 4 + 2) << 3)   /* Socket n TX buffer */
#define W5500_Sn_RX_BSB(n)  (((n) * 4 + 3) << 3)   /* Socket n RX buffer */

/* Socket Register Offsets */
#define Sn_MR       0x0000  /* Mode Register */
#define Sn_CR       0x0001  /* Command Register */
#define Sn_IR       0x0002  /* Interrupt Register */
#define Sn_SR       0x0003  /* Status Register */
#define Sn_PORT     0x0004  /* Source Port (2 bytes) */
#define Sn_DIPR     0x000C  /* Dest IP (4 bytes) */
#define Sn_DPORT    0x0010  /* Dest Port (2 bytes) */
#define Sn_RXBUF_SIZE 0x001E /* RX buffer size */
#define Sn_TXBUF_SIZE 0x001F /* TX buffer size */
#define Sn_TX_FSR   0x0020  /* TX Free Size (2 bytes) */
#define Sn_TX_RD    0x0022  /* TX Read Pointer */
#define Sn_TX_WR    0x0024  /* TX Write Pointer */
#define Sn_RX_RSR   0x0026  /* RX Received Size (2 bytes) */
#define Sn_RX_RD    0x0028  /* RX Read Pointer */

/* Socket Status Values */
#define SOCK_CLOSED     0x00
#define SOCK_INIT       0x13
#define SOCK_LISTEN     0x14
#define SOCK_ESTABLISHED 0x17
#define SOCK_CLOSE_WAIT 0x1C
#define SOCK_UDP        0x22

/* Socket Commands */
#define Sn_CMD_OPEN     0x01
#define Sn_CMD_LISTEN   0x02
#define Sn_CMD_CONNECT  0x04
#define Sn_CMD_DISCON   0x08
#define Sn_CMD_CLOSE    0x10
#define Sn_CMD_SEND     0x20
#define Sn_CMD_RECV     0x40

/* Socket Mode */
#define Sn_MR_TCP       0x01
#define Sn_MR_UDP       0x02

/**
 * @brief  Open a socket
 * @param  sn: Socket number (0-7)
 * @param  protocol: Sn_MR_TCP or Sn_MR_UDP
 * @param  port: Local port number
 * @retval 0=success, -1=error
 */
int W5500_Socket_Open(uint8_t sn, uint8_t protocol, uint16_t port);

/**
 * @brief  Close a socket
 */
void W5500_Socket_Close(uint8_t sn);

/**
 * @brief  Start listening on TCP socket (server mode)
 * @retval 0=success, -1=not in INIT state
 */
int W5500_Socket_Listen(uint8_t sn);

/**
 * @brief  Get socket status
 * @retval Socket status register value
 */
uint8_t W5500_Socket_Status(uint8_t sn);

/**
 * @brief  Send data on connected TCP socket
 * @param  sn: Socket number
 * @param  data: Data buffer
 * @param  len: Data length
 * @retval Bytes sent, or -1 on error
 */
int W5500_Socket_Send(uint8_t sn, const uint8_t *data, uint16_t len);

/**
 * @brief  Receive data from TCP socket
 * @param  sn: Socket number
 * @param  buf: Receive buffer
 * @param  buf_len: Buffer size
 * @retval Bytes received, 0=no data, -1=error
 */
int W5500_Socket_Recv(uint8_t sn, uint8_t *buf, uint16_t buf_len);

/**
 * @brief  Get number of bytes available to read
 */
uint16_t W5500_Socket_Available(uint8_t sn);

#endif /* BSP_W5500_SOCKET_H */

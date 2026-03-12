/**
 * @file    bsp_w5500_socket.c
 * @brief   W5500 TCP/UDP Socket layer implementation
 *
 *          Uses bsp_w5500.c register read/write for hardware access.
 *          Implements open/listen/send/recv/close using W5500 socket registers.
 */
#include "bsp_w5500_socket.h"

/* ========================= Helper ========================= */

/** Write a socket command and wait for it to complete */
static void socket_cmd(uint8_t sn, uint8_t cmd)
{
    BSP_W5500_WriteReg(Sn_CR, W5500_Sn_BSB(sn) | 0x04, cmd);
    /* Wait for command register to clear (command accepted) */
    while (BSP_W5500_ReadReg(Sn_CR, W5500_Sn_BSB(sn)) != 0x00)
        ;
}

/** Read 16-bit register from socket */
static uint16_t socket_read16(uint8_t sn, uint16_t reg)
{
    uint8_t hi = BSP_W5500_ReadReg(reg, W5500_Sn_BSB(sn));
    uint8_t lo = BSP_W5500_ReadReg(reg + 1, W5500_Sn_BSB(sn));
    return (uint16_t)((hi << 8) | lo);
}

/** Write 16-bit register to socket */
static void socket_write16(uint8_t sn, uint16_t reg, uint16_t val)
{
    BSP_W5500_WriteReg(reg, W5500_Sn_BSB(sn) | 0x04, (val >> 8) & 0xFF);
    BSP_W5500_WriteReg(reg + 1, W5500_Sn_BSB(sn) | 0x04, val & 0xFF);
}

/* ========================= Public API ========================= */

int W5500_Socket_Open(uint8_t sn, uint8_t protocol, uint16_t port)
{
    if (sn > 7) return -1;

    /* Close any existing connection */
    W5500_Socket_Close(sn);

    /* Set protocol mode */
    BSP_W5500_WriteReg(Sn_MR, W5500_Sn_BSB(sn) | 0x04, protocol);

    /* Set source port */
    socket_write16(sn, Sn_PORT, port);

    /* Set buffer sizes (2KB each by default) */
    BSP_W5500_WriteReg(Sn_RXBUF_SIZE, W5500_Sn_BSB(sn) | 0x04, 2);
    BSP_W5500_WriteReg(Sn_TXBUF_SIZE, W5500_Sn_BSB(sn) | 0x04, 2);

    /* Issue OPEN command */
    socket_cmd(sn, Sn_CMD_OPEN);

    /* Verify socket entered INIT (TCP) or UDP state */
    uint8_t sr = BSP_W5500_ReadReg(Sn_SR, W5500_Sn_BSB(sn));
    if (protocol == Sn_MR_TCP && sr != SOCK_INIT) return -1;
    if (protocol == Sn_MR_UDP && sr != SOCK_UDP) return -1;

    return 0;
}

void W5500_Socket_Close(uint8_t sn)
{
    socket_cmd(sn, Sn_CMD_CLOSE);
    /* Clear interrupts */
    BSP_W5500_WriteReg(Sn_IR, W5500_Sn_BSB(sn) | 0x04, 0xFF);
}

int W5500_Socket_Listen(uint8_t sn)
{
    uint8_t sr = BSP_W5500_ReadReg(Sn_SR, W5500_Sn_BSB(sn));
    if (sr != SOCK_INIT) return -1;

    socket_cmd(sn, Sn_CMD_LISTEN);

    sr = BSP_W5500_ReadReg(Sn_SR, W5500_Sn_BSB(sn));
    if (sr != SOCK_LISTEN) return -1;

    return 0;
}

uint8_t W5500_Socket_Status(uint8_t sn)
{
    return BSP_W5500_ReadReg(Sn_SR, W5500_Sn_BSB(sn));
}

int W5500_Socket_Send(uint8_t sn, const uint8_t *data, uint16_t len)
{
    if (len == 0) return 0;

    /* Check socket is connected */
    uint8_t sr = BSP_W5500_ReadReg(Sn_SR, W5500_Sn_BSB(sn));
    if (sr != SOCK_ESTABLISHED) return -1;

    /* Wait for TX free space */
    uint16_t free_size;
    do {
        free_size = socket_read16(sn, Sn_TX_FSR);
    } while (free_size < len);

    /* Get TX write pointer */
    uint16_t ptr = socket_read16(sn, Sn_TX_WR);

    /* Write data to TX buffer */
    BSP_W5500_WriteBuf(ptr, W5500_Sn_TX_BSB(sn) | 0x04, data, len);

    /* Update TX write pointer */
    socket_write16(sn, Sn_TX_WR, ptr + len);

    /* Issue SEND command */
    socket_cmd(sn, Sn_CMD_SEND);

    return (int)len;
}

int W5500_Socket_Recv(uint8_t sn, uint8_t *buf, uint16_t buf_len)
{
    uint16_t rx_size = W5500_Socket_Available(sn);
    if (rx_size == 0) return 0;

    /* Limit to buffer size */
    if (rx_size > buf_len) rx_size = buf_len;

    /* Get RX read pointer */
    uint16_t ptr = socket_read16(sn, Sn_RX_RD);

    /* Read data from RX buffer */
    BSP_W5500_ReadBuf(ptr, W5500_Sn_RX_BSB(sn), buf, rx_size);

    /* Update RX read pointer */
    socket_write16(sn, Sn_RX_RD, ptr + rx_size);

    /* Issue RECV command to release buffer */
    socket_cmd(sn, Sn_CMD_RECV);

    return (int)rx_size;
}

uint16_t W5500_Socket_Available(uint8_t sn)
{
    return socket_read16(sn, Sn_RX_RSR);
}

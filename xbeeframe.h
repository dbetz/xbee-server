/* xbeeframe.h - definitions for the Xbee frame driver */

#ifndef __XBEEFRAME_H__
#define __XBEEFRAME_H__

#include <stdint.h>

/**
 * Defines receive buffer length
 */
#define XBEEFRAME_RXSIZE    1024

#define XBEEFRAME_STATUS_IDLE           0
#define XBEEFRAME_STATUS_BUSY           1

/* mailbox structure */
typedef struct {
    volatile uint8_t *rxframe;  // rx frame buffer
    volatile uint32_t rxlength; // rx frame length
    volatile uint32_t rxstatus; // rx status
    volatile uint8_t *txframe;  // tx frame buffer
    volatile uint32_t txlength; // tx frame length
    volatile uint32_t txstatus; // tx status
    volatile uint8_t *ldbuf;    // buffer containing first data to load
    volatile uint32_t ldcount;  // number of bytes in ldbuf
    volatile uint8_t *ldaddr;   // hub load address
    volatile uint32_t ldtotal;  // hub load byte count
    volatile uint32_t ticks;    // number of ticks per bit
    uint32_t cogId;             // cog running driver (not used by driver)
    uint8_t buffers[XBEEFRAME_RXSIZE * 2];
} XbeeFrame_t;

int XbeeFrame_start(XbeeFrame_t *mailbox, int rxpin, int txpin, int mode, int baudrate);
void XbeeFrame_sendframe(XbeeFrame_t *mailbox, uint8_t *frame, int length);
uint8_t *XbeeFrame_recvframe(XbeeFrame_t *mailbox, int *plength);
void XbeeFrame_release(XbeeFrame_t *mailbox);

#endif

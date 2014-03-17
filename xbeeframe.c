/* xbeeframe.c - Xbee frame driver interface */

#include <propeller.h>
#include "xbeeframe.h"

/* init structure */
typedef struct {
    XbeeFrame_t *mailbox;   // mailbox address
    uint32_t rx_pin;        // receive pin
    uint32_t tx_pin;        // transmit pin
    uint32_t rts_pin;       // rts pin
    uint32_t ticks;         // baud rate
    uint32_t rxlength;      // size of frame buffer
    uint8_t *buffers;       // rxlength*2 size buffer
} XbeeFrameInit_t;

/**
 * XbeeFrame_start - initializes and starts the xbee frame driver in a cog.
 * @param init is the initialization structure
 * @param mailbox is the mailbox structure
 * @param rxpin is pin number for receive input
 * @param txpin is pin number for transmit output
 * @param rtspin is pin number for rts
 * @param baudrate is frequency of bits ... 115200, 57600, etc...
 * @returns COG ID on success and -1 on failure
 */
int XbeeFrame_start(XbeeFrame_t *mailbox, int rxpin, int txpin, int rtspin, int baudrate)
{
    volatile XbeeFrameInit_t init;
    
    use_cog_driver(xbeeframe_driver);

    init.mailbox  = mailbox;                // mailbox
    init.rx_pin   = rxpin;                  // receive pin
    init.tx_pin   = txpin;                  // transmit pin
    init.rts_pin  = rtspin;                 // rts pin
    init.ticks    = _clkfreq / baudrate;    // baud
    init.rxlength = XBEEFRAME_RXSIZE;       // receive buffer length
    init.buffers  = mailbox->buffers;       // receive buffers
    
    memset(mailbox, 0, sizeof(XbeeFrame_t));
    mailbox->cogId = load_cog_driver(xbeeframe_driver, &init);
    while (init.mailbox)
        ;

    return mailbox->cogId;
}

/**
 * XbeeFrame_sendframe - sends a frame to the Xbee module 
 */
void XbeeFrame_sendframe(XbeeFrame_t *mailbox, uint8_t *frame, int length)
{
    mailbox->txframe = frame;
    mailbox->txlength = length;
    mailbox->txstatus = XBEEFRAME_STATUS_BUSY;
    while (mailbox->txstatus != XBEEFRAME_STATUS_IDLE)
        ;
}

/**
 * XbeeFrame_recvframe - receives a frame from the Xbee module 
 */
uint8_t *XbeeFrame_recvframe(XbeeFrame_t *mailbox, int *plength)
{
    if (mailbox->rxstatus == XBEEFRAME_STATUS_IDLE)
        return NULL;
    *plength = (int)mailbox->rxlength;
    return (uint8_t *)mailbox->rxframe;
}

/**
 * XbeeFrame_release - releases a frame received from the Xbee module 
 */
void XbeeFrame_release(XbeeFrame_t *mailbox)
{
    mailbox->rxstatus = XBEEFRAME_STATUS_IDLE;
}

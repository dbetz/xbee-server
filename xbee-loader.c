#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <propeller.h>
#include <eeprom.h>
#include "xbee-server.h"

#define EEPROM_ADDR         0xa0
#define EEPROM_BLOCK_SIZE   128

#define OPTIONS_RESPONSE "\
HTTP/1.1 200 OK\r\n\
Access-Control-Allow-Origin: *\r\n\
Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT\r\n\
Access-Control-Allow-Headers: PUT\r\n\
Access-Control-Max-Age: 1000000\r\n\
Keep-Alive: timeout=1, max=100\r\n\
Connection: Keep-Alive\r\n\
Content-Type: text/plain\r\n\
Content-Length: 0\r\n\
\r\n"

#define PUT_RESPONSE "\
HTTP/1.1 200 OK\r\n\
Access-Control-Allow-Origin: *\r\n\
Content-Length: 0\r\n\
\r\n"

#define ERROR_RESPONSE "\
HTTP/1.1 404 OK\r\n\
Access-Control-Allow-Origin: *\r\n\
Content-Length: 0\r\n\
\r\n"

static void handle_options_request(Socket_t *sock, int phase);
static void handle_put_request(Socket_t *sock, int phase);

MethodBinding_t methodBindings[] = {
{   "OPTIONS",  handle_options_request  },
{   "PUT",      handle_put_request      },
{   NULL,       NULL                    }
};

static EEPROM_BOOT eeprom_state;
static EEPROM *eeprom;

static void start_loader(uint32_t addr, uint32_t count);

/* main - the main routine */
int main(void)
{
    Server_t server;
    
    if ((eeprom = eepromBootOpen(&eeprom_state, EEPROM_ADDR)) == NULL) {
        printf("eepromOpen failed\n");
        return 1;
    }
            
    if (http_init(&server) != 0)
        return 1;
    server.methodBindings = methodBindings;

    http_serve(&server);
    
    return 0;
}

static void handle_options_request(Socket_t *sock, int phase)
{
    if (phase == HP_CONTENT)
        http_send_response(sock, (uint8_t *)OPTIONS_RESPONSE, sizeof(OPTIONS_RESPONSE) - 1);
}

enum {
    UNKNOWN_REQ,
    EEPROM_RD_REQ,
    EEPROM_WR_REQ,
    LOAD_REQ
};

static void handle_put_request(Socket_t *sock, int phase)
{
    static unsigned int start, count;
    static int type;
    char *p = (char *)sock->content;
    int remaining, cnt;
    
    switch (phase) {
    case HP_REQUEST:
        while (*p != '\0' && !isspace(*p))
            ++p;
        while (*p != '\0' && isspace(*p))
            ++p;
        if (strncmp(p, "/eeprom/", 8) == 0) {
            p += 8;
            start = atoi(p);
            type = EEPROM_WR_REQ;
        }
        else if (strncmp(p, "/load/", 6) == 0) {
            p += 6;
            start = atoi(p);
            if ((p = strchr(p, '/')) != NULL) {
                count = atoi(++p);
                type = LOAD_REQ;
            }
            else {
                printf("syntax error in /load request: '%s'\n", sock->content);
                type = UNKNOWN_REQ;
            }
        }
        else {
            printf("unknown request: '%s'\n", sock->content);
            type = UNKNOWN_REQ;
        }
        break;
    case HP_CONTENT:
        switch (type) {
        case EEPROM_WR_REQ:
            for (remaining = sock->length; remaining > 0; remaining -= cnt) {
                if ((cnt = remaining) > EEPROM_BLOCK_SIZE)
                    cnt = EEPROM_BLOCK_SIZE;
                if (eepromWrite(eeprom, (uint32_t)start, (uint8_t *)p, cnt) != 0)
                    printf("eepromWrite failed\n");
                start += cnt;
                p += cnt;
            }
            http_send_response(sock, (uint8_t *)PUT_RESPONSE, sizeof(PUT_RESPONSE) - 1);
            break;
        case LOAD_REQ:
            http_send_response(sock, (uint8_t *)PUT_RESPONSE, sizeof(PUT_RESPONSE) - 1);
            http_term(sock->server);
            if (eepromClose(eeprom) != 0)
                printf("failed to close eeprom driver\n");
            start_loader(start, count);
            break;
        default:
            http_send_response(sock, (uint8_t *)ERROR_RESPONSE, sizeof(ERROR_RESPONSE) - 1);
            break;
        }
        break;
    default:
        break;
    }
}

typedef struct {
    uint32_t jmp_inst;      // jmp to start
    uint32_t i2cDataSet;    // Minumum data setup time (ticks)
    uint32_t i2cClkLow;     // Minimum clock low time (ticks)
    uint32_t i2cClkHigh;    // Minimum clock high time (ticks)
    uint32_t eeprom_addr;   // EEPROM address
    uint32_t hub_addr;      // Hub address
    uint32_t count;         // Byte count
} LoaderHdr;

static void start_loader(uint32_t addr, uint32_t count)
{
    extern uint32_t binary_xbee_eeprom_loader_dat_start[];
    LoaderHdr *hdr = (LoaderHdr *)binary_xbee_eeprom_loader_dat_start;
    
    hdr->i2cDataSet = ((_clkfreq / 10000) * 250) / 100000;
    hdr->i2cClkLow = ((_clkfreq / 10000) * 1300) / 100000;
    hdr->i2cClkHigh = ((_clkfreq / 10000) * 1000) / 100000;
    hdr->eeprom_addr = addr;
    hdr->hub_addr = 0x0000;
    hdr->count = count;

    coginit(cogid(), binary_xbee_eeprom_loader_dat_start, 0);
    /* should never reach here! */
}

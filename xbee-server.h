#ifndef __XBEE_SERVER_H__
#define __XBEE_SERVER_H__

#include <stdint.h>
#include "xbeeframe.h"

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

#define MULTI_SOCKETS

#ifdef MULTI_SOCKETS
#define MAX_SOCKETS 4
#else
#define MAX_SOCKETS 1
#endif

enum {
    SF_BUSY = 0x00000001
};

enum {
    SS_REQUEST,
    SS_HEADER,
    SS_CONTENT,
    SS_SKIP_NL,
    SS_CONTENT_NL
};

#define MAX_CONTENT 1024

typedef struct Server_t Server_t;
typedef struct Socket_t Socket_t;

struct Socket_t {
    Server_t *server;
    int flags;
    int state;
    struct {    // must be in the same order as in IPV4RX_header_t
        uint8_t srcaddr[4];
        uint8_t dstport[2];
        uint8_t srcport[2];
    } id;
    uint8_t protocol;
    void (*handler)(Socket_t *sock, int phase);
    uint8_t content[MAX_CONTENT + 1];
    uint8_t *frame_ptr;
    int frame_len;
    int length;
    int i;
};

enum {
    HP_REQUEST,
    HP_HEADER,
    HP_CONTENT_START,
    HP_CONTENT
};

typedef struct {
    char *method;
    void (*handler)(Socket_t *sock, int phase);
} MethodBinding_t;

struct Server_t {
    Socket_t sockets[MAX_SOCKETS];
    MethodBinding_t *methodBindings;
    XbeeFrame_t mailbox;
    uint8_t txframeid;
    int cogid;
};

int http_init(Server_t *state);
void http_term(Server_t *state);
void http_serve(Server_t *state);
void http_send_response(Socket_t *sock, uint8_t *data, int length);

int prepare_response(Socket_t *sock, uint8_t *frame, uint8_t *data, int length);

#endif

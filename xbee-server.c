#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <propeller.h>
#include "xbee-server.h"
#include "xbeeframe.h"
#include "xbeeload.h"

//#define AT_DEBUG
//#define FRAME_RX_DEBUG
//#define FRAME_TX_DEBUG
//#define FRAME_DEBUG_TEXT
//#define STATE_MACHINE_DEBUG
//#define SOCKET_DEBUG
//#define STATUS_DEBUG

#define BAD_REQUEST_RESPONSE "\
HTTP/1.1 400 Bad Request\r\n\
\r\n"

/* Xbee pins */
#define XBEE_RX     9
#define XBEE_TX     8
#define XBEE_RTS    7
#define XBEE_BAUD   9600

#define ID_ATCOMMAND    0x08
#define ID_IPV4TX       0x20
#define ID_ATRESPONSE   0x88
#define ID_TXSTATUS     0x89
#define ID_IPV4RX       0xb0

#define PORT            80

typedef struct {
    uint8_t apiid;
    uint8_t frameid;
    uint8_t dstaddr[4];
    uint8_t dstport[2];
    uint8_t srcport[2];
    uint8_t protocol;
    uint8_t options;
    uint8_t data[1];
} IPV4TX_header_t;

typedef struct {
    uint8_t apiid;
    uint8_t srcaddr[4];
    uint8_t dstport[2];
    uint8_t srcport[2];
    uint8_t protocol;
    uint8_t status;
    uint8_t data[1];
} IPV4RX_header_t;

typedef struct {
    uint8_t apiid;
    uint8_t frameid;
    uint8_t status;
} TXStatus_t;

typedef struct {
    uint8_t apiid;
    uint8_t frameid;
    uint8_t command[1];
} ATCommand_t;

typedef struct {
    uint8_t apiid;
    uint8_t frameid;
    uint8_t command[2];
    uint8_t status;
    uint8_t value[1];
} ATResponse_t;

#define HUBSIZE         (32 * 1024)
#define RESPONSESIZE    1024

#define field_offset(s, f)  ((int)&((s *)0)->f)

/* prototypes */
static void handle_ipv4_frame(Server_t *server, IPV4RX_header_t *frame, int length);
static void handle_txstatus_frame(Server_t *server, TXStatus_t *frame, int length);
static int prepare_response(Socket_t *sock, uint8_t *frame, uint8_t *data, int length);
static void send_at_command(Server_t *server, int id, char *fmt, ...);
static void handle_atresponse_frame(Server_t *server, ATResponse_t *frame, int length);
static void parse_request(Socket_t *sock);
static void parse_header(Socket_t *sock);
static void handle_content(Socket_t *sock);
static char *match(Socket_t *sock, char *str);
static char *skip_spaces(char *str);
#if defined(AT_DEBUG) || defined(FRAME_RX_DEBUG) || defined(FRAME_TX_DEBUG)
static void show_frame(uint8_t *frame, int length);
#endif

/* http_init - initialize the http server */
int http_init(Server_t *server)
{
    /* initialize the server state */
    memset(server, 0, sizeof(Server_t));
    
    printf("Starting frame driver\n");
    if ((server->cogid = XbeeFrame_start(&server->mailbox, XBEE_RX, XBEE_TX, XBEE_RTS, XBEE_BAUD)) < 0) {
        printf("failed to start frame driver\n");
        return 1;
    }
    
    /* set the port and ask for our IP address */
    //send_at_command(server, 0, "C0%x", PORT); // this doesn't seem to work for some reason
    send_at_command(server, 1, "MY");

    return 0;
}

/* http_term - terminate the server */
void http_term(Server_t *server)
{
    if (server->cogid >= 0) {
        cogstop(server->cogid);
        server->cogid = -1;
    }
}

/* http_serve - serve http requests */
void http_serve(Server_t *server)
{    
    printf("Listening for frames\n");
    while (1) {
        uint8_t *frame;
        int length;
        
        if ((frame = XbeeFrame_recvframe(&server->mailbox, &length)) != NULL) {
        
#ifdef FRAME_RX_DEBUG
            printf("[RX]");
            show_frame(frame, length);
#endif
            
            /* handle the frame */
            switch (frame[0]) {
            case ID_IPV4RX:
                handle_ipv4_frame(server, (IPV4RX_header_t *)frame, length);
                break;
            case ID_ATRESPONSE:
                handle_atresponse_frame(server, (ATResponse_t *)frame, length);
                break;
            case ID_TXSTATUS:
                handle_txstatus_frame(server, (TXStatus_t *)frame, length);
                break;
            default:
                break;
            }
                
            XbeeFrame_release(&server->mailbox);
        }
    }
}

static void handle_ipv4_frame(Server_t *server, IPV4RX_header_t *frame, int length)
{
    Socket_t *sock = server->sockets;
#ifdef MULTI_SOCKETS
    Socket_t *free = NULL;
#endif
    int len, cnt, i;
    uint8_t *ptr;
    
#ifdef SOCKET_DEBUG
    printf("ip %d.%d.%d.%d, dst %d, src %d, protocol %d\n", 
            frame->srcaddr[0],
            frame->srcaddr[1],
            frame->srcaddr[2],
            frame->srcaddr[3],
            (frame->dstport[0] << 8) | frame->dstport[1],
            (frame->srcport[0] << 8) | frame->srcport[1],
            frame->protocol);
#endif
    
    /* find a socket for this connection */
    for (i = 0; i < MAX_SOCKETS; ++i) {
        if (sock->flags & SF_BUSY) {
            if (memcmp(&sock->id, &frame->srcaddr, sizeof(sock->id)) == 0)
                break;
        }
#ifdef MULTI_SOCKETS
        else if (!free)
            free = sock;
#endif
        ++sock;
    }
    
    /* check for needing to open a new socket */
    if (i >= MAX_SOCKETS) {
#ifdef MULTI_SOCKETS
        if (!(sock = free)) {
            printf("No free sockets\n");
            return; // no sockets available, ignore frame
        }
#else
        sock = &server->sockets[0];
#endif
        sock->server = server;
        sock->flags |= SF_BUSY;
        memcpy(&sock->id, frame->srcaddr, sizeof(sock->id));
        sock->protocol = frame->protocol;
        sock->state = SS_REQUEST;
        sock->handler = NULL;
        sock->length = 0;
        sock->i = 0;
    }
#ifdef SOCKET_DEBUG
    printf("Using socket %d\n", sock - server->sockets);
#endif
    
    /* setup the frame parsing variables */
    ptr = frame->data;
    len = length - field_offset(IPV4RX_header_t, data);
    
    /* process the data in this frame */
    while (len > 0) {
        switch (sock->state) {
        case SS_REQUEST:
        case SS_HEADER:
            while (len > 0 && *ptr != '\r' && *ptr != '\n') {
                if (sock->i < MAX_CONTENT)
                    sock->content[sock->i++] = *ptr;
                ++ptr;
                --len;
            }
            if (len > 0) {
                int term = *ptr;
                sock->content[sock->i] = '\0';
                ++ptr;
                --len;
                if (sock->i == 0) {
                    if (sock->state == SS_REQUEST) {
                        printf("Missing request line\n");
                    }
                    else {
#ifdef STATE_MACHINE_DEBUG
                        printf("Found content\n");
#endif
                        if (term == '\r')
                            sock->state = SS_CONTENT_NL;
                        else {
                            if (sock->handler) {
                                sock->frame_ptr = ptr;
                                sock->frame_len = len;
                                (*sock->handler)(sock, HP_CONTENT_START);
                            }
                            sock->state = SS_CONTENT;
                        }
                    }
                }
                else {
#ifdef STATE_MACHINE_DEBUG
                    printf("Found %s: %s\n", sock->state == SS_REQUEST ? "request" : "header", sock->content);
#endif
                    switch (sock->state) {
                    case SS_REQUEST:
                        parse_request(sock);
                        break;
                    case SS_HEADER:
                        parse_header(sock);
                        break;
                    }
                    sock->state = (term == '\r' ? SS_SKIP_NL : SS_HEADER);
                }
                sock->i = 0;
            }
            break;
        case SS_SKIP_NL:
        case SS_CONTENT_NL:
            if (*ptr == '\n') {
                ++ptr;
                --len;
            }
            if (sock->state == SS_CONTENT_NL && sock->handler) {
                sock->frame_ptr = ptr;
                sock->frame_len = len;
                (*sock->handler)(sock, HP_CONTENT_START);
            }
            sock->state = (sock->state == SS_SKIP_NL ? SS_HEADER : SS_CONTENT);
            break;
        case SS_CONTENT:
            cnt = sock->length - sock->i;
#ifdef STATE_MACHINE_DEBUG
            printf("length %d, remaining: %d\n", sock->length, cnt);
#endif
            if (len < cnt)
                cnt = len;
            memcpy(&sock->content[sock->i], ptr, cnt);
            sock->i += cnt;
            ptr += cnt;
            len -= cnt;
            if (len > 0 && sock->i >= sock->length)
                handle_content(sock);
            break;
        }
    }
    
    if (sock->i >= sock->length)
        handle_content(sock);
}

static void parse_request(Socket_t *sock)
{
    MethodBinding_t *binding = NULL;
    printf("\nRequest: %s\n", sock->content);
    for (binding = sock->server->methodBindings; binding->method != NULL; ++binding) {
        if (match(sock, binding->method)) {
#ifdef REQUEST_DEBUG
            printf(" -- found handler for %s\n", binding->method);
#endif
            sock->handler = binding->handler;
            (*sock->handler)(sock, HP_REQUEST);
            break;
        }
    }
}

static void parse_header(Socket_t *sock)
{
    char *p;
    if ((p = match(sock, "CONTENT-LENGTH:")) != NULL) {
        sock->length = atoi(skip_spaces(p));
#ifdef REQUEST_DEBUG
        printf("Length: %d\n", sock->length);
#endif
    }
    if (sock->handler)
        (*sock->handler)(sock, HP_HEADER);
}

static void handle_content(Socket_t *sock)
{
    if (sock->handler)
        (*sock->handler)(sock, HP_CONTENT);
    else
        http_send_response(sock, (uint8_t *)BAD_REQUEST_RESPONSE, sizeof(BAD_REQUEST_RESPONSE) - 1);
    sock->state = SS_REQUEST;
    sock->handler = NULL;
    sock->length = 0;
    sock->i = 0;
}

static void send_at_command(Server_t *server, int id, char *fmt, ...)
{
    uint8_t frame[32];
    ATCommand_t *atcmd = (ATCommand_t *)frame;
    va_list ap;
    int len;

    atcmd->apiid = ID_ATCOMMAND;
    atcmd->frameid = id;

    va_start(ap, fmt);
    len = field_offset(ATCommand_t, command) + vsprintf((char *)atcmd->command, fmt, ap);
    va_end(ap);

#ifdef AT_DEBUG
    printf("AT frame:");
    show_frame(frame, len);
    printf("AT '%s'", atcmd->command);
#endif

    XbeeFrame_sendframe(&server->mailbox, frame, len);
    
#ifdef AT_DEBUG
    printf(" -- sent\n");
#endif
}

static void handle_atresponse_frame(Server_t *server, ATResponse_t *frame, int length)
{
    if (frame->status == 0x00) {
        if (strncmp((char *)frame->command, "MY", 2) == 0)
            printf("IP Address: %d.%d.%d.%d\n", frame->value[0], frame->value[1], frame->value[2], frame->value[3]);
    }
}

static void handle_txstatus_frame(Server_t *server, TXStatus_t *frame, int length)
{
#ifndef STATUS_DEBUG
    if (frame->status != 0x00)
#endif
        printf("TX Status: Frame %02x, ", frame->frameid);
    switch (frame->status) {
#ifdef STATUS_DEBUG
    case 0x00:  printf("Success\n"); break;
#else
    case 0x00:  break;
#endif
    case 0x03:  printf("Transmission purged\n"); break;
    case 0x04:  printf("Physical error\n"); break;
    case 0x21:  printf("TX64 transmission timed out\n"); break;
    case 0x32:  printf("Resource error\n"); break;
    case 0x74:  printf("Message too long\n"); break;
    case 0x76:  printf("Attempt to create client socket failed\n"); break;
    case 0x77:  printf("TCP connection does not exist\n"); break;
    case 0x78:  printf("Source port on UDP transmission doesn't match listening port\n"); break;
    default:    printf("Unknown status %02x\n", frame->status); break;
    }
}

void http_send_response(Socket_t *sock, uint8_t *data, int length)
{
    uint8_t frame[1024];
    length = prepare_response(sock, frame, data, length);
    XbeeFrame_sendframe(&sock->server->mailbox, frame, length);
    sock->flags &= ~SF_BUSY;
}

static int prepare_response(Socket_t *sock, uint8_t *frame, uint8_t *data, int length)
{
    IPV4TX_header_t *txhdr = (IPV4TX_header_t *)frame;
    char *ptr = (char *)data;
    int len = length;
    
    printf("Response: ");
    while (--len >= 0 && *ptr != '\r')
        putchar(*ptr++);
    putchar('\n');

    txhdr->apiid = ID_IPV4TX;
    txhdr->frameid = ++sock->server->txframeid;
    memcpy(&txhdr->dstaddr, &sock->id.srcaddr, 4);
    memcpy(&txhdr->dstport, &sock->id.srcport, 2);
    memcpy(&txhdr->srcport, &sock->id.dstport, 2);
    txhdr->protocol = sock->protocol;
    //txhdr->options = 0x00; // don't terminate after send
    txhdr->options = 0x01; // terminate after send
    memcpy(txhdr->data, data, length);
    length += field_offset(IPV4TX_header_t, data);

#ifdef FRAME_TX_DEBUG
    printf("[TX %02x]", sock->server->txframeid);
    show_frame(frame, length);
#endif
    
    return length;
}

static char *match(Socket_t *sock, char *str)
{
    char *p = (char *)sock->content;
    while (*str != '\0' && *p != '\0') {
        if (*str != toupper(*p))
            return NULL;
        ++str;
        ++p;
    }
    return p;
}

static char *skip_spaces(char *str)
{
    while (*str != '\0' && isspace(*str))
        ++str;
    return str;
}

#if defined(AT_DEBUG) || defined(FRAME_RX_DEBUG) || defined(FRAME_TX_DEBUG)
static void show_frame(uint8_t *frame, int length)
{
    int i;
    for (i = 0; i < length; ++i)
        printf(" %02x", frame[i]);
    putchar('\n');
#ifdef FRAME_DEBUG_TEXT
    printf("     \"");
    for (i = 0; i < length; ++i) {
        if (frame[i] >= 0x20 && frame[i] <= 0x7e)
            putchar(frame[i]);
        else
            printf("<%02x>", frame[i]);
        if (frame[i] == '\n')
            putchar('\n');
    }
    printf("\"\n");
#endif
}
#endif

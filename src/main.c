#include "socket_server.h"
#include "relay_module.h"
#include "relay_states.h"
#include "message.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#define TELNET_PORT 23
#define MESSAGE_INACTIVITY_TIMEOUT_SECONDS 20

static void process_commands(int const command_fd, relay_module_info_st const * const relay_module_info)
{
    int relay_fd = -1;
    fd_set fds;
    unsigned int num_fds;
    bool had_error = false;

    for (; !had_error;)
    {
        int sockets_waiting;
        struct timeval timeout;

        timeout.tv_sec = MESSAGE_INACTIVITY_TIMEOUT_SECONDS;
        timeout.tv_usec = 0;

        FD_ZERO(&fds);
        FD_SET(command_fd, &fds);
        num_fds = command_fd + 1; 

        sockets_waiting = TEMP_FAILURE_RETRY(select(num_fds, &fds, NULL, NULL, &timeout));
        fprintf(stderr, "sockets waiting: %d\n", sockets_waiting);
        if (sockets_waiting == -1)
        {
            had_error = true;
        }
        else if (sockets_waiting == 0) /* Timeout. */
        {
            relay_module_disconnect(relay_fd);
            relay_fd = -1;
        }
        else
        {
            int msg_sock = -1;

            msg_sock = TEMP_FAILURE_RETRY(accept(command_fd, 0, 0));
            if (msg_sock == -1)
            {
                continue;
            }
            process_new_command(msg_sock, relay_module_info, &relay_fd);
        }
    }

    relay_module_disconnect(relay_fd);

    return;
}

static void relay_worker(relay_module_info_st const * const relay_module_info)
{
    int command_socket = -1;

    command_socket = listen_on_unix_socket("LIGHTING_REQUESTS", true);
    if (command_socket < 0)
    {
        goto done;
    }

    process_commands(command_socket, relay_module_info);

done:
    close_connection_to_unix_socket(command_socket);
}

static void relay_module_info_get(relay_module_info_st * const relay_module_info, 
                                  char const * const module_address)
{
    int16_t const module_port = TELNET_PORT;
    char const * const module_username = "admin";
    char const * const module_password = "admin";

    relay_module_info->address = module_address;
    relay_module_info->port = module_port;
    relay_module_info->username = module_username;
    relay_module_info->password = module_password;
}

int main(int argc, char * * argv)
{
    relay_module_info_st relay_module_info;

    if (argc < 2)
    {
        fprintf(stderr, "Format: %s <module address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    relay_module_info_get(&relay_module_info, argv[1]);
    for (;;)
    {
        relay_worker(&relay_module_info);

        usleep(500000); /* This is just so that we don't retry failing connections too quickly. */
    }

    return 0;
}

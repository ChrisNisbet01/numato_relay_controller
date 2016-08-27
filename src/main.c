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
#define MAXIMUM_SECONDS_BETWEEN_RELAY_MODULE_UPDATES 120

typedef struct message_handler_info_st
{
    relay_module_info_st const * relay_module_info;
    int * relay_fd;
} message_handler_info_st;

typedef struct relay_state_ctx_st
{
    relay_states_st * current_states;
    time_t last_written;
} relay_state_ctx_st;

static void set_state_handler(void * const user_info, relay_states_st * const desired_relay_states);

static relay_state_ctx_st relay_state_ctx;

static message_handler_st const message_handlers =
{
    .set_state_handler = set_state_handler
};

static bool need_to_update_module(relay_state_ctx_st const * const relay_state_ctx,
                                  unsigned int const writeall_bitmask)
{
    bool need_to_write_states;

    if (relay_state_ctx->current_states == NULL)
    {
        /* True if the relay states haven't been updated yet. */
        need_to_write_states = true;
    }
    else if (writeall_bitmask != relay_states_get_states_bitmask(relay_state_ctx->current_states))
    {
        need_to_write_states = true;
    }
    else if (writeall_bitmask != 0
             && difftime(time(NULL), relay_state_ctx->last_written) >= MAXIMUM_SECONDS_BETWEEN_RELAY_MODULE_UPDATES)
    {
        /* This will ensure that if the relay module restarts and we 
         * want some relays to be on that we'll always turn them on 
         * again within a few minutes, assuming we can communicate 
         * with the module. 
         */
        need_to_write_states = true;
    }
    else
    {
        need_to_write_states = false;
    }

    return need_to_write_states;
}

static void relay_states_update_module(relay_state_ctx_st * const relay_state_ctx,
                                relay_states_st const * const relay_states,
                                relay_module_info_st const * const relay_module_info,
                                int * const relay_fd)
{
    unsigned int writeall_bitmask;
    bool need_to_write_states;
    relay_states_st * desired_states;

    desired_states = relay_states_combine(relay_state_ctx->current_states, relay_states);
    if (desired_states == NULL)
    {
        goto done;
    }
    writeall_bitmask = relay_states_get_states_bitmask(desired_states);

    need_to_write_states = need_to_update_module(relay_state_ctx, writeall_bitmask);

    if (need_to_write_states)
    {
        if (!update_relay_module(writeall_bitmask, relay_module_info, relay_fd))
        {
            goto done;
        }
        /* Update the current states after the new states have been 
         * successfully written to the module. 
         */
        relay_states_free(relay_state_ctx->current_states);
        relay_state_ctx->current_states = desired_states;
        desired_states = NULL;
        relay_state_ctx->last_written = time(NULL);
    }

done:
    relay_states_free(desired_states);

    return;
}

static void set_state_handler(void * const user_info, relay_states_st * const desired_relay_states)
{
    message_handler_info_st * info = user_info;

    relay_states_update_module(&relay_state_ctx, 
                               desired_relay_states, 
                               info->relay_module_info, 
                               info->relay_fd);

    fprintf(stderr, "\n");

}

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
            message_handler_info_st info;

            msg_sock = TEMP_FAILURE_RETRY(accept(command_fd, 0, 0));
            if (msg_sock == -1)
            {
                continue;
            }
            info.relay_fd = &relay_fd;
            info.relay_module_info = relay_module_info;

            process_new_command(msg_sock, &message_handlers, &info);
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

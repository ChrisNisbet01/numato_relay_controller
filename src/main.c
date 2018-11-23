#include "relay_module.h"
#include "relay_states.h"
#include "daemonize.h"
#include "debug.h"
#include "ubus.h"
#include "ubus_server.h"
#include "message.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
 
/* Numato relay controller default username and password. 
 * Inlcuded in this file for reference, but should always be 
 * passed in on the command line. 
 */
#define DEFAULT_USERNAME "admin"
#define DEFAULT_PASSWORD "admin"

#define TELNET_PORT 23

/* The socket to the relay controller module will be closed once
 * the incoming message socket has been idel for this period of 
 * time.
 */
#define MESSAGE_INACTIVITY_TIMEOUT_SECONDS 20

/* The relay module is usually only updated when the relay 
 * states need to change. They will also be updated if there 
 * has been no update for this period of time. This will 
 * ensure that if the relay module has been reset for some 
 * reason (e.g. power outage) that it will be updated again 
 * within a failry short amount of time. 
 */
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

static int relay_fd = -1;
static message_handler_info_st message_handler_info;

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

static void relay_states_update_module(relay_states_st const * const relay_states,
                                       relay_module_info_st const * const relay_module_info,
                                       int * const relay_fd)
{
    unsigned int writeall_bitmask;
    relay_states_st * desired_states;

    /* Overlay the desired states with the current states. The new 
     * request may not want to change the states of all the 
     * relays. 
     */
    desired_states = relay_states_combine(relay_state_ctx.current_states, relay_states);
    if (desired_states == NULL)
    {
        goto done;
    }
    writeall_bitmask = relay_states_get_states_bitmask(desired_states);

    if (need_to_update_module(&relay_state_ctx, writeall_bitmask))
    {
        if (!update_relay_module(writeall_bitmask, relay_module_info, relay_fd))
        {
            goto done;
        }
        /* Update the current states after the new states have been 
         * successfully written to the module. 
         */
        relay_states_free(relay_state_ctx.current_states);
        relay_state_ctx.current_states = desired_states;
        desired_states = NULL;

        /* Save the time when the states were last written. This is used 
         * to periodically check if the states need to be forcibly 
         * updated even if no desired states are changed. 
         */
        relay_state_ctx.last_written = time(NULL);
    }

done:
    relay_states_free(desired_states);

    return;
}

static void set_state_handler(void * const user_info, relay_states_st * const desired_relay_states)
{
    message_handler_info_st * info = user_info;

    relay_states_update_module(desired_relay_states, 
                               info->relay_module_info, 
                               info->relay_fd);
}

static void relay_module_info_init(relay_module_info_st * const relay_module_info,
                                   char const * const module_address,
                                   uint16_t const module_port,
                                   char const * const username,
                                   char const * const password)
{
    relay_module_info->address = module_address;
    relay_module_info->port = module_port;
    relay_module_info->username = username;
    relay_module_info->password = password;
}

static void usage(char const * const program_name)
{
    fprintf(stdout, "Usage: %s [options] <GPIO module address> <username> <password>\n", program_name);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -d %-21s %s\n", "", "Run as a daemon");
    fprintf(stdout, "  -s %-21s %s\n", "ubus socket", "Ubus socket path");
}

int main(int argc, char * * argv)
{
    relay_module_info_st relay_module_info;
    bool daemonise = false;
    int daemonise_result;
    int exit_code;
    unsigned int const min_args = 3;
    unsigned int args_remaining;
    int option;
    char const * listening_socket_name = NULL;

    while ((option = getopt(argc, argv, "s:?d")) != -1)
    {
        switch (option)
        {
            case 'd':
                daemonise = true;
                break;
            case 's':
                listening_socket_name = argv[optind];
                break;
            case '?':
                usage(basename(argv[0]));
                exit_code = EXIT_SUCCESS;
                goto done;
        }
    }

    args_remaining = argc - optind;
    if (args_remaining < min_args)
    {
        usage(basename(argv[0]));
        exit_code = EXIT_FAILURE;
        goto done;
    }

    relay_module_info_init(&relay_module_info,
                           argv[optind], 
                           TELNET_PORT,
                           argv[optind + 1],
                           argv[optind + 2]
                           );

    if (daemonise)
    {
        daemonise_result = daemonize(NULL, NULL, NULL);
        if (daemonise_result < 0)
        {
            fprintf(stderr, "Failed to daemonise. Exiting\n");
            exit_code = EXIT_FAILURE;
            goto done;
        }
        if (daemonise_result == 0)
        {
            /* This is the parent process, which can exit now. */
            exit_code = EXIT_SUCCESS;
            goto done;
        }
    }

    struct ubus_context * const ubus_ctx = ubus_initialise(listening_socket_name);

    if (ubus_ctx == NULL)
    {
        DPRINTF("\r\nfailed to initialise UBUS\n");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    message_handler_info.relay_fd = &relay_fd;
    message_handler_info.relay_module_info = &relay_module_info;

    bool const ubus_server_initialised = 
        ubus_server_initialise(
            ubus_ctx, 
            &message_handlers, 
            &message_handler_info);

    if (!ubus_server_initialised)
    {
        DPRINTF("\r\nfailed to initialise UBUS server\n");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    uloop_run();

    uloop_done();

    ubus_done();
    ubus_server_done(); 

    exit_code = EXIT_SUCCESS;

done:
    exit(exit_code);
}

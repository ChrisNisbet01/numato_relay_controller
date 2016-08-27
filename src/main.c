#include "socket_server.h"
#include "relay_module.h"
#include "relay_states.h"

#include <get_char_with_timeout.h>

#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#define TELNET_PORT 23
#define NUM_RELAYS 8
#define JSON_MESSAGE_READ_TIMEOUT_SECONDS 5
#define MAXIMUM_SECONDS_BETWEEN_RELAY_MODULE_UPDATES 120
#define MESSAGE_INACTIVITY_TIMEOUT_SECONDS 20

typedef struct relay_module_info_st
{
    char const * address;
    uint16_t port;
    char const * username;
    char const * password;
} relay_module_info_st; 

typedef struct relay_state_ctx_st
{
    bool states_written;
    relay_states_st current_states;
    time_t last_written;
} relay_state_ctx_st;

static relay_state_ctx_st relay_state_ctx;

static json_object * read_json_from_stream(int const fd, unsigned int const read_timeout_seconds)
{
    struct json_tokener * tok;
    json_object * obj = NULL;
    enum json_tokener_error error = json_tokener_continue;
    int get_char_result;

    tok = json_tokener_new();

    do
    {
        char buf[2];

        get_char_result = get_char_with_timeout(fd, read_timeout_seconds, &buf[0]);

        if (get_char_result == sizeof buf[0])
        {
            buf[1] = '\0';

            obj = json_tokener_parse_ex(tok, buf, 1);
            error = tok->err;
        }

    }
    while (obj == NULL && get_char_result > 0 && error == json_tokener_continue);

    json_tokener_free(tok);

    return obj;
}

static bool update_relay_module(unsigned int const writeall_bitmask,
                                relay_module_info_st const * const relay_module_info,
                                int * const relay_fd)
{
    bool updated_states;

    if (*relay_fd == -1)
    {
        fprintf(stderr, "need to connect to relay module\n");
        *relay_fd = relay_module_connect(relay_module_info->address,
                                         relay_module_info->port,
                                         relay_module_info->username,
                                         relay_module_info->password);
        if (*relay_fd == -1)
        {
            fprintf(stderr, "failed to connect to relay module\n");
            updated_states = false;
            goto done;
        }
    }
    if (!relay_module_set_all_relay_states(*relay_fd, writeall_bitmask))
    {
        fprintf(stderr, "Failed to update module. Closing socket\n");
        relay_module_disconnect(*relay_fd);
        *relay_fd = -1;
        updated_states = false;
        goto done;
    }

    updated_states = true; 

done:
    return updated_states;
}

static bool need_to_update_module(relay_state_ctx_st const * const relay_state_ctx, 
                                  unsigned int const writeall_bitmask)
{
    bool need_to_write_states;

    if (writeall_bitmask != relay_states_get_writeall_bitmask(&relay_state_ctx->current_states))
    {
        need_to_write_states = true;
    }
    else if (!relay_state_ctx->states_written)
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
    relay_states_st desired_states;

    relay_states_combine(&relay_state_ctx->current_states, relay_states, &desired_states);

    writeall_bitmask = relay_states_get_writeall_bitmask(&desired_states);

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
        relay_state_ctx->current_states = desired_states;
        relay_state_ctx->states_written = true;
        relay_state_ctx->last_written = time(NULL);
    }

done:
    return;
}

static bool parse_zone(json_object * const zone, unsigned int * const relay_id, bool * const state)
{
    bool parsed_zone;
    json_object * object;
    char const * state_value;

    json_object_object_get_ex(zone, "state", &object);
    if (object == NULL)
    {
        parsed_zone = false;
        goto done;
    }
    state_value = json_object_get_string(object);

    json_object_object_get_ex(zone, "id", &object);
    if (object == NULL)
    {
        parsed_zone = false;
        goto done;
    }
    *relay_id = json_object_get_int(object);

    if (strcasecmp(state_value, "on") == 0)
    {
        *state = true;
    }
    else if (strcasecmp(state_value, "off") == 0)
    {
        *state = false;
    }
    else
    {
        parsed_zone = false;
        goto done;
    }
    parsed_zone = true;

done:
    return parsed_zone;
}

static void process_zone(json_object * const zone, relay_states_st * const relay_states)
{
    bool state;
    unsigned int relay_id;

    if (!parse_zone(zone, &relay_id, &state))
    {
        goto done;
    }

    fprintf(stderr, "set relay %d state %s\n", relay_id, state ? "on" : "off");

    relay_states_set_state(relay_states, relay_id, state);

done:
    return;
}

static bool populate_relay_states_from_message(json_object * const message, relay_states_st * const relay_states)
{
    bool relay_states_populated;
    json_object * params;
    json_object * zones_array;
    int num_zones;
    int index;

    json_object_object_get_ex(message, "params", &params);
    if (params == NULL)
    {
        relay_states_populated = false;
        goto done;
    }
    json_object_object_get_ex(params, "zones", &zones_array);

    if (json_object_get_type(zones_array) != json_type_array)
    {
        relay_states_populated = false;
        goto done;
    }
    num_zones = json_object_array_length(zones_array);


    relay_states_init(relay_states);

    for (index = 0; index < num_zones; index++)
    {
        json_object * const zone = json_object_array_get_idx(zones_array, index);

        process_zone(zone, relay_states);
    }

    relay_states_populated = true;

done:
    return relay_states_populated;
}

static void process_set_state_message(json_object * const message,
                                      relay_module_info_st const * const relay_module_info,
                                      int * const relay_fd)
{
    relay_states_st relay_states;

    fprintf(stderr, "processing 'set state' message\n");

    if (!populate_relay_states_from_message(message, &relay_states))
    {
        goto done;
    }
    relay_states_update_module(&relay_state_ctx, &relay_states, relay_module_info, relay_fd);

    fprintf(stderr, "\n");

done:
    return;
}

static void process_json_message(json_object * const message, 
                                 int const msg_fd, 
                                 relay_module_info_st const * const relay_module_info,
                                 int * const relay_fd)
{
    json_object * json_method;
    char const * method_string;

    fprintf(stderr, "processing json message\n");
    json_object_object_get_ex(message, "method", &json_method);
    if (json_method == NULL)
    {
        goto done;
    }
    method_string = json_object_get_string(json_method);

    if (strcasecmp(method_string, "set state") == 0)
    {
        process_set_state_message(message, relay_module_info, relay_fd);
    }

done:
    return;
}

static void process_new_command(int const msg_sock, 
                                relay_module_info_st const * const relay_module_info, 
                                int * const relay_fd)
{
    json_object * message = NULL;

    message = read_json_from_stream(msg_sock, JSON_MESSAGE_READ_TIMEOUT_SECONDS);

    if (message == NULL)
    {
        goto done;
    }

    process_json_message(message, msg_sock, relay_module_info, relay_fd);

done:
    if (msg_sock != -1)
    {
        close(msg_sock);
    }
    json_object_put(message);

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

int main(int argc, char * * argv)
{
    int16_t const module_port = TELNET_PORT;
    char const * const module_username = "admin";
    char const * const module_password = "admin";
    relay_module_info_st relay_module_info;

    if (argc < 2)
    {
        fprintf(stderr, "Format: %s <module address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    for (;;)
    {
        relay_module_info.address = argv[1];
        relay_module_info.port = module_port;
        relay_module_info.username = module_username;
        relay_module_info.password = module_password;

        relay_worker(&relay_module_info);

        usleep(500000); /* This is just so that we don't retry failing connections too quickly. */
    }

    return 0;
}

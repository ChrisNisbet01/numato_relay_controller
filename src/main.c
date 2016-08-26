#include "socket.h"
#include "read_line.h"
#include "socket_server.h"

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
#define PROMPT_WAIT_SECONDS 5
#define JSON_MESSAGE_READ_TIMEOUT_SECONDS 5
#define MAXIMUM_SECONDS_BETWEEN_RELAY_MODULE_UPDATES 120

typedef struct relay_module_info_st
{
    char const * address;
    uint16_t port;
    char const * username;
    char const * password;
} relay_module_info_st; 

#define BIT(x) (1UL << (x))

typedef struct relay_states_st
{
    bool states_written;
    unsigned int current_states; /* Bitmask of the current relay states. */
    time_t last_written;
} relay_states_st;

typedef struct relay_command_st
{
    unsigned int states_modified; /* Bitmask of relay outputs to be commanded */
    unsigned int desired_states; /* Bitmask of the desired states. */
} relay_command_st;

static relay_states_st relay_states;

static void relay_command_init(relay_command_st * const relay_command)
{
    relay_command->states_modified = 0;
    relay_command->desired_states = 0;
}

static void relay_command_set_state(relay_command_st * const relay_command, unsigned int relay_index, bool const state)
{
    relay_command->states_modified |= BIT(relay_index);
    if (state)
    {
        relay_command->desired_states |= BIT(relay_index);
    }
    else
    {
        relay_command->desired_states &= ~BIT(relay_index);
    }
}

static unsigned int get_writeall_command_bitmask(relay_states_st const * const relay_states, 
                                                 relay_command_st const * const relay_command)
{
    unsigned int writeall_bitmask;

    fprintf(stderr, 
            "current 0x%x, modified 0x%x desired 0x%x\n", 
            relay_states->current_states, 
            relay_command->states_modified, 
            relay_command->desired_states);

    /* First get the current states of the relays that weren't 
     * modified by the relay_command. 
     */
    writeall_bitmask = relay_states->current_states & ~relay_command->states_modified;
    /* Now assign the new desired states. */
    writeall_bitmask |= relay_command->desired_states;

    fprintf(stderr, "bitmask 0x%x\n", writeall_bitmask);

    return writeall_bitmask;
}


static bool read_until_string_found(int const sock_fd, char const * const string)
{
    bool string_found;
    char * line = NULL;
    size_t line_length = 0; 
    size_t const string_len = strlen(string);

    do
    {
        if (read_line_with_timeout(&line, &line_length, sock_fd, 5) < 0)
        {
            string_found = false;
            goto done;
        }
        if (strncmp(line, string, string_len) == 0)
        {
            string_found = true;
            goto done;
        }
    }
    while(1);

done:
    free(line);

    return string_found;
}

static bool wait_for_prompt(int const sock_fd, 
                            char const * const prompt,
                            unsigned int const maximum_wait_seconds)
{
    bool got_prompt;
    char ch;
    int read_result;
    char const * prompt_char = prompt;

    do
    {
        read_result = read_with_telnet_handling(sock_fd, &ch, 1, maximum_wait_seconds);
        if (read_result != 1)
        {
            got_prompt = false;
            goto done;
        }
        if (ch != *prompt_char)
        {
            prompt_char = prompt;
        }
        if (ch == *prompt_char)
        {
            prompt_char++;
            if (*prompt_char == '\0')
            {
                got_prompt = true;
                goto done;
            }
        }
        else
        {
            prompt_char = prompt;
        }
    }
    while (1);

    got_prompt = false;

done:
    return got_prompt;
}

static bool wait_for_telnet(int const sock_fd)
{
    bool done_telnet;

    do
    {
        int const read_result = read_with_telnet_handling(sock_fd, NULL, 1, 5);
        if (read_result == 0)
        {
            done_telnet = true;
            goto done;
        }
    }
    while (1);

    done_telnet = false;

done:
    return done_telnet;
}

static bool set_all_relay_states(int const sock_fd, unsigned int const writeall_bitmask)
{
    bool set_state;

    fprintf(stderr, "sending writeall command %x\n", writeall_bitmask);
    if (dprintf(sock_fd, "relay writeall %02x\r\n", writeall_bitmask) < 0)
    {
        set_state = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, ">", PROMPT_WAIT_SECONDS))
    {
        set_state = false;
        goto done;
    }
    set_state = true;

done:
    return set_state;
}

#if defined NEED_SET_RELAY_STATE
static bool set_relay_state(int const sock_fd, unsigned int const relay, bool const state)
{
    bool set_state;

    if (dprintf(sock_fd, "relay %s %u\r\n", state ? "on" : "off", relay) < 0)
    {
        set_state = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, ">", PROMPT_WAIT_SECONDS))
    {
        set_state = false;
        goto done;
    }
    set_state = true;

done:
    return set_state;
}
#endif

static bool relay_module_login(int const sock_fd, char const * const username, char const * const password)
{
    bool logged_in;

    if (!wait_for_prompt(sock_fd, "User Name: ", PROMPT_WAIT_SECONDS))
    {
        logged_in = false;
        goto done;
    }
    if (dprintf(sock_fd, "%s\r\n", username) < 0)
    {
        logged_in = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, "Password: ", PROMPT_WAIT_SECONDS))
    {
        logged_in = false;
        goto done;
    }

    /* For some reason the relay module chooses this point to 
     * issue some telnet commands, and fails authentication if we 
     * don't respond to it. 
     */
    if (!wait_for_telnet(sock_fd))
    {
        logged_in = false;
        goto done;
    }

    if (dprintf(sock_fd, "%s\r\n", password) < 0)
    {
        logged_in = false;
        goto done;
    }

    /* XXX - TODO - Also look for login failure message (access 
     * denied?).
     */
    if (!read_until_string_found(sock_fd, "Logged in successfully"))
    {
        logged_in = false;
        goto done;
    }

    if (!wait_for_prompt(sock_fd, ">", PROMPT_WAIT_SECONDS))
    {
        logged_in = false;
        goto done;
    }

    logged_in = true;

done:
    return logged_in;
}

static void relay_module_disconnect(int const relay_module_fd)
{
    if (relay_module_fd >= 0)
    {
        fprintf(stderr, "disconnecting from relay module\n");
        close(relay_module_fd);
    }
}

static int relay_module_connect(char const * const address,
                                int16_t const port, 
                                char const * const username, 
                                char const * const password)
{
    int sock_fd;

    fprintf(stderr, "connect to relay module\n");

    sock_fd = connect_to_socket(address, port);
    if (sock_fd < 0)
    {
        goto done;
    }
    fprintf(stderr, "connected\n");
    if (!relay_module_login(sock_fd, username, password))
    {
        close(sock_fd);
        sock_fd = -1;
        goto done;
    }

done:
    return sock_fd;
}

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

static void process_zone(json_object * const zone, relay_command_st * const relay_command)
{
    bool state;
    unsigned int relay_id;

    if (!parse_zone(zone, &relay_id, &state))
    {
        goto done;
    }

    fprintf(stderr, "set relay %d state %s\n", relay_id, state ? "on" : "off");

    relay_command_set_state(relay_command, relay_id, state);

done:
    return;
}

static bool populate_relay_command_from_message(json_object * const message, relay_command_st * const relay_command)
{
    bool relay_command_populated;
    json_object * params;
    json_object * zones_array;
    int num_zones;
    int index;

    json_object_object_get_ex(message, "params", &params);
    if (params == NULL)
    {
        relay_command_populated = false;
        goto done;
    }
    json_object_object_get_ex(params, "zones", &zones_array);

    if (json_object_get_type(zones_array) != json_type_array)
    {
        relay_command_populated = false;
        goto done;
    }
    num_zones = json_object_array_length(zones_array);


    relay_command_init(relay_command);

    for (index = 0; index < num_zones; index++)
    {
        json_object * const zone = json_object_array_get_idx(zones_array, index);

        process_zone(zone, relay_command);
    }

    relay_command_populated = true; 

done:
    return relay_command_populated;
}

static bool update_relay_states(relay_states_st * const relay_states,
                                unsigned int const writeall_bitmask,
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
    if (!set_all_relay_states(*relay_fd, writeall_bitmask))
    {
        updated_states = false;
        goto done;
    }

    updated_states = true; 

done:
    return updated_states;
}

static void relay_command_perform(relay_states_st * const relay_states,
                                  relay_command_st const * const relay_command,
                                  relay_module_info_st const * const relay_module_info,
                                  int * const relay_fd)
{
    unsigned int writeall_bitmask;
    bool need_to_write_states;

    writeall_bitmask = get_writeall_command_bitmask(relay_states, relay_command);

    if (writeall_bitmask != relay_states->current_states)
    {
        need_to_write_states = true;
    }
    else if (!relay_states->states_written)
    {
        need_to_write_states = true;
    }
    else if (writeall_bitmask != 0 
             && difftime(time(NULL), relay_states->last_written) >= MAXIMUM_SECONDS_BETWEEN_RELAY_MODULE_UPDATES)
    {
        fprintf(stderr, "inactivity_update\n");
        need_to_write_states = true;
    }
    else
    {
        need_to_write_states = false;
    }

    /* TODO: Update if the desired states are non-zero and it's 
     * been a few minutes since the last update to ensure that the 
     * relays are restored to the desired state if the module has 
     * reset or something. 
     */
    if (need_to_write_states)
    {
        if (!update_relay_states(relay_states, writeall_bitmask, relay_module_info, relay_fd))
        {
            goto done;
        }
        /* Update the current states after the new states have been 
         * successfully written to the module. 
         */
        relay_states->current_states = writeall_bitmask;
        relay_states->states_written = true;
        relay_states->last_written = time(NULL);
    }

done:
    return;
}

static void process_set_state_message(json_object * const message, 
                                      relay_module_info_st const * const relay_module_info,
                                      int * const relay_fd)
{
    relay_command_st relay_command;

    fprintf(stderr, "processing 'set state' message\n");

    if (!populate_relay_command_from_message(message, &relay_command))
    {
        goto done;
    }
    relay_command_perform(&relay_states, &relay_command, relay_module_info, relay_fd);

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

static void process_new_command(int const command_fd, 
                                relay_module_info_st const * const relay_module_info, 
                                int * const relay_fd)
{
    json_object * message = NULL;
    int msg_sock = -1;

    msg_sock = TEMP_FAILURE_RETRY(accept(command_fd, 0, 0));
    if (msg_sock == -1)
    {
        goto done;
    }

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

        timeout.tv_sec = 20;
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
            process_new_command(command_fd, relay_module_info, &relay_fd);
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

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

#define TELNET_PORT 23
#define NUM_RELAYS 8

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

static bool wait_for_prompt(int const sock_fd, char const * const prompt)
{
    bool got_prompt;
    char ch;
    int read_result;
    char const * prompt_char = prompt;

    do
    {
        read_result = read_with_telnet_handling(sock_fd, &ch, 1, 5);
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

static bool login_to_module(int const sock_fd, char const * const username, char const * const password)
{
    bool logged_in;

    if (!wait_for_prompt(sock_fd, "User Name: "))
    {
        logged_in = false;
        goto done; 
    }
    if (dprintf(sock_fd, "%s\r\n", username) < 0)
    {
        logged_in = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, "Password: "))
    {
        logged_in = false;
        goto done;
    }
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

    if (!read_until_string_found(sock_fd, "Logged in successfully"))
    {
        logged_in = false;
        goto done;
    }

    if (!wait_for_prompt(sock_fd, ">"))
    {
        logged_in = false;
        goto done;
    }

    logged_in = true;

done:
    return logged_in;
}

static bool set_relay_state(int const sock_fd, unsigned int const relay, bool const state)
{
    bool set_state;

    if (dprintf(sock_fd, "relay %s %u\r\n", state ? "on" : "off", relay) < 0)
    {
        set_state = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, ">"))
    {
        set_state = false;
        goto done;
    }
    set_state = true;

done:
    return set_state;
}

#if defined(SET_STATES_OFF_ON_STARTUP)
static bool set_all_relay_states(int const sock_fd, bool const state)
{
    bool set_state;

    if (dprintf(sock_fd, "relay writeall %s\r\n", state ? "ff" : "00") < 0)
    {
        set_state = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, ">"))
    {
        set_state = false;
        goto done;
    }
    set_state = true;

done:
    return set_state;
}
#endif

static int establish_connection_with_relay_module(char const * const address,
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
    if (!login_to_module(sock_fd, username, password))
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

static void process_json_message(json_object * const message, int const relay_fd)
{
    json_object * params;
    json_object * zones_array;
    int num_zones;
    int index;

    json_object_object_get_ex(message, "params", &params);
    if (params == NULL)
    {
        goto done;
    }
    json_object_object_get_ex(params, "zones", &zones_array);

    if (json_object_get_type(zones_array) != json_type_array)
    {
        goto done;
    }
    num_zones = json_object_array_length(zones_array);

    for (index = 0; index < num_zones; index++)
    {
        json_object * const zone = json_object_array_get_idx(zones_array, index);
        json_object * object;
        char const * state_value;
        char relay_id;

        json_object_object_get_ex(zone, "state", &object);
        if (object == NULL)
        {
            continue;
        }
        state_value = json_object_get_string(object);

        json_object_object_get_ex(zone, "id", &object);
        if (object == NULL)
        {
            continue;
        }
        relay_id = json_object_get_int(object);
        fprintf(stderr, "relay id %u to state %s\n", relay_id, state_value);
        if (strcasecmp(state_value, "on") == 0)
        {
            set_relay_state(relay_fd, relay_id, true);
        }
        else if (strcasecmp(state_value, "off") == 0)
        {
            set_relay_state(relay_fd, relay_id, false);
        }
    }

done:
    return;
}
static bool process_commands(int const command_fd, int const relay_fd)
{
    fd_set fds;
    unsigned int num_fds;
    bool had_error;

    FD_ZERO(&fds);
    FD_SET(command_fd, &fds);
    num_fds = command_fd + 1;

    /* XXX - TODO - reqork so that the connection to the relay 
     * module is only established once we received a message. 
     * If no message is received for a period of time we may as well 
     * close the connection to the relay module. 
     */
    for (;;)
    {
        int sockets_waiting;
        int msg_sock;
        json_object * message;
        sockets_waiting = TEMP_FAILURE_RETRY(select(num_fds, &fds, NULL, NULL, NULL));
        if (sockets_waiting == -1)
        {
            had_error = true;
            goto done;
        }

        msg_sock = TEMP_FAILURE_RETRY(accept(command_fd, 0, 0));
        if (msg_sock == -1)
        {
            had_error = true;
            goto done;
        }

        message = read_json_from_stream(msg_sock, 5);

        process_json_message(message, relay_fd);

        json_object_put(message);
    }

    had_error = false;

done:
    return had_error;
}

static void relay_worker(bool * const first_connection, char const * const module_address)
{
    int16_t module_port = TELNET_PORT;
    char const * const module_username = "admin";
    char const * const module_password = "admin";
    int sock_fd;
    int command_socket = -1;

    if (!(*first_connection))
    {
        usleep(500000);
    }
    *first_connection = false;

    sock_fd = establish_connection_with_relay_module(module_address, module_port, module_username, module_password);

    if (sock_fd < 0)
    {
        fprintf(stderr, "failed to connect to relay module\n");
        goto done;
    }

#if defined(SET_STATES_OFF_ON_STARTUP)
    if (!set_all_relay_states(sock_fd, false))
    {
        fprintf(stderr, "Error setting all relays off\n");
        goto done;
    }
#endif
    command_socket = listen_on_socket("LIGHTING_REQUESTS", true);
    fprintf(stderr, "command listener %d\n", command_socket);
    if (command_socket < 0)
    {
        goto done;
    }
    process_commands(command_socket, sock_fd);

done:
    if (command_socket >= 0)
    {
        close(command_socket);
    }
    if (sock_fd >= 0)
    {
        close(sock_fd);
    }
}

int main(int argc, char * * argv)
{
    bool first_connection = true;

    if (argc < 2)
    {
        fprintf(stderr, "Format: %s <module address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    for (;;)
    {
        relay_worker(&first_connection, argv[1]);
    }

    return 0;
}

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
#define PROMPT_WAIT_SECONDS 5

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

static void process_zone(json_object * const zone, int const relay_fd)
{
    bool state;
    unsigned int relay_id;

    if (!parse_zone(zone,&relay_id, &state))
    {
        goto done;
    }

    fprintf(stderr, "set relay %d state %s\n", relay_id, state ? "on" : "off");

    set_relay_state(relay_fd, relay_id, state);

done:
    return;
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

        process_zone(zone, relay_fd);
    }
    fprintf(stderr, "\n");

done:
    return;
}

typedef struct relay_module_info_st
{
    char const * address;
    uint16_t port;
    char const * username;
    char const * password;
} relay_module_info_st;

static bool process_commands(int const command_fd, relay_module_info_st const * const relay_module_info)
{
    int relay_fd = -1;
    fd_set fds;
    unsigned int num_fds;
    bool had_error;
    json_object * message = NULL; 

    FD_ZERO(&fds);
    FD_SET(command_fd, &fds);
    num_fds = command_fd + 1;

    for (;;)
    {
        int sockets_waiting;
        int msg_sock;
        struct timeval timeout;

        timeout.tv_sec = 60;
        timeout.tv_usec = 0;

        sockets_waiting = TEMP_FAILURE_RETRY(select(num_fds, &fds, NULL, NULL, &timeout));
        if (sockets_waiting == -1)
        {
            had_error = true;
            goto done;
        }
        else if (sockets_waiting == 0) /* Timeout. */
        {
            relay_module_disconnect(relay_fd);
            relay_fd = -1;
        }
        else
        {
            msg_sock = TEMP_FAILURE_RETRY(accept(command_fd, 0, 0));
            if (msg_sock == -1)
            {
                had_error = true;
                goto done;
            }

            message = read_json_from_stream(msg_sock, 5);
            close(msg_sock);

            if (message == NULL)
            {
                had_error = true;
                goto done;
            }

            if (relay_fd == -1)
            {
                relay_fd = relay_module_connect(relay_module_info->address, 
                                                relay_module_info->port, 
                                                relay_module_info->username, 
                                                relay_module_info->password);
                if (relay_fd == -1)
                {
                    fprintf(stderr, "failed to connect to relay module\n");
                    had_error = true;
                    goto done;
                }
            }
            if (relay_fd >= 0)
            {
                process_json_message(message, relay_fd);
            }
            json_object_put(message);
            message = NULL;
        }
    }

    had_error = false;

done:
    json_object_put(message);
    relay_module_disconnect(relay_fd);

    return had_error;
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

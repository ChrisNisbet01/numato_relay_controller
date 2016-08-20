#include "socket.h"
#include "read_line.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

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

static void relay_worker(bool * const first_connection)
{
    char const module_address[] = "192.168.1.32";
    int16_t module_port = TELNET_PORT;
    char const * const module_username = "admin";
    char const * const module_password = "admin";
    int sock_fd;
    int relay;

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

    if (!set_all_relay_states(sock_fd, false))
    {
        fprintf(stderr, "Error setting all relays off\n");
        goto done;
    }

    for (;;)
    {
        for (relay = 0; relay < NUM_RELAYS; relay++)
        {
            if (!set_relay_state(sock_fd, relay, true))
            {
                goto done;
            }
            usleep(800000);
            if (!set_relay_state(sock_fd, relay, false))
            {
                goto done;
            }
        }
    }

done:
    if (sock_fd >= 0)
    {
        close(sock_fd);
    }
}

int main(int argc, char * * argv)
{
    bool first_connection = true;

    for (;;)
    {
        relay_worker(&first_connection);
    }

    return 0;
}
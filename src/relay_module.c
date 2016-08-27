#include "relay_module.h"
#include "read_write.h"
#include "socket.h"

#include <stdio.h>
#include <unistd.h>

#define PROMPT_WAIT_SECONDS 5

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

bool relay_module_set_all_relay_states(int const sock_fd, unsigned int const writeall_bitmask)
{
    bool set_states;

    fprintf(stderr, "sending writeall command %02x\n", writeall_bitmask);
    if (dprintf(sock_fd, "relay writeall %02x\r\n", writeall_bitmask) < 0)
    {
        set_states = false;
        goto done;
    }
    if (!wait_for_prompt(sock_fd, ">", PROMPT_WAIT_SECONDS))
    {
        set_states = false;
        goto done;
    }
    set_states = true;

done:
    return set_states;
}

#if defined NEED_SET_RELAY_STATE
bool relay_module_set_relay_state(int const sock_fd, unsigned int const relay, bool const state)
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

void relay_module_disconnect(int const relay_module_fd)
{
    if (relay_module_fd >= 0)
    {
        fprintf(stderr, "disconnecting from relay module\n");
        close(relay_module_fd);
    }
}

int relay_module_connect(char const * const address,
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



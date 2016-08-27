#ifndef __RELAY_MODULE_H__
#define __RELAY_MODULE_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct relay_module_info_st
{
    char const * address;
    uint16_t port;
    char const * username;
    char const * password;
} relay_module_info_st; 

bool relay_module_set_all_relay_states(int const sock_fd, unsigned int const writeall_bitmask);
#if defined NEED_SET_RELAY_STATE
bool relay_module_set_relay_state(int const sock_fd, unsigned int const relay, bool const state);
#endif
void relay_module_disconnect(int const relay_module_fd);
int relay_module_connect(char const * const address,
                         int16_t const port,
                         char const * const username,
                         char const * const password);

#endif /* __RELAY_MODULE_H__ */

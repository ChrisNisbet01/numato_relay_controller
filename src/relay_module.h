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

void relay_module_disconnect(int const relay_module_fd);
bool update_relay_module(unsigned int const writeall_bitmask,
                         relay_module_info_st const * const relay_module_info,
                         int * const relay_fd);

#endif /* __RELAY_MODULE_H__ */

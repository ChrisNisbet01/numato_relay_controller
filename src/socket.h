#ifndef __SOCKET_H__
#define __SOCKET_H__

#include <stdint.h>

int connect_to_socket(char const * const socket_name, uint16_t const port);

#endif /* __SOCKET_H__ */

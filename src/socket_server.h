#ifndef __SOCKET_SERVER_H__
#define __SOCKET_SERVER_H__

#include <stdbool.h>

int listen_on_socket(char const * const socket_name, bool use_abstract_namespace);

#endif /* __SOCKET_SERVER_H__ */

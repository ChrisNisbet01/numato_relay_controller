#ifndef __SOCKET_SERVER_H__
#define __SOCKET_SERVER_H__

#include <stdbool.h>

int listen_on_unix_socket(char const * const socket_name, bool const use_abstract_namespace);
void close_connection_to_unix_socket(int const sock_fd);

#endif /* __SOCKET_SERVER_H__ */

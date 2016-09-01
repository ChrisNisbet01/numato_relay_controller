#include "socket_server.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


static int set_socket_name(char * dest, size_t maxsize, char const * const name, bool const use_abstract_namepsace)
{
    char const nul = '\0';
    int len;
    int result;

    if (name == NULL || *name == nul)
    {
        result = -1;
        goto done;
    }

    len = strlen(name);

    if (use_abstract_namepsace == true)
    {
        maxsize--;
        dest[0] = nul;
        dest++;
        len++;
    }

    if (maxsize < len + 1)
    {
        result = -1;
        goto done;
    }
    strncpy(dest, name, maxsize);

    result = len;

done:
    return result;
}

int listen_on_unix_socket(char const * const socket_name, bool const use_abstract_namespace)
{
    int sock;
    bool had_error;
    struct sockaddr_un server;
    int len;

    sock = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (sock < 0)
    {
        fprintf(stderr, "No socket\n");
        had_error = true;
        goto done;
    }
    memset(&server, 0, sizeof server);
    server.sun_family = PF_LOCAL;

    len = set_socket_name(server.sun_path, sizeof server.sun_path, socket_name, use_abstract_namespace);
    if (len < 0)
    {
        fprintf(stderr, "set_socket_name_failed\n");
        had_error = true;
        goto done;
    }
    len += sizeof server.sun_family;

    unlink(socket_name);    /* remove any previous instances of the socket (non-abstract namespace only?) */

    if (bind(sock, (struct sockaddr *)&server, len))
    {
        fprintf(stderr, "bind failed\n");
        had_error = true;
        goto done;
    }

    if (listen(sock, 5) == -1)
    {
        fprintf(stderr, "listen failed\n");
        had_error = true;
        goto done;
    }

    had_error = false;

done:
    if (had_error)
    {
        close(sock);
        sock = -1;
    }

    return sock;
}

void close_connection_to_unix_socket(int const sock_fd)
{
    if (sock_fd >= 0)
    {
        close(sock_fd);
    }
}


#include "read_line.h"
#include <get_char_with_timeout.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/telnet.h>
#include <stdbool.h>
#include <stdio.h>

#define MIN_ALLOC 64

static bool do_telnet_negotiation(int const sock_fd, unsigned char * const buf, unsigned int const buf_len)
{
    bool negotiation_succeeded;
    int i;

    for (i = 0; i < buf_len; i++)
    {
        if (buf[i] == DO)
        {
            buf[i] = WONT;
        }
        else if (buf[i] == WILL)
        {
            buf[i] = DO;
        }
    }

    if (write(sock_fd, buf, buf_len) < 0)
    {
        negotiation_succeeded = false;
        goto done;
    }

    negotiation_succeeded = true;

done:
    return negotiation_succeeded;
}

ssize_t read_with_telnet_handling(int const fd, void * const buf, size_t const buf_len, unsigned int const timeout_seconds)
{
    int bytes_read;
    unsigned char * out = buf;

    bytes_read = 0;
    while (bytes_read < buf_len)
    {
        int get_char_result;
        unsigned char ch;

        get_char_result = get_char_with_timeout(fd, timeout_seconds, (char *)&ch);
        if (get_char_result != 1)
        {
            if (bytes_read == 0)
            {
                bytes_read = get_char_result;
            }
            goto done;
        }
        if (ch == IAC)
        {
            unsigned char iac_buf[3];
            ssize_t len;

            iac_buf[0] = ch;
            // read 2 more bytes
            len = TEMP_FAILURE_RETRY(read(fd, &iac_buf[1], sizeof iac_buf - 1));
            if (len <= 0)
            {
                bytes_read = -1;
                goto done;
            }
            if (!do_telnet_negotiation(fd, iac_buf, sizeof iac_buf))
            {
                bytes_read = -1;
                goto done;
            }
            if (out == NULL)
            {
                goto done;
            }
        }
        else
        {
            if (out != NULL)
            {
                *out = ch;
                out++;
            }
            bytes_read++;
        }
    }

done:
    return bytes_read;
}

int read_line_with_timeout(char * * output_buffer, size_t * output_buffer_size, int fd, unsigned int timeout_seconds)
{
    size_t total_bytes_read;
    char const terminator = '\n';
    char const nul = '\0';
    int readline_result;

    if (output_buffer_size == NULL || output_buffer == NULL || fd < 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* allocate a buffer if none was supplied */
    if (*output_buffer == NULL)
    {
        *output_buffer_size = MIN_ALLOC;
        *output_buffer = malloc(*output_buffer_size);
        if (*output_buffer == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
    }

    total_bytes_read = 0;

    for (;;)
    {
        char ch;
        int get_char_result;

        get_char_result = read_with_telnet_handling(fd, &ch, 1, timeout_seconds);
        if (get_char_result == -1)
        {
            readline_result = -1;
            goto done;
        }

        /* No error. Was anything read? */
        if (get_char_result == 0)      /* EOF */
        {
            /* The final line in a file doesn't need to be terminated with 
             * '\n'. 
             */
            if (total_bytes_read > 0)
            {
                break;
            }
            readline_result = -1;
            goto done;
        }
        /* read a character */

        /* Ensure we have space to write it to the output buffer */
        if (total_bytes_read >= *output_buffer_size - 1)    /* must be space for the new char and the NUL terminator */
        {
            *output_buffer_size *= 2;
            *output_buffer = realloc(*output_buffer, *output_buffer_size);
            if (*output_buffer == NULL)
            {
                errno = ENOMEM;
                readline_result = -1;
                goto done;
            }
        }

        /* add it to the output buffer */
        (*output_buffer)[total_bytes_read] = ch;
        total_bytes_read++;

        if (ch == terminator)
        {
            break;
        }
    }

    (*output_buffer)[total_bytes_read] = nul;
    readline_result = total_bytes_read;

done:
    return readline_result;
}


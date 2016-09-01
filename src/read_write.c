#include "read_write.h"
#include "read_line.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

bool read_until_string_found(int const sock_fd, char const * const success_string, char const * const failure_string)
{
    bool string_found;
    char * line = NULL;
    size_t line_length = 0;
    size_t const success_string_len = strlen(success_string);
    size_t const failure_string_len = strlen(failure_string);
    time_t start_time = time(NULL);
    unsigned int const timeout_seconds = 5;

    do
    {
        time_t check_time;

        if (read_line_with_timeout(&line, &line_length, sock_fd, timeout_seconds) < 0)
        {
            string_found = false;
            goto done;
        }

        /* Ensure the total time taken hasn't been too long. */
        check_time = time(NULL);
        if ((check_time - start_time) >= timeout_seconds)
        {
            string_found = false;
            goto done;
        }

        if (strncmp(line, success_string, success_string_len) == 0)
        {
            string_found = true;
            goto done;
        }
        if (failure_string != NULL)
        {
            if (strncmp(line, failure_string, failure_string_len) == 0)
            {
                string_found = false;
                goto done;
            }
        }
    }
    while (1);

done:
    free(line);

    return string_found;
}

bool wait_for_prompt(int const sock_fd,
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

bool wait_for_telnet(int const sock_fd)
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



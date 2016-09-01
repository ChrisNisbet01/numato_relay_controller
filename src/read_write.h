#ifndef __READ_WRITE_H__
#define __READ_WRITE_H__

#include <stdbool.h>

bool read_until_string_found(int const sock_fd, char const * const success_string, char const * const failure_string);
bool wait_for_prompt(int const sock_fd,
                     char const * const prompt,
                     unsigned int const maximum_wait_seconds);
bool wait_for_telnet(int const sock_fd);

#endif /* __READ_WRITE_H__ */

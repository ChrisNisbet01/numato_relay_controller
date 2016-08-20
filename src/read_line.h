#ifndef __READ_LINE_H__
#define __READ_LINE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

ssize_t read_with_telnet_handling(int const fd, void * const buf, size_t const buf_len, unsigned int const timeout_seconds);
int read_line_with_timeout(char * * output_buffer, size_t * output_buffer_size, int fd, unsigned int timeout_seconds);

#endif /*  __READ_LINE_H__ */

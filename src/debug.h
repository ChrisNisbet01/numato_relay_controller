#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>

#define DPRINTF(format, ...) \
    do \
    { \
        fprintf(stderr, "%s(%d): " format, __func__, __LINE__, ## __VA_ARGS__); \
        fflush(stderr); \
    } while(0)


#endif /* __DEBUG_H__ */

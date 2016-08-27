#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include "relay_module.h"

void process_new_command(int const msg_sock,
                         relay_module_info_st const * const relay_module_info,
                         int * const relay_fd);

#endif /* __MESSAGE_H__ */

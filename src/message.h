#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include "message_handler.h"

void process_new_request(int const msg_sock,
                         message_handler_st const * const handler,
                         void * const user_info);

#endif /* __MESSAGE_H__ */

#ifndef __UBUS_SERVER_H__
#define __UBUS_SERVER_H__

#include "message_handler.h"

#include <libubus.h>
#include <stdbool.h>

bool
ubus_server_initialise(
    struct ubus_context * const ctx,
    message_handler_st const * const handlers_in,
    void * const user_info_in);

void
ubus_server_done(void);


#endif /* __UBUS_SERVER_H__ */

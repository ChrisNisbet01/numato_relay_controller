#ifndef __UBUS_SERVER_H__
#define __UBUS_SERVER_H__

#include <libubus.h>
#include <stdbool.h>

bool
ubus_server_initialise(
    struct ubus_context * const ctx);

void
ubus_server_done(void);


#endif /* __UBUS_SERVER_H__ */

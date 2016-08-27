#ifndef __MESSAGE_HANDLER_H__
#define __MESSAGE_HANDLER_H__

#include "relay_states.h"

typedef void (* set_state_handler_fn)(void * const user_info, relay_states_st * const desired_relay_states);

typedef struct message_handler_st
{
    set_state_handler_fn set_state_handler;
} message_handler_st;

#endif /* __MESSAGE_HANDLER_H__ */

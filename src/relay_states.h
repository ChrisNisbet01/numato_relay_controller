#ifndef __RELAY_STATES_H__
#define __RELAY_STATES_H__

#include <stdbool.h>

typedef struct relay_states_st
{
    unsigned int states_modified; /* Bitmask indicating which bits in desired_states have meaning. */
    unsigned int desired_states; /* Bitmask of the desired states. */
} relay_states_st; 

void relay_states_init(relay_states_st * const relay_states);
void relay_states_set_state(relay_states_st * const relay_states, unsigned int relay_index, bool const state);
void relay_states_combine(relay_states_st const * const previous_relay_states,
                          relay_states_st const * const new_relay_states,
                          relay_states_st * const output_relay_states);
unsigned int relay_states_get_writeall_bitmask(relay_states_st const * const relay_states);


#endif /* __RELAY_STATES_H__ */

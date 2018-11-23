#ifndef __RELAY_STATES_H__
#define __RELAY_STATES_H__

#include <stddef.h>
#include <stdbool.h>

typedef struct relay_states_st relay_states_st;

void relay_states_init(relay_states_st * const relay_states);
relay_states_st * relay_states_create(void);
void relay_states_free(relay_states_st * const relay_states);

void relay_states_set_state(relay_states_st * const relay_states, unsigned int relay_index, bool const state);
relay_states_st * relay_states_combine(relay_states_st const * const previous_relay_states,
                                       relay_states_st const * const new_relay_states);
unsigned int relay_states_get_states_bitmask(relay_states_st const * const relay_states);

size_t numato_num_inputs(void);

size_t numato_num_outputs(void);

#endif /* __RELAY_STATES_H__ */

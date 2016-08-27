#include "relay_states.h"

#define BIT(x) (1UL << (x))

void relay_states_init(relay_states_st * const relay_states)
{
    relay_states->states_modified = 0;
    relay_states->desired_states = 0;
}

void relay_states_set_state(relay_states_st * const relay_states, unsigned int relay_index, bool const state)
{
    relay_states->states_modified |= BIT(relay_index);
    if (state)
    {
        relay_states->desired_states |= BIT(relay_index);
    }
    else
    {
        relay_states->desired_states &= ~BIT(relay_index);
    }
}

void relay_states_combine(relay_states_st const * const previous_relay_states,
                          relay_states_st const * const new_relay_states,
                          relay_states_st * const output_relay_states)
{

    output_relay_states->desired_states = previous_relay_states->desired_states & ~new_relay_states->states_modified;
    output_relay_states->desired_states |= new_relay_states->desired_states;
    output_relay_states->states_modified = previous_relay_states->states_modified | new_relay_states->states_modified;
}

unsigned int relay_states_get_writeall_bitmask(relay_states_st const * const relay_states)
{
    return relay_states->desired_states;
}



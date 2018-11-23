#include "relay_states.h"

#include <stddef.h>
#include <stdlib.h>

#define BIT(x) (1UL << (x))

struct relay_states_st
{
    unsigned int states_modified; /* Bitmask indicating which bits in desired_states have meaning. */
    unsigned int desired_states; /* Bitmask of the desired states. */
}; 

void relay_states_init(relay_states_st * const relay_states)
{
    if (relay_states == NULL)
    {
        goto done;
    }

    relay_states->states_modified = 0;
    relay_states->desired_states = 0;

done:
    return;
}

relay_states_st * relay_states_create(void)
{
    relay_states_st * const relay_states = malloc(sizeof *relay_states);

    if (relay_states == NULL)
    {
        goto done;
    }

    relay_states_init(relay_states);

done:
    return relay_states;
}

void relay_states_free(relay_states_st * const relay_states)
{
    free(relay_states);
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

relay_states_st * relay_states_combine(relay_states_st const * const previous_relay_states,
                                       relay_states_st const * const new_relay_states)
{
    relay_states_st * combined_relay_states = relay_states_create();

    if (combined_relay_states == NULL)
    {
        goto done;
    }
    if (previous_relay_states == NULL)
    {
        /* Nothing to combine. Just take the new states. */
        combined_relay_states->desired_states = new_relay_states->desired_states; 
        combined_relay_states->states_modified = new_relay_states->states_modified;
    }
    else
    {
        combined_relay_states->desired_states = previous_relay_states->desired_states & ~new_relay_states->states_modified;
        combined_relay_states->desired_states |= new_relay_states->desired_states;
        combined_relay_states->states_modified = previous_relay_states->states_modified | new_relay_states->states_modified;
    }

done:
    return combined_relay_states;
}

unsigned int relay_states_get_states_bitmask(relay_states_st const * const relay_states)
{
    return relay_states->desired_states;
}

size_t numato_num_inputs(void)
{
    return 0;
}

size_t numato_num_outputs(void)
{
    return 8;
}

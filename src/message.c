#include "message.h"
#include "relay_states.h"
#include "message_handler.h"

#include <get_char_with_timeout.h>

#include <json-c/json.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define JSON_MESSAGE_READ_TIMEOUT_SECONDS 5

static char const relay_params_array_name[] = "relays";
static char const relay_state_field_name[] = "state";
static char const relay_id_field_name[] = "id";
static char const relay_state_on_string[] = "on";
static char const relay_state_off_string[] = "off";
static char const relay_method_set_state_string[] = "set state"; 

static json_object * read_json_from_stream(int const fd, unsigned int const read_timeout_seconds)
{
    struct json_tokener * tok;
    json_object * obj = NULL;
    enum json_tokener_error error = json_tokener_continue;
    int get_char_result;

    tok = json_tokener_new();

    do
    {
        char buf[2];

        get_char_result = get_char_with_timeout(fd, read_timeout_seconds, &buf[0]);

        if (get_char_result == sizeof buf[0])
        {
            buf[1] = '\0';

            obj = json_tokener_parse_ex(tok, buf, 1);
            error = tok->err;
        }

    }
    while (obj == NULL && get_char_result > 0 && error == json_tokener_continue);

    json_tokener_free(tok);

    return obj;
}

static bool parse_zone(json_object * const zone, unsigned int * const relay_id, bool * const state)
{
    bool parsed_zone;
    json_object * object;
    char const * state_value;

    json_object_object_get_ex(zone, relay_state_field_name, &object);
    if (object == NULL)
    {
        parsed_zone = false;
        goto done;
    }
    state_value = json_object_get_string(object);

    json_object_object_get_ex(zone, relay_id_field_name, &object);
    if (object == NULL)
    {
        parsed_zone = false;
        goto done;
    }
    *relay_id = json_object_get_int(object);

    if (strcasecmp(state_value, relay_state_on_string) == 0)
    {
        *state = true;
    }
    else if (strcasecmp(state_value, relay_state_off_string) == 0)
    {
        *state = false;
    }
    else
    {
        parsed_zone = false;
        goto done;
    }
    parsed_zone = true;

done:
    return parsed_zone;
}

static void process_zone(json_object * const zone, relay_states_st * const relay_states)
{
    bool state;
    unsigned int relay_id;

    if (!parse_zone(zone, &relay_id, &state))
    {
        goto done;
    }

    relay_states_set_state(relay_states, relay_id, state);

done:
    return;
}

static relay_states_st * get_desired_relay_states_from_message(json_object * const message)
{
    bool relay_states_populated;
    json_object * params;
    json_object * zones_array;
    int num_zones;
    int index;
    relay_states_st * relay_states = NULL;

    json_object_object_get_ex(message, "params", &params);
    if (params == NULL)
    {
        relay_states_populated = false;
        goto done;
    }
    json_object_object_get_ex(params, relay_params_array_name, &zones_array);

    if (json_object_get_type(zones_array) != json_type_array)
    {
        relay_states_populated = false;
        goto done;
    }
    num_zones = json_object_array_length(zones_array);

    relay_states = relay_states_create();
    if (relay_states == NULL)
    {
        relay_states_populated = false;
        goto done;
    }

    for (index = 0; index < num_zones; index++)
    {
        json_object * const zone = json_object_array_get_idx(zones_array, index);

        process_zone(zone, relay_states);
    }

    relay_states_populated = true;

done:
    if (!relay_states_populated)
    {
        relay_states_free(relay_states);
        relay_states = NULL;
    }

    return relay_states;
}

static void process_set_state_message(json_object * const message,
                                      message_handler_st const * const handlers,
                                      void * const user_info)
{
    relay_states_st * relay_states;

    relay_states = get_desired_relay_states_from_message(message);
    if (relay_states == NULL)
    {
        goto done;
    }

    if (handlers->set_state_handler != NULL)
    {
        handlers->set_state_handler(user_info, relay_states);
    }

done:
    relay_states_free(relay_states);

    return;
}

static void process_json_message(json_object * const message,
                                 int const msg_fd,
                                 message_handler_st const * const handlers,
                                 void * const user_info)
{
    json_object * json_method;
    char const * method_string;

    /* msg_fd is passed so that in the future we can write 
     * responses back to the sender. 
     */

    json_object_object_get_ex(message, "method", &json_method);
    if (json_method == NULL)
    {
        goto done;
    }
    method_string = json_object_get_string(json_method);

    if (strcasecmp(method_string, relay_method_set_state_string) == 0)
    {
        process_set_state_message(message, handlers, user_info);
    }

done:
    return;
}

void process_new_request(int const msg_sock,
                         message_handler_st const * const handlers,
                         void * const user_info)
{
    json_object * request = NULL;

    request = read_json_from_stream(msg_sock, JSON_MESSAGE_READ_TIMEOUT_SECONDS);

    if (request == NULL)
    {
        goto done;
    }

    process_json_message(request, msg_sock, handlers, user_info);

done:
    json_object_put(request);
}



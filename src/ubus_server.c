#include "ubus_server.h"
#include "ubus_private.h"
#include "debug.h"
#include "relay_states.h"

#include <libubox/blobmsg.h>

#include <string.h>
#include <stdio.h>

static char const gpio_object_name[] = "numato.gpio";
static char const gpio_object_type_name[] = "gpio";
static char const gpio_get_method_name[] = "get";
static char const gpio_set_method_name[] = "set";
static char const gpio_count_name[] = "count";
static char const gpio_io_type_str[] = "io type";
static char const gpio_io_type_bi[] = "bi";
static char const gpio_io_type_bo[] = "bo"; 

static char const pin_str[] = "pin";
static char const state_str[] = "state";
static char const result_str[] = "result";

struct ubus_context * ubus_ctx;

enum
{
    GPIO_COUNT_TYPE,
    __GPIO_COUNT_MAX
};

static struct blobmsg_policy const gpio_count_policy[__GPIO_COUNT_MAX] = {
    [GPIO_COUNT_TYPE] = { .name = gpio_io_type_str, .type = BLOBMSG_TYPE_STRING }
};

enum
{
    GPIO_GET_PIN,
    __GPIO_GET_MAX
};

static struct blobmsg_policy const gpio_get_policy[__GPIO_GET_MAX] = {
    [GPIO_GET_PIN] = {.name = pin_str, .type = BLOBMSG_TYPE_INT32}
};

enum
{
    GPIO_SET_PIN,
    GPIO_SET_STATE,
    __GPIO_SET_MAX
};

static struct blobmsg_policy const gpio_set_policy[__GPIO_SET_MAX] = {
    [GPIO_SET_PIN] = { .name = pin_str, .type = BLOBMSG_TYPE_INT32 },
    [GPIO_SET_STATE] = { .name = state_str, .type = BLOBMSG_TYPE_BOOL }
};

static int
gpio_set_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    int result;
    struct blob_attr * tb[__GPIO_SET_MAX];
    struct blob_buf b; 

    blobmsg_parse(gpio_set_policy,
                  ARRAY_SIZE(gpio_set_policy),
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    if (tb[GPIO_SET_PIN] == NULL || tb[GPIO_SET_STATE] == NULL)
    {
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    uint32_t const pin = blobmsg_get_u32(tb[GPIO_SET_PIN]);
    bool const state = blobmsg_get_bool(tb[GPIO_SET_STATE]);

    (void)pin;
    (void)state;
    /* XXX - TODO: Update the state of the relay here. */
    bool const success = true;

    local_blob_buf_init(&b, 0);

    blobmsg_add_u8(&b, result_str, success);

    ubus_send_reply(ctx, req, b.head);

    result = 0;

done:
    return result;
}

static int
gpio_get_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    int result;
    struct blob_attr * tb[__GPIO_GET_MAX];
    struct blob_buf b; 

    blobmsg_parse(gpio_get_policy,
                  ARRAY_SIZE(gpio_get_policy),
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    if (tb[GPIO_GET_PIN] == NULL)
    {
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    uint32_t const pin = blobmsg_get_u32(tb[GPIO_GET_PIN]);
    bool state;

    (void)pin;

    /* XXX - TODO: get teh input state (are there any?) here. */
    state = false;
    bool const success = true;

    local_blob_buf_init(&b, 0);

    blobmsg_add_u8(&b, result_str, success);
    if (success)
    {
        blobmsg_add_u8(&b, state_str, state);
    }

    ubus_send_reply(ctx, req, b.head);

    result = 0;

done:
    return result;
}

static int
gpio_count_handler(
    struct ubus_context * ctx,
    struct ubus_object * obj,
    struct ubus_request_data * req,
    const char * method,
    struct blob_attr * msg)
{
    int result;
    struct blob_attr * tb[__GPIO_COUNT_MAX];
    struct blob_buf b;

    blobmsg_parse(gpio_count_policy,
                  ARRAY_SIZE(gpio_count_policy),
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    if (tb[GPIO_COUNT_TYPE] == NULL)
    {
        result = UBUS_STATUS_INVALID_ARGUMENT;
        goto done;
    }

    char const * const io_type = blobmsg_get_string(tb[GPIO_COUNT_TYPE]);
    int count;

    if (strcmp(io_type, gpio_io_type_bi) == 0)
    {
        count = numato_num_inputs();
    }
    else if (strcmp(io_type, gpio_io_type_bo) == 0)
    {
        count = numato_num_outputs();
    }
    else
    {
        count = 0;
    }

    local_blob_buf_init(&b, 0);

    blobmsg_add_u32(&b, io_type, count);

    ubus_send_reply(ctx, req, b.head);

    result = 0;

done:
    return result;
}

static bool
gpio_add_object(
    struct ubus_context * const ctx,
    struct ubus_object * const obj)
{
    int const ret = ubus_add_object(ctx, obj);

    if (ret != UBUS_STATUS_OK)
    {
        DPRINTF("Failed to publish object '%s': %s\n",
                obj->name,
                ubus_strerror(ret));
    }

    return ret == UBUS_STATUS_OK;
}

static struct ubus_method gpio_object_methods[] = {
    UBUS_METHOD(gpio_get_method_name, gpio_get_handler, gpio_get_policy),
    UBUS_METHOD(gpio_set_method_name, gpio_set_handler, gpio_set_policy),
    UBUS_METHOD(gpio_count_name, gpio_count_handler, gpio_count_policy)
};

static struct ubus_object_type gpio_object_type =
    UBUS_OBJECT_TYPE(gpio_object_type_name, gpio_object_methods);

static struct ubus_object gpio_object =
{
    .name = gpio_object_name,
    .type = &gpio_object_type,
    .methods = gpio_object_methods,
    .n_methods = ARRAY_SIZE(gpio_object_methods)
};

bool
ubus_server_initialise(
    struct ubus_context * const ctx)
{
    ubus_ctx = ctx;

    return gpio_add_object(ubus_ctx, &gpio_object);
}

void
ubus_server_done(void)
{
    ubus_ctx = NULL;
}



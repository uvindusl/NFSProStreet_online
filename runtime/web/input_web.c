#include "input_proto.h"
#include "host_abi.h"

#include <stddef.h>

#if defined(__wasm__) && __has_attribute(export_name)
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

static volatile NfspsInputState g_input;

_Static_assert(sizeof(NfspsInputState) == 20, "input mailbox ABI changed");

void prostreet_input_reset(void);

WASM_EXPORT("prostreet_input_init")
int prostreet_input_init(void)
{
    prostreet_input_reset();
    return nfsu2_register(HOST_KIND_INPUT, (void *)&g_input,
                          (uint32_t)sizeof g_input);
}

WASM_EXPORT("prostreet_input_ptr")
uintptr_t prostreet_input_ptr(void)
{
    return (uintptr_t)&g_input;
}

WASM_EXPORT("prostreet_input_size")
uint32_t prostreet_input_size(void)
{
    return (uint32_t)sizeof g_input;
}

void prostreet_input_reset(void)
{
    g_input.buttons = 0;
    g_input.pad_buttons = 0;
    g_input.steering = 0;
    g_input.throttle = 0;
    g_input.brake = 0;
    g_input.connected = 0;
    ++g_input.sequence;
}

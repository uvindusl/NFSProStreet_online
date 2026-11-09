#include "input_proto.h"

#include <stddef.h>

#if defined(__wasm__) && __has_attribute(export_name)
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

static volatile NfspsInputState g_input;

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

#ifndef NFSPS_INPUT_PROTO_H
#define NFSPS_INPUT_PROTO_H

#include <stdint.h>

enum {
    NFSPS_INPUT_UP = 1u << 0,
    NFSPS_INPUT_DOWN = 1u << 1,
    NFSPS_INPUT_LEFT = 1u << 2,
    NFSPS_INPUT_RIGHT = 1u << 3,
    NFSPS_INPUT_CONFIRM = 1u << 4,
    NFSPS_INPUT_BACK = 1u << 5,
    NFSPS_INPUT_SHIFT = 1u << 6,
    NFSPS_INPUT_HANDBRAKE = 1u << 7
};

typedef struct {
    uint32_t buttons;
    uint32_t pad_buttons;
    int16_t steering;
    int16_t throttle;
    int16_t brake;
    uint16_t connected;
    uint32_t sequence;
} NfspsInputState;

#endif

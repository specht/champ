const uint16_t WATCH_COUNT = 9;
#define WATCH_U8 1
#define WATCH_U16 2
#define WATCH_S8 3
#define WATCH_S16 4
const char* WATCH_LABELS[] = {
    "SINX",
    "COSX",
    "SINY",
    "COSY",
    "SINZ",
    "COSZ",
    "RX",
    "RY",
    "RZ"
};
const uint16_t WATCH_ADDRESSES[] = {
    0x007b,
    0x007c,
    0x007d,
    0x007e,
    0x007f,
    0x0080,
    0x008d,
    0x008f,
    0x0091
};
const uint8_t WATCH_TYPES[] = {
    3,
    3,
    3,
    3,
    3,
    3,
    4,
    4,
    4
};
const uint16_t ARG_WATCH_COUNT = 2;
const uint16_t ARG_WATCH_ADDRESSES[] = {
    0x7c92,
    0x7c92
};
const uint8_t ARG_WATCH_TYPES[] = {
    3,
    3
};
#define WATCH_A 1
#define WATCH_X 2
#define WATCH_Y 3
const uint8_t ARG_WATCH_REGISTERS[] = {
    3,
    1
};
const char* WATCH_REGISTER_LABELS[] = {
    "",
    "A",
    "X",
    "Y"
};

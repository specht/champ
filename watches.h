const uint16_t WATCH_COUNT = 3;
#define WATCH_U8 1
#define WATCH_U16 2
#define WATCH_S8 3
#define WATCH_S16 4
const char* WATCH_LABELS[] = {
    "RX",
    "RY",
    "RZ"
};
const uint16_t WATCH_ADDRESSES[] = {
    0x008d,
    0x008f,
    0x0091
};
const uint8_t WATCH_TYPES[] = {
    4,
    4,
    4
};

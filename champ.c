#include <X11/Xlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "labels.h"
#include "watches.h"

#define SCALE 4
#define SCREEN_WIDTH 280
#define SCREEN_HEIGHT 192
#define SCREEN_REFRESH_EVERY_CYCLES 10000

#define CARRY               0x01
#define ZERO                0x02
#define INTERRUPT_DISABLE   0x04
#define DECIMAL_MODE        0x08
// #define INTERRUPT_VECTORING 0x10
#define OVERFLOW            0x40
#define NEGATIVE            0x80

Display *dsp;
Window win;
GC gc;
unsigned int white, black;
Atom wmDelete;
XEvent evt;
KeyCode keyQ;

uint8_t show_screen = 0;
uint8_t show_log = 1;
uint8_t show_call_stack = 0;

uint8_t trace_stack[0x100];
uint8_t trace_stack_pointer = 0xff;
uint16_t trace_stack_function[0x100];
uint64_t cycles_per_function[0x10000];
uint64_t calls_per_function[0x10000];
uint64_t last_frame_cycle_count = 0;
uint64_t frame_cycle_count = 0;
uint64_t frame_count = 0;

uint16_t start_pc = 0x6000;
uint16_t start_frame_pc = 0xffff;
uint64_t max_frames = 0;
FILE *watches_file = 0;

int init_display()
{
    dsp = XOpenDisplay(NULL);
    if (!dsp)
        return 1;

    int screen = DefaultScreen(dsp);
    white = WhitePixel(dsp, screen);
    black = BlackPixel(dsp, screen);

    win = XCreateSimpleWindow(dsp, DefaultRootWindow(dsp), 0, 0,
                              SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE,
                              0, black, black );

    wmDelete = XInternAtom(dsp, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(dsp, win, &wmDelete, 1);

    gc = XCreateGC(dsp, win, 0, NULL);

//     XSetForeground(dsp, gc, white);

    long eventMask = StructureNotifyMask;
    eventMask |= ButtonPressMask|ButtonReleaseMask|KeyPressMask|KeyReleaseMask;
    XSelectInput(dsp, win, eventMask);

    keyQ = XKeysymToKeycode(dsp, XStringToKeysym("Q"));

    XMapWindow(dsp, win);

    // wait until window appears
    do { XNextEvent(dsp,&evt); } while (evt.type != MapNotify);
}

uint16_t yoffset[192] = {
    0x0000, 0x0400, 0x0800, 0x0c00, 0x1000, 0x1400, 0x1800, 0x1c00,
    0x0080, 0x0480, 0x0880, 0x0c80, 0x1080, 0x1480, 0x1880, 0x1c80,
    0x0100, 0x0500, 0x0900, 0x0d00, 0x1100, 0x1500, 0x1900, 0x1d00,
    0x0180, 0x0580, 0x0980, 0x0d80, 0x1180, 0x1580, 0x1980, 0x1d80,
    0x0200, 0x0600, 0x0a00, 0x0e00, 0x1200, 0x1600, 0x1a00, 0x1e00,
    0x0280, 0x0680, 0x0a80, 0x0e80, 0x1280, 0x1680, 0x1a80, 0x1e80,
    0x0300, 0x0700, 0x0b00, 0x0f00, 0x1300, 0x1700, 0x1b00, 0x1f00,
    0x0380, 0x0780, 0x0b80, 0x0f80, 0x1380, 0x1780, 0x1b80, 0x1f80,
    0x0028, 0x0428, 0x0828, 0x0c28, 0x1028, 0x1428, 0x1828, 0x1c28,
    0x00a8, 0x04a8, 0x08a8, 0x0ca8, 0x10a8, 0x14a8, 0x18a8, 0x1ca8,
    0x0128, 0x0528, 0x0928, 0x0d28, 0x1128, 0x1528, 0x1928, 0x1d28,
    0x01a8, 0x05a8, 0x09a8, 0x0da8, 0x11a8, 0x15a8, 0x19a8, 0x1da8,
    0x0228, 0x0628, 0x0a28, 0x0e28, 0x1228, 0x1628, 0x1a28, 0x1e28,
    0x02a8, 0x06a8, 0x0aa8, 0x0ea8, 0x12a8, 0x16a8, 0x1aa8, 0x1ea8,
    0x0328, 0x0728, 0x0b28, 0x0f28, 0x1328, 0x1728, 0x1b28, 0x1f28,
    0x03a8, 0x07a8, 0x0ba8, 0x0fa8, 0x13a8, 0x17a8, 0x1ba8, 0x1fa8,
    0x0050, 0x0450, 0x0850, 0x0c50, 0x1050, 0x1450, 0x1850, 0x1c50,
    0x00d0, 0x04d0, 0x08d0, 0x0cd0, 0x10d0, 0x14d0, 0x18d0, 0x1cd0,
    0x0150, 0x0550, 0x0950, 0x0d50, 0x1150, 0x1550, 0x1950, 0x1d50,
    0x01d0, 0x05d0, 0x09d0, 0x0dd0, 0x11d0, 0x15d0, 0x19d0, 0x1dd0,
    0x0250, 0x0650, 0x0a50, 0x0e50, 0x1250, 0x1650, 0x1a50, 0x1e50,
    0x02d0, 0x06d0, 0x0ad0, 0x0ed0, 0x12d0, 0x16d0, 0x1ad0, 0x1ed0,
    0x0350, 0x0750, 0x0b50, 0x0f50, 0x1350, 0x1750, 0x1b50, 0x1f50,
    0x03d0, 0x07d0, 0x0bd0, 0x0fd0, 0x13d0, 0x17d0, 0x1bd0, 0x1fd0
};

typedef struct {
    uint16_t ip;
    uint8_t sp;
    uint64_t total_cycles;
    uint8_t a, x, y, flags;
} r_cpu;

void init_cpu(r_cpu* cpu)
{
    cpu->ip = 0;
    cpu->sp = 0xff;
    cpu->total_cycles = 0;
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->flags = 0x20; // bit 5 is always set
}

uint8_t ram[0x10000];
r_cpu cpu;

void load(char* path, uint16_t offset)
{
    uint8_t buffer[0x10000];
    size_t size;
    FILE* f;
    f = fopen(path, "rw");
    if (!f)
    {
        fprintf(stderr, "Error reading file: %s\n", path);
        exit(1);
    }
    size = fread(buffer, 1, 0x10000, f);
    fprintf(stderr, "Read 0x%04x bytes from %s, saved to 0x%04x.\n", (int)size, path, offset);
    memcpy(ram + offset, buffer, size);
    fclose(f);
}

uint8_t rip8()
{
    return ram[cpu.ip++];
}

uint16_t rip16()
{
    uint16_t result = rip8();
    result |= ((uint16_t)rip8()) << 8;
    return result;
}

uint8_t read8(uint16_t address)
{
    return ram[address];
}

uint16_t read16(uint16_t address)
{
    uint16_t result = read8(address);
    result |= ((uint16_t)read8(address + 1)) << 8;
    return result;
}

void refresh_watches(uint16_t address)
{
    for (int i = 0; i < WATCH_COUNT; i++)
    {
        if ((WATCH_ADDRESSES[i] == address) ||
            ((WATCH_TYPES[i] == WATCH_U16 || WATCH_TYPES[i] == WATCH_S16) &&
                (WATCH_ADDRESSES[i] == address + 1)))
        {
            int32_t value = 0;
            switch (WATCH_TYPES[i])
            {
                case WATCH_U8:
                    value = (uint8_t)ram[address];
                    break;
                case WATCH_S8:
                    value = (int8_t)ram[address];
                    break;
                case WATCH_U16:
                    value = (uint16_t)ram[address];
                    break;
                case WATCH_S16:
                    value = (int16_t)ram[address];
                    break;
            }
            fprintf(watches_file, "0x%04x %s %d\n", cpu.ip, WATCH_LABELS[i], value);
        }
    }
}

void write8(uint16_t address, uint8_t value)
{
    ram[address] = value;
    if (watches_file)
        refresh_watches(address);
}

void write16(uint16_t address, uint16_t value)
{
    ram[address] = value & 0xff;
    ram[address + 1] = (value >> 8) & 0xff;
    if (watches_file)
        refresh_watches(address);
}

void push(uint8_t value)
{
    if (cpu.sp == 0)
    {
        fprintf(stderr, "Stack overflow!\n");
        exit(1);
    }
    ram[(uint16_t)cpu.sp + 0x100] = value;
    cpu.sp--;
}

uint8_t pop()
{
    if (cpu.sp == 0xff)
    {
        fprintf(stderr, "Stack underrun!\n");
        exit(1);
    }
    cpu.sp++;
    return ram[(uint16_t)cpu.sp + 0x100];
}

void set_flag(int which, int value)
{
    cpu.flags &= ~which;
    if (value)
        cpu.flags |= which;
}

void update_zero_and_negative_flags(uint8_t value)
{
    set_flag(ZERO, value == 0);
    set_flag(NEGATIVE, value & 0x80);
}

uint8_t test_flag(int which)
{
    return ((cpu.flags & which) == 0) ? 0 : 1;
}

void cmp(uint8_t a, uint8_t b)
{
    set_flag(CARRY, a >= b);
    set_flag(ZERO, a == b);
    set_flag(NEGATIVE, (a - b) & 0x80);
}

uint8_t rol(uint8_t x)
{
    uint8_t old_carry = test_flag(CARRY);
    set_flag(CARRY, x & 0x80);
    x <<= 1;
    if (old_carry)
        x |= 1;
    return x;
}

uint8_t ror(uint8_t x)
{
    uint8_t old_carry = test_flag(CARRY);
    set_flag(CARRY, x & 1);
    x >>= 1;
    if (old_carry)
        x |= 0x80;
    return x;
}

void adc(uint8_t value)
{
    uint16_t t16 = cpu.a + value + (test_flag(CARRY) ? 1 : 0);
    cpu.a = t16 & 0xff;
    set_flag(CARRY, t16 > 0xff);
    update_zero_and_negative_flags(cpu.a);
    set_flag(OVERFLOW, ((t16 ^ (uint16_t)cpu.a) & (t16 ^ (uint16_t)value) & 0x0080));

    if (test_flag(DECIMAL_MODE))
    {
        set_flag(CARRY, 0);
        if ((cpu.a & 0xf) > 0x9)
            cpu.a += 0x06;
        if ((cpu.a & 0xf0) > 0x90)
        {
            cpu.a += 0x60;
            set_flag(CARRY, 1);
        }
    }
}

void sbc(uint8_t value)
{
    uint16_t t16 = cpu.a + ((uint16_t)value ^ 0xff) + (test_flag(CARRY) ? 1 : 0);
    cpu.a = t16 & 0xff;
    set_flag(CARRY, t16 > 0xff);
    update_zero_and_negative_flags(cpu.a);
    set_flag(OVERFLOW, ((t16 ^ (uint16_t)cpu.a) & (t16 ^ (uint16_t)value) & 0x0080));
    if (test_flag(DECIMAL_MODE))
    {
        set_flag(CARRY, 0);
        cpu.a -= 0x66;
        if ((cpu.a & 0xf) > 0x9)
            cpu.a += 0x06;
        if ((cpu.a & 0xf0) > 0x90)
        {
            cpu.a += 0x60;
            set_flag(CARRY, 1);
        }
    }
}

#define OPCODES \
    _(ADC) _(AND) _(ASL) _(BCC) _(BCS) _(BEQ) _(BIT) _(BMI) _(BNE) _(BPL) _(BRK) _(BVC) \
    _(BVS) _(CLC) _(CLD) _(CLI) _(CLV) _(CMP) _(CPX) _(CPY) _(DEC) _(DEX) _(DEY) _(EOR) \
    _(INC) _(INX) _(INY) _(JMP) _(JSR) _(LDA) _(LDX) _(LDY) _(LSR) _(NOP) _(ORA) _(PHA) \
    _(PHP) _(PLA) _(PLP) _(ROL) _(ROR) _(RTI) _(RTS) _(SBC) _(SEC) _(SED) _(SEI) _(STA) \
    _(STX) _(STY) _(TAX) _(TAY) _(TSX) _(TXA) _(TXS) _(TYA) \
    _(BRA) _(PHX) _(PHY) _(PLX) _(PLY) _(STZ) _(TRB) _(TSB) _(DEA) _(INA)

#define _(x) x,
typedef enum
{
    NO_OPCODE = -1,
    OPCODES
} r_opcode;
#undef _

#define _(x) #x,
const char* const OPCODE_STRINGS[] = { OPCODES };
#undef _

#define ADDRESSING_MODES \
    _(accumulator) \
    _(immediate) \
    _(implied) \
    _(relative) \
    _(absolute) \
    _(zero_page) \
    _(zero_page_indirect) \
    _(indirect) /* JMP only */ \
    _(zero_page_x) \
    _(zero_page_y) \
    _(absolute_x) /* +1 for page overflow */ \
    _(absolute_y) /* +1 for page overflow */ \
    _(indexed_indirect_x) \
    _(indirect_indexed_y) /* +1 for page overflow */

#define _(x) x,
typedef enum
{
    NO_ADDRESSING_MODE = -1,
    ADDRESSING_MODES
} r_addressing_mode;
#undef _

#define _(x) #x,
const char* const ADDRESSING_MODE_STRINGS[] = { ADDRESSING_MODES };
#undef _

#define TEST_OPCODE(opcode) test_opcode = opcode;
#define OPCODE_VARIANT(opcode, cycles, addressing_mode) \
    if (opcode_from_ip == opcode) { \
        *_opcode = test_opcode; \
        *_addressing_mode = addressing_mode; \
        *_cycles = cycles; \
    }

void fetch_next_opcode(uint8_t* _read_opcode, r_opcode* _opcode, r_addressing_mode* _addressing_mode, uint8_t* _cycles)
{
    /*
     * The following data has been transcribed from
     * https://www.atarimax.com/jindroush.atari.org/aopc.html
     */
    r_opcode test_opcode = NO_OPCODE;
    uint8_t opcode_from_ip = rip8();
    *_read_opcode = opcode_from_ip;

    TEST_OPCODE(ADC)
        OPCODE_VARIANT(0x69, 2, immediate)
        OPCODE_VARIANT(0x65, 3, zero_page)
        OPCODE_VARIANT(0x75, 4, zero_page_x)
        OPCODE_VARIANT(0x6D, 4, absolute)
        OPCODE_VARIANT(0x7D, 4, absolute_x)
        OPCODE_VARIANT(0x79, 4, absolute_y)
        OPCODE_VARIANT(0x61, 6, indexed_indirect_x)
        OPCODE_VARIANT(0x71, 6, indirect_indexed_y)
    TEST_OPCODE(AND)
        OPCODE_VARIANT(0x29, 2, immediate)
        OPCODE_VARIANT(0x25, 2, zero_page)
        OPCODE_VARIANT(0x35, 3, zero_page_x)
        OPCODE_VARIANT(0x2D, 4, absolute)
        OPCODE_VARIANT(0x3D, 4, absolute_x)
        OPCODE_VARIANT(0x39, 4, absolute_y)
        OPCODE_VARIANT(0x21, 6, indexed_indirect_x)
        OPCODE_VARIANT(0x31, 5, indirect_indexed_y)
    TEST_OPCODE(ASL)
        OPCODE_VARIANT(0x0A, 2, accumulator)
        OPCODE_VARIANT(0x06, 5, zero_page)
        OPCODE_VARIANT(0x16, 6, zero_page_x)
        OPCODE_VARIANT(0x0E, 6, absolute)
        OPCODE_VARIANT(0x1E, 7, absolute_x)
    TEST_OPCODE(BIT)
        OPCODE_VARIANT(0x24, 3, zero_page)
        OPCODE_VARIANT(0x2C, 4, absolute)
    TEST_OPCODE(BPL)
        OPCODE_VARIANT(0x10, 2, relative)
    TEST_OPCODE(BRA)
        OPCODE_VARIANT(0x80, 2, relative)
    TEST_OPCODE(BMI)
        OPCODE_VARIANT(0x30, 2, relative)
    TEST_OPCODE(BVC)
        OPCODE_VARIANT(0x50, 2, relative)
    TEST_OPCODE(BVS)
        OPCODE_VARIANT(0x70, 2, relative)
    TEST_OPCODE(BCC)
        OPCODE_VARIANT(0x90, 2, relative)
    TEST_OPCODE(BCS)
        OPCODE_VARIANT(0xB0, 2, relative)
    TEST_OPCODE(BNE)
        OPCODE_VARIANT(0xD0, 2, relative)
    TEST_OPCODE(BEQ)
        OPCODE_VARIANT(0xF0, 2, relative)
    TEST_OPCODE(BRK)
        OPCODE_VARIANT(0x00, 7, implied)
    TEST_OPCODE(CMP)
        OPCODE_VARIANT(0xC9, 2, immediate)
        OPCODE_VARIANT(0xC5, 3, zero_page)
        OPCODE_VARIANT(0xD5, 4, zero_page_x)
        OPCODE_VARIANT(0xCD, 4, absolute)
        OPCODE_VARIANT(0xDD, 4, absolute_x)
        OPCODE_VARIANT(0xD9, 4, absolute_y)
        OPCODE_VARIANT(0xC1, 6, indexed_indirect_x)
        OPCODE_VARIANT(0xD1, 5, indirect_indexed_y)
    TEST_OPCODE(CPX)
        OPCODE_VARIANT(0xE0, 2, immediate)
        OPCODE_VARIANT(0xE4, 3, zero_page)
        OPCODE_VARIANT(0xEC, 4, absolute)
    TEST_OPCODE(CPY)
        OPCODE_VARIANT(0xC0, 2, immediate)
        OPCODE_VARIANT(0xC4, 3, zero_page)
        OPCODE_VARIANT(0xCC, 4, absolute)
    TEST_OPCODE(DEC)
        OPCODE_VARIANT(0xC6, 5, zero_page)
        OPCODE_VARIANT(0xD6, 6, zero_page_x)
        OPCODE_VARIANT(0xCE, 6, absolute)
        OPCODE_VARIANT(0xDE, 7, absolute_x)
    TEST_OPCODE(EOR)
        OPCODE_VARIANT(0x49, 2, immediate)
        OPCODE_VARIANT(0x45, 3, zero_page)
        OPCODE_VARIANT(0x55, 4, zero_page_x)
        OPCODE_VARIANT(0x4D, 4, absolute)
        OPCODE_VARIANT(0x5D, 4, absolute_x)
        OPCODE_VARIANT(0x59, 4, absolute_y)
        OPCODE_VARIANT(0x41, 6, indexed_indirect_x)
        OPCODE_VARIANT(0x51, 5, indirect_indexed_y)
    TEST_OPCODE(CLC)
        OPCODE_VARIANT(0x18, 2, implied)
    TEST_OPCODE(SEC)
        OPCODE_VARIANT(0x38, 2, implied)
    TEST_OPCODE(CLI)
        OPCODE_VARIANT(0x58, 2, implied)
    TEST_OPCODE(SEI)
        OPCODE_VARIANT(0x78, 2, implied)
    TEST_OPCODE(CLV)
        OPCODE_VARIANT(0xB8, 2, implied)
    TEST_OPCODE(CLD)
        OPCODE_VARIANT(0xD8, 2, implied)
    TEST_OPCODE(SED)
        OPCODE_VARIANT(0xF8, 2, implied)
    TEST_OPCODE(INC)
        OPCODE_VARIANT(0xE6, 5, zero_page)
        OPCODE_VARIANT(0xF6, 6, zero_page_x)
        OPCODE_VARIANT(0xEE, 6, absolute)
        OPCODE_VARIANT(0xFe, 7, absolute_x)
    TEST_OPCODE(JMP)
        OPCODE_VARIANT(0x4C, 3, absolute)
        OPCODE_VARIANT(0x6C, 5, indirect)
    TEST_OPCODE(JSR)
        OPCODE_VARIANT(0x20, 6, absolute)
    TEST_OPCODE(LDA)
        OPCODE_VARIANT(0xA9, 2, immediate)
        OPCODE_VARIANT(0xA5, 3, zero_page)
        OPCODE_VARIANT(0xB5, 4, zero_page_x)
        OPCODE_VARIANT(0xAD, 4, absolute)
        OPCODE_VARIANT(0xBD, 4, absolute_x)
        OPCODE_VARIANT(0xB9, 4, absolute_y)
        OPCODE_VARIANT(0xA1, 6, indexed_indirect_x)
        OPCODE_VARIANT(0xB1, 5, indirect_indexed_y)
    TEST_OPCODE(LDX)
        OPCODE_VARIANT(0xA2, 2, immediate)
        OPCODE_VARIANT(0xA6, 3, zero_page)
        OPCODE_VARIANT(0xB6, 4, zero_page_y)
        OPCODE_VARIANT(0xAE, 4, absolute)
        OPCODE_VARIANT(0xBE, 4, absolute_y)
    TEST_OPCODE(LDY)
        OPCODE_VARIANT(0xA0, 2, immediate)
        OPCODE_VARIANT(0xA4, 3, zero_page)
        OPCODE_VARIANT(0xB4, 4, zero_page_x)
        OPCODE_VARIANT(0xAC, 4, absolute)
        OPCODE_VARIANT(0xBC, 4, absolute_x)
    TEST_OPCODE(LSR)
        OPCODE_VARIANT(0x4A, 2, accumulator)
        OPCODE_VARIANT(0x46, 5, zero_page)
        OPCODE_VARIANT(0x56, 6, zero_page_x)
        OPCODE_VARIANT(0x4E, 6, absolute)
        OPCODE_VARIANT(0x5E, 7, absolute_x)
    TEST_OPCODE(NOP)
        OPCODE_VARIANT(0xEA, 2, implied)
    TEST_OPCODE(ORA)
        OPCODE_VARIANT(0x09, 2, immediate)
        OPCODE_VARIANT(0x05, 2, zero_page)
        OPCODE_VARIANT(0x15, 3, zero_page_x)
        OPCODE_VARIANT(0x0D, 4, absolute)
        OPCODE_VARIANT(0x1D, 4, absolute_x)
        OPCODE_VARIANT(0x19, 4, absolute_y)
        OPCODE_VARIANT(0x01, 6, indexed_indirect_x)
        OPCODE_VARIANT(0x11, 5, indirect_indexed_y)
    TEST_OPCODE(TAX)
        OPCODE_VARIANT(0xAA, 2, implied)
    TEST_OPCODE(TXA)
        OPCODE_VARIANT(0x8A, 2, implied)
    TEST_OPCODE(DEA)
        OPCODE_VARIANT(0x3A, 2, implied)
    TEST_OPCODE(INA)
        OPCODE_VARIANT(0x1A, 2, implied)
    TEST_OPCODE(DEX)
        OPCODE_VARIANT(0xCA, 2, implied)
    TEST_OPCODE(INX)
        OPCODE_VARIANT(0xE8, 2, implied)
    TEST_OPCODE(TAY)
        OPCODE_VARIANT(0xA8, 2, implied)
    TEST_OPCODE(TYA)
        OPCODE_VARIANT(0x98, 2, implied)
    TEST_OPCODE(DEY)
        OPCODE_VARIANT(0x88, 2, implied)
    TEST_OPCODE(INY)
        OPCODE_VARIANT(0xC8, 2, implied)
    TEST_OPCODE(ROL)
        OPCODE_VARIANT(0x2A, 2, accumulator)
        OPCODE_VARIANT(0x26, 5, zero_page)
        OPCODE_VARIANT(0x36, 6, zero_page_x)
        OPCODE_VARIANT(0x2E, 6, absolute)
        OPCODE_VARIANT(0x3E, 7, absolute_x)
    TEST_OPCODE(ROR)
        OPCODE_VARIANT(0x6A, 2, accumulator)
        OPCODE_VARIANT(0x66, 5, zero_page)
        OPCODE_VARIANT(0x76, 6, zero_page_x)
        OPCODE_VARIANT(0x6E, 6, absolute)
        OPCODE_VARIANT(0x7E, 7, absolute_x)
    TEST_OPCODE(RTI)
        OPCODE_VARIANT(0x40, 6, implied)
    TEST_OPCODE(RTS)
        OPCODE_VARIANT(0x60, 6, implied)
    TEST_OPCODE(SBC)
        OPCODE_VARIANT(0xE9, 2, immediate)
        OPCODE_VARIANT(0xE5, 3, zero_page)
        OPCODE_VARIANT(0xF5, 4, zero_page_x)
        OPCODE_VARIANT(0xED, 4, absolute)
        OPCODE_VARIANT(0xFD, 4, absolute_x)
        OPCODE_VARIANT(0xF9, 4, absolute_y)
        OPCODE_VARIANT(0xE1, 6, indexed_indirect_x)
        OPCODE_VARIANT(0xF1, 5, indirect_indexed_y)
    TEST_OPCODE(STA)
        OPCODE_VARIANT(0x85, 3, zero_page)
        OPCODE_VARIANT(0x95, 4, zero_page_x)
        OPCODE_VARIANT(0x8D, 4, absolute)
        OPCODE_VARIANT(0x9D, 5, absolute_x) /* no page cross penalty? */
        OPCODE_VARIANT(0x99, 5, absolute_y)
        OPCODE_VARIANT(0x81, 6, indexed_indirect_x)
        OPCODE_VARIANT(0x91, 6, indirect_indexed_y)
        OPCODE_VARIANT(0x92, 5, zero_page_indirect)
    TEST_OPCODE(TXS)
        OPCODE_VARIANT(0x9A, 2, implied)
    TEST_OPCODE(TSX)
        OPCODE_VARIANT(0xBA, 2, implied)
    TEST_OPCODE(PHA)
        OPCODE_VARIANT(0x48, 3, implied)
    TEST_OPCODE(PLA)
        OPCODE_VARIANT(0x68, 4, implied)
    TEST_OPCODE(PLX)
        OPCODE_VARIANT(0xFA, 4, implied)
    TEST_OPCODE(PLY)
        OPCODE_VARIANT(0x7A, 4, implied)
    TEST_OPCODE(PHP)
        OPCODE_VARIANT(0x08, 3, implied)
    TEST_OPCODE(PHX)
        OPCODE_VARIANT(0xDA, 3, implied)
    TEST_OPCODE(PHY)
        OPCODE_VARIANT(0x5A, 3, implied)
    TEST_OPCODE(PLP)
        OPCODE_VARIANT(0x28, 4, implied)
    TEST_OPCODE(STX)
        OPCODE_VARIANT(0x86, 3, zero_page)
        OPCODE_VARIANT(0x96, 4, zero_page_y)
        OPCODE_VARIANT(0x8E, 4, absolute)
    TEST_OPCODE(STY)
        OPCODE_VARIANT(0x84, 3, zero_page)
        OPCODE_VARIANT(0x94, 4, zero_page_x)
        OPCODE_VARIANT(0x8C, 4, absolute)
    TEST_OPCODE(STZ)
        OPCODE_VARIANT(0x64, 3, zero_page)
        OPCODE_VARIANT(0x74, 4, zero_page_x)
        OPCODE_VARIANT(0x9C, 4, absolute)
        OPCODE_VARIANT(0x9E, 5, absolute_x)
}

void branch(uint8_t condition, int8_t offset, uint8_t* cycles)
{
    if (condition)
    {
        // branch succeeds
        *cycles += 1;
        if ((cpu.ip & 0xfff0) != ((cpu.ip + offset) & 0xfff0))
            *cycles += 1;
        cpu.ip += offset;
    }
}

void handle_next_opcode()
{
    uint16_t old_ip = cpu.ip;

    // fetch opcode, addressing mode and cycles for next instruction
    uint8_t read_opcode = 0;
    r_opcode opcode = NO_OPCODE;
    r_addressing_mode addressing_mode = NO_ADDRESSING_MODE;
    uint8_t cycles = 0;
    fetch_next_opcode(&read_opcode, &opcode, &addressing_mode, &cycles);

    if (opcode == NO_OPCODE || addressing_mode == NO_ADDRESSING_MODE)
    {
        fprintf(stderr, "Unhandled opcode at %04x: %02x\n", old_ip, read_opcode);
        exit(1);
    }

    // handle addressing modes, store result in target_address
    uint16_t target_address = 0;
    uint8_t immediate_value = 0;
    int8_t relative_offset = 0;
    switch (addressing_mode)
    {
        case immediate:
            immediate_value = rip8();
            break;
        case relative:
            relative_offset = (int8_t)rip8();
            break;
        case absolute:
            target_address = rip16();
            break;
        case zero_page:
            target_address = rip8();
            break;
        case indirect:
            target_address = read16(rip16());
            break;
        case zero_page_indirect:
            target_address = read16(rip8());
            break;
        case zero_page_x:
            target_address = (rip8() + cpu.x) % 0xff;
            break;
        case zero_page_y:
            target_address = (rip8() + cpu.y) % 0xff;
            break;
        case absolute_x:
            target_address = rip16();
            if ((target_address >> 12) != ((target_address + cpu.x) >> 12))
                cycles += 1;
            target_address += cpu.x;
            break;
        case absolute_y:
            target_address = rip16();
            if ((target_address >> 12) != ((target_address + cpu.y) >> 12))
                cycles += 1;
            target_address += cpu.y;
            break;
        case indexed_indirect_x:
            target_address = read16((rip8() + cpu.x) & 0xff);
            break;
        case indirect_indexed_y:
            target_address = cpu.y;
            uint16_t temp = read16(rip8());
            if ((target_address >> 12) != ((target_address + temp) >> 12))
                cycles += 1;
            target_address += temp;
            break;
    }

    if (show_log)
    {
        fprintf(stderr, "%04x | %d | %02x | %s %-18s | ", old_ip, cycles, read_opcode, OPCODE_STRINGS[opcode],
            ADDRESSING_MODE_STRINGS[addressing_mode]
        );
    }

    int unhandled_opcode = 0;
    uint8_t t8 = 0;
    uint16_t t16 = 0;

    // handle opcode
    switch (opcode)
    {
        case ADC:
            adc((addressing_mode == immediate) ? immediate_value : read8(target_address));
            break;
        case AND:
            t8 = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            cpu.a &= t8;
            update_zero_and_negative_flags(cpu.a);
            break;
        case ASL:
            if (addressing_mode == accumulator)
            {
                set_flag(CARRY, cpu.a & 0x80);
                cpu.a <<= 1;
                update_zero_and_negative_flags(cpu.a);
            }
            else
            {
                t8 = read8(target_address);
                set_flag(CARRY, t8 & 0x80);
                t8 <<= 1;
                update_zero_and_negative_flags(t8);
                write8(target_address, t8);
            }
            break;
        case BCC:
            branch(!test_flag(CARRY), relative_offset, &cycles);
            break;
        case BCS:
            branch(test_flag(CARRY), relative_offset, &cycles);
            break;
        case BEQ:
            branch(test_flag(ZERO), relative_offset, &cycles);
            break;
        case BIT:
            t8 = read8(target_address);
            uint8_t temp = cpu.a;
            temp &= t8;
            update_zero_and_negative_flags(temp);
            set_flag(NEGATIVE, t8 & 0x80);
            set_flag(OVERFLOW, t8 & 0x40);
            break;
        case BMI:
            branch(test_flag(NEGATIVE), relative_offset, &cycles);
            break;
        case BNE:
            branch(!test_flag(ZERO), relative_offset, &cycles);
            break;
        case BPL:
            branch(!test_flag(NEGATIVE), relative_offset, &cycles);
            break;
        case BRA:
            branch(1, relative_offset, &cycles);
            break;
        case BRK:
            unhandled_opcode = 1;
            break;
        case BVC:
            branch(!test_flag(OVERFLOW), relative_offset, &cycles);
            break;
        case BVS:
            branch(test_flag(OVERFLOW), relative_offset, &cycles);
            break;
        case CLC:
            set_flag(CARRY, 0);
            break;
        case CLD:
            set_flag(DECIMAL_MODE, 0);
            break;
        case CLI:
            // is this the right flag 0x04 ?
            set_flag(INTERRUPT_DISABLE, 0);
            break;
        case CLV:
            set_flag(OVERFLOW, 0);
            break;
        case CMP:
            t8 = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            cmp(cpu.a, t8);
            break;
        case CPX:
            t8 = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            cmp(cpu.x, t8);
            break;
        case CPY:
            t8 = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            cmp(cpu.y, t8);
            break;
        case DEC:
            t8 = read8(target_address);
            t8 -= 1;
            write8(target_address, t8);
            update_zero_and_negative_flags(t8);
            break;
        case DEA:
            cpu.a -= 1;
            update_zero_and_negative_flags(cpu.a);
            break;
        case DEX:
            cpu.x -= 1;
            update_zero_and_negative_flags(cpu.x);
            break;
        case DEY:
            cpu.y -= 1;
            update_zero_and_negative_flags(cpu.y);
            break;
        case EOR:
            t8 = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            cpu.a ^= t8;
            update_zero_and_negative_flags(cpu.a);
            break;
        case INC:
            t8 = read8(target_address);
            t8 += 1;
            write8(target_address, t8);
            update_zero_and_negative_flags(t8);
            break;
        case INA:
            cpu.a += 1;
            update_zero_and_negative_flags(cpu.a);
            break;
        case INX:
            cpu.x += 1;
            update_zero_and_negative_flags(cpu.x);
            break;
        case INY:
            cpu.y += 1;
            update_zero_and_negative_flags(cpu.y);
            break;
        case JMP:
            cpu.ip = target_address;
            // TODO handle page boundary behaviour?
            break;
        case JSR:
            // push IP - 1 because target address has already been read
            trace_stack_function[trace_stack_pointer] = target_address;
            trace_stack[trace_stack_pointer] = cpu.sp;
            trace_stack_pointer--;
            calls_per_function[target_address]++;

            push(((cpu.ip - 1) >> 8) & 0xff);
            push((cpu.ip - 1) & 0xff);
            cpu.ip = target_address;
            break;
        case LDA:
            cpu.a = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            update_zero_and_negative_flags(cpu.a);
            break;
        case LDX:
            cpu.x = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            update_zero_and_negative_flags(cpu.x);
            break;
        case LDY:
            cpu.y = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            update_zero_and_negative_flags(cpu.y);
            break;
        case LSR:
            if (addressing_mode == accumulator)
            {
                set_flag(CARRY, cpu.a & 1);
                cpu.a >>= 1;
                update_zero_and_negative_flags(cpu.a);
            }
            else
            {
                t8 = read8(target_address);
                set_flag(CARRY, t8 & 1);
                t8 >>= 1;
                update_zero_and_negative_flags(t8);
                write8(target_address, t8);
            }
            break;
        case NOP:
            break;
        case ORA:
            t8 = (addressing_mode == immediate) ? immediate_value : read8(target_address);
            cpu.a |= t8;
            update_zero_and_negative_flags(cpu.a);
            break;
        case PHA:
            push(cpu.a);
            break;
        case PHX:
            push(cpu.x);
            break;
        case PHY:
            push(cpu.y);
            break;
        case PHP:
            push(cpu.flags);
            break;
        case PLA:
            cpu.a = pop();
            break;
        case PLX:
            cpu.x = pop();
            break;
        case PLY:
            cpu.y = pop();
            break;
        case PLP:
            pop(cpu.flags);
            break;
        case ROL:
            if (addressing_mode == accumulator)
            {
                cpu.a = rol(cpu.a);
                update_zero_and_negative_flags(cpu.a);
            }
            else
            {
                t8 = rol(read8(target_address));
                write8(target_address, t8);
                update_zero_and_negative_flags(t8);
            }
            break;
        case ROR:
            if (addressing_mode == accumulator)
            {
                cpu.a = ror(cpu.a);
                update_zero_and_negative_flags(cpu.a);
            }
            else
            {
                t8 = ror(read8(target_address));
                write8(target_address, t8);
                update_zero_and_negative_flags(t8);
            }
            break;
        case RTI:
            cpu.flags = pop();
            t16 = pop();
            t16 |= ((uint16_t)pop()) << 8;
            cpu.ip = t16;
            break;
        case RTS:
            if (trace_stack[trace_stack_pointer + 1] == cpu.sp + 2)
            {
                trace_stack_pointer++;
            }

            t16 = pop();
            t16 |= ((uint16_t)pop()) << 8;
            cpu.ip = t16 + 1;
            break;
        case SBC:
            sbc((addressing_mode == immediate) ? immediate_value : read8(target_address));
            break;
        case SEC:
            set_flag(CARRY, 1);
            break;
        case SED:
            set_flag(DECIMAL_MODE, 1);
            break;
        case SEI:
            // is this the right flag 0x04 ?
            set_flag(INTERRUPT_DISABLE, 1);
            break;
        case STA:
            write8(target_address, cpu.a);
            break;
        case STX:
            write8(target_address, cpu.x);
            break;
        case STY:
            write8(target_address, cpu.y);
            break;
        case STZ:
            write8(target_address, 0);
            break;
        case TAX:
            cpu.x = cpu.a;
            update_zero_and_negative_flags(cpu.x);
            break;
        case TAY:
            cpu.y = cpu.a;
            update_zero_and_negative_flags(cpu.y);
            break;
        case TSX:
            cpu.x = cpu.sp;
            break;
        case TXA:
            cpu.a = cpu.x;
            update_zero_and_negative_flags(cpu.a);
            break;
        case TXS:
            cpu.sp = cpu.x;
            break;
        case TYA:
            cpu.a = cpu.y;
            update_zero_and_negative_flags(cpu.a);
            break;
        default:
            unhandled_opcode = 1;
            break;
    };
    if (unhandled_opcode)
    {
        fprintf(stderr, "Opcode not implemented yet!\n");
        exit(1);
    }
    cpu.total_cycles += cycles;
    if (trace_stack_pointer < 0xff)
        cycles_per_function[trace_stack_function[trace_stack_pointer + 1]] += cycles;
    if (show_log)
    {
        char flags_str[6];
        flags_str[0] = test_flag(CARRY) ? 'C' : 'c';
        flags_str[1] = test_flag(ZERO) ? 'Z' : 'z';
        flags_str[2] = test_flag(DECIMAL_MODE) ? 'D' : 'd';
        flags_str[3] = test_flag(OVERFLOW) ? 'V' : 'v';
        flags_str[4] = test_flag(NEGATIVE) ? 'N' : 'n';
        flags_str[5] = 0;

        fprintf(stderr, "A: %02x, X: %02x, Y: %02x, PC: %04x, SP: %02x, FLAGS: %02x %s | %10ld |",
               cpu.a, cpu.x, cpu.y, cpu.ip, cpu.sp, cpu.flags, flags_str, cpu.total_cycles);
        fprintf(stderr, "\n");
    }
}

void set_pixel(int px, int py, unsigned int color)
{
    int x, y;
    XSetForeground(dsp, gc, color);
    for (y = 0; y < SCALE; y++)
        for (x = 0; x < SCALE; x++)
            XDrawPoint(dsp, win, gc, px * SCALE + x, py * SCALE + y);
}

void render_hires_screen()
{
    uint8_t current_screen = ram[0x30b];
    int x, y;
    for (y = 0; y < 192; y++)
    {
        uint16_t line_offset = yoffset[y] | (current_screen == 1 ? 0x2000 : 0x4000);
        for (x = 0; x < 40; x++)
        {
            uint8_t byte = ram[line_offset + x];
            for (int px = 0; px < 7; px++)
                set_pixel(x * 7 + px, y, ((byte >> px) & 1) ? white : black);
        }
    }
}

int32_t find_address_for_label(const char* label)
{
    for (int i = 0; i < LABEL_COUNT; i++)
        if ((strlen(label) == strlen(LABELS[i])) &&
            (strcmp(label, LABELS[i]) == 0))
            for (int k = 0; k < 0x10000; k++)
                if (label_for_address[k] == i)
                    return k;
    return -1;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage: ./champ [options] <memory dump>\n");
        printf("\n");
        printf("Options:\n");
        printf("  --show-screen\n");
        printf("  --hide-log\n");
        printf("  --start-pc <address or label>\n");
        printf("  --frame-start <address or label>\n");
        printf("  --max-frames <n>\n");
        printf("  --watches <output filename>\n");
        exit(1);
    }

    for (int i = 1; i < argc - 1; i++)
    {
        if (strcmp(argv[i], "--show-screen") == 0)
            show_screen = 1;
        else if (strcmp(argv[i], "--hide-log") == 0)
            show_log = 0;
        else if (strcmp(argv[i], "--start-pc") == 0)
        {
            char *temp = argv[++i];
            char *p = 0;
            start_pc = strtol(temp, &p, 0);
            if (p == temp)
                start_pc = find_address_for_label(temp);
            fprintf(stderr, "Using start PC: 0x%04x\n", start_pc);
        }
        else if (strcmp(argv[i], "--start-frame") == 0)
        {
            char *temp = argv[++i];
            char *p = 0;
            start_frame_pc = strtol(temp, &p, 0);
            if (p == temp)
                start_frame_pc = find_address_for_label(temp);
            fprintf(stderr, "Using frame start: 0x%04x\n", start_frame_pc);
        }
        else if (strcmp(argv[i], "--max-frames") == 0)
        {
            char *temp = argv[++i];
            char *p = 0;
            max_frames = strtol(temp, &p, 0);
            if (p == temp)
                max_frames = find_address_for_label(temp);
            fprintf(stderr, "Max frames: %d\n", max_frames);
        }
        else if (strcmp(argv[i], "--watches") == 0)
        {
            const char* filename = argv[++i];
            if (access(filename, F_OK) != -1)
            {
                fprintf(stderr, "Watch output file exists: %s, exiting...\n", filename);
                exit(1);
            }
            watches_file = fopen(filename, "w");
            fprintf(stderr, "Writing watches to %s.\n", filename);
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(1);
        }
    }
    memset(ram, 0, sizeof(ram));
    memset(cycles_per_function, 0, sizeof(cycles_per_function));
    memset(calls_per_function, 0, sizeof(calls_per_function));

    load(argv[argc - 1], 0);

    if (show_screen)
        init_display();

    init_cpu(&cpu);
    cpu.ip = start_pc;
    struct timespec tstart = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    unsigned long start_time = tstart.tv_sec * 1000000000 + tstart.tv_nsec;
    uint32_t next_display_refresh = 0;
    uint8_t old_screen_number = 0;
    while (1) {
//         if (cpu.ip >= 0xf5b2)
//             printf("*** ");
        handle_next_opcode();
        if ((start_frame_pc != 0xffff) && (cpu.ip == start_frame_pc))
        {
            if (last_frame_cycle_count > 0)
            {
                frame_cycle_count += (cpu.total_cycles - last_frame_cycle_count);
                frame_count += 1;
//                     printf("%ld\n", (cpu.total_cycles - last_frame_cycle_count));
//                     printf("%d\n", (uint64_t)((double)frame_cycle_count / frame_count));
                if ((max_frames > 0) && (frame_count >= max_frames))
                    break;
            }
            last_frame_cycle_count = cpu.total_cycles;
        }
//         if (cycles)
//         {
// //             struct timespec tlap = {0, 0};
// //             clock_gettime(CLOCK_MONOTONIC, &tlap);
// //             unsigned long lap_time = tlap.tv_sec * 1000000000 + tlap.tv_nsec;
// //             unsigned long elapsed_time = lap_time - start_time;
// //             start_time = lap_time;
// //             printf("Time taken for %d cycles: %ld\n", cycles, elapsed_time);
//         }
//         else
//             break;
        if (show_screen)
        {
            if (XEventsQueued(dsp, QueuedAfterFlush) > 0)
            {
                int exit_program = 0;
                XNextEvent(dsp, &evt);

                switch (evt.type) {

                case (KeyRelease) :
                    if (evt.xkey.keycode == keyQ)
                        exit_program = 1;

                case (ClientMessage) :
                    if (evt.xclient.data.l[0] == wmDelete)
                        exit_program = 1;
                    break;

                }
                if (exit_program)
                    break;
            }
            if (ram[0x30b] != old_screen_number)
            {
                old_screen_number = ram[0x30b];
                render_hires_screen();
            }
        }
    }
    fprintf(stderr, "Total cycles: %d\n", cpu.total_cycles);
    fprintf(stderr, "Cycles per frame: %d\n", (uint64_t)((double)frame_cycle_count / frame_count));
    printf("%12s %6s %8s %8s %4s %s\n", "Total CC", "% CC", "Calls", "CC/Call", "Addr", "Label");
    for (uint32_t i = 0; i < 0x10000; i++)
    {
        if (cycles_per_function[i] > 0)
        {
            printf("%12d %5.2f%% %8d %8d %04x",
                   cycles_per_function[i],
                   cycles_per_function[i] * 100.0 / cpu.total_cycles,
                   calls_per_function[i],
                   cycles_per_function[i] / calls_per_function[i],
                   i);
            if (label_for_address[i] >= 0)
                printf(" %s", LABELS[label_for_address[i]]);
            printf("\n");
        }
    }
    if (watches_file)
        fclose(watches_file);
    return 0;
}

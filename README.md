# Champ - N. Harold Cham's 65C02 Profiler

This is a 65C02 emulator / profiler that enables you to really get to know your APPLE ][ HiRes Graphics Mode Demo.

## Features

* full 65C02 emulation at cycle accuracy
* screen output as animated GIF with exact frame timing
* average frame rate
* see how much time is spent in which subroutine
* watch variables (single variables or pairs)

* no dependencies but Ruby, gcc and Merlin32

![Champ Screenshot](doc/screenshot.png?raw=true "Fig. 1 Champ Screenshot")

## Usage

First, make sure you have gcc, ruby and Merlin32 installed. You need to prepare a YAML file to tell champ about all source and object files and their memory locations.

Take `plot3d.yaml` for example:

```
load:
    - [0x8900, 'plot3d/SINETABLE']
    - [0x1200, 'plot3d/multtab.s']
    - [0xb600, 'plot3d/projtab.s']
    - [0x8400, 'plot3d/ldrwtab.s']
    - [0x9500, 'plot3d/FONT']
    - [0x9000, 'plot3d/object1.s']
    - [0x8b00, 'plot3d/object2.s']
    - [0x6000, 'plot3d/plot3d242.s']
entry: ENTRY
instant_rts:
    - LOAD1
```

We specified some source files (they'll get compiled automatically) and some object files along with their locations in memory. We also specified the entry point for our program, this can be a label or an address.

Furthermore, we can disable subroutines by replacing the first opcode with a RTS. This is necessary in some cases because Champ does not emulate hardware and thus can not load data from disk, for example.

## Miscellaneous

By the way, there's a full-fledged, standalone, no-dependencies GIF encoder in `pgif.c` that writes animated GIFs and uses some optimizations to further minimize space.

#ifndef _EMULATOR_H_
#define _EMULATOR_H_

typedef struct Cpu Cpu;
typedef struct Debugger Debugger;

typedef struct Emulator {
    Cpu *cpu;
    Debugger *debugger;
} Emulator;

#endif

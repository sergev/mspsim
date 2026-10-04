#ifndef _LOADER_H_
#define _LOADER_H_

#include <stddef.h>
#include <stdint.h>

#include "emulator.h"

/*
 * Firmware loaders. Each returns the number of bytes loaded, or -1 with a
 * message in err. They may set emu->entry, the start address used when
 * the reset vector is erased; call emu_reset() afterwards to start there.
 */

/* ELF or Intel HEX, detected from the contents. */
long load_firmware(Emulator *emu, const char *path, char *err, size_t errsize);

/* Raw binary image at addr, which also becomes emu->entry. */
long load_binary(Emulator *emu, const char *path, uint16_t addr, char *err, size_t errsize);

/* ELF32 little-endian MSP430: PT_LOAD segments at their physical addresses, plus symbols. */
long load_elf(Emulator *emu, const char *path, char *err, size_t errsize);

/* Intel HEX: data, extended address and start address records. */
long load_ihex(Emulator *emu, const char *path, char *err, size_t errsize);

#endif

#include "loader/loader.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader/symbols.h"

#define FAIL(...)                            \
    do {                                     \
        snprintf(err, errsize, __VA_ARGS__); \
        goto fail;                           \
    } while (0)

/* Whole file in a malloc'ed buffer, NUL-terminated. */
static uint8_t *read_file(const char *path, size_t *size, char *err, size_t errsize)
{
    FILE *f      = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;

    if (f == NULL)
        FAIL("%s", strerror(errno));
    if (fseek(f, 0, SEEK_END) < 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) < 0)
        FAIL("%s", strerror(errno));
    buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n)
        FAIL("read error");
    fclose(f);
    buf[n] = 0;
    *size  = n;
    return buf;
fail:
    if (f != NULL)
        fclose(f);
    free(buf);
    return NULL;
}

long load_firmware(Emulator *emu, const char *path, char *err, size_t errsize)
{
    unsigned char head[4] = { 0 };
    FILE *f               = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        snprintf(err, errsize, "%s", strerror(errno));
        return -1;
    }
    n = fread(head, 1, sizeof head, f);
    fclose(f);

    if (n == 4 && memcmp(head, "\177ELF", 4) == 0)
        return load_elf(emu, path, err, errsize);
    if (n > 0 && head[0] == ':')
        return load_ihex(emu, path, err, errsize);
    snprintf(err, errsize, "not an ELF or Intel HEX file; use -b ADDR for a raw binary");
    return -1;
}

long load_binary(Emulator *emu, const char *path, uint16_t addr, char *err, size_t errsize)
{
    size_t size;
    uint8_t *buf = read_file(path, &size, err, errsize);

    if (buf == NULL)
        return -1;
    if (size > 0x10000u - addr) {
        snprintf(err, errsize, "%zu bytes do not fit at 0x%04x", size, addr);
        free(buf);
        return -1;
    }
    memcpy(emu->mem + addr, buf, size);
    free(buf);
    emu->entry = addr;
    return size;
}

/* ---- ELF ---- */

enum {
    EM_MSP430  = 105,
    PT_LOAD    = 1,
    SHT_SYMTAB = 2,
    SHN_UNDEF  = 0,
    STT_NOTYPE = 0,
    STT_OBJECT = 1,
    STT_FUNC   = 2,
};

static uint16_t rd16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* [off, off + len) lies inside the file */
static bool in_file(size_t size, uint32_t off, uint32_t len)
{
    return off <= size && len <= size - off;
}

static void elf_symbols(Emulator *emu, const uint8_t *buf, size_t size)
{
    uint32_t shoff     = rd32(buf + 32);
    uint16_t shentsize = rd16(buf + 46), shnum = rd16(buf + 48);

    if (shnum == 0 || shentsize < 40 || !in_file(size, shoff, (uint32_t)shnum * shentsize))
        return;

    for (int i = 0; i < shnum; i++) {
        const uint8_t *sh = buf + shoff + i * shentsize;

        if (rd32(sh + 4) != SHT_SYMTAB)
            continue;
        uint32_t off = rd32(sh + 16), len = rd32(sh + 20), link = rd32(sh + 24);
        uint32_t entsize = rd32(sh + 36);
        if (link >= shnum || entsize < 16 || !in_file(size, off, len))
            continue;

        const uint8_t *strsh = buf + shoff + link * shentsize;
        uint32_t stroff = rd32(strsh + 16), strlen_ = rd32(strsh + 20);
        if (!in_file(size, stroff, strlen_) || strlen_ == 0 || buf[stroff + strlen_ - 1] != 0)
            continue;

        for (uint32_t s = 0; s + entsize <= len; s += entsize) {
            const uint8_t *sym = buf + off + s;
            uint32_t name = rd32(sym), value = rd32(sym + 4);
            uint8_t type = sym[12] & 0xF;
            const char *str;
            int rank;

            if (rd16(sym + 14) == SHN_UNDEF || name >= strlen_ || value > 0xFFFF)
                continue;
            if (type == STT_FUNC)
                rank = SYM_FUNC;
            else if (type == STT_NOTYPE)
                rank = SYM_LABEL;
            else if (type == STT_OBJECT)
                rank = SYM_DATA;
            else
                continue;
            str = (const char *)buf + stroff + name;
            if (str[0] == 0 || str[0] == '.' || str[0] == '$')
                continue;
            symbols_add(emu, str, value, rank);
        }
    }
    symbols_sort(emu);
}

long load_elf(Emulator *emu, const char *path, char *err, size_t errsize)
{
    size_t size;
    uint8_t *buf = read_file(path, &size, err, errsize);
    long total   = 0;

    if (buf == NULL)
        return -1;
    if (size < 52 || memcmp(buf, "\177ELF", 4) != 0)
        FAIL("not an ELF file");
    if (buf[4] != 1 || buf[5] != 1)
        FAIL("not a 32-bit little-endian ELF file");
    if (rd16(buf + 18) != EM_MSP430)
        FAIL("not an MSP430 ELF file (machine %u)", rd16(buf + 18));

    uint32_t phoff     = rd32(buf + 28);
    uint16_t phentsize = rd16(buf + 42), phnum = rd16(buf + 44);
    if (phnum == 0)
        FAIL("no program headers");
    if (phentsize < 32 || !in_file(size, phoff, (uint32_t)phnum * phentsize))
        FAIL("bad program headers");

    for (int i = 0; i < phnum; i++) {
        const uint8_t *ph = buf + phoff + i * phentsize;
        uint32_t off = rd32(ph + 4), paddr = rd32(ph + 12), filesz = rd32(ph + 16);

        if (rd32(ph) != PT_LOAD || filesz == 0)
            continue;
        if (!in_file(size, off, filesz))
            FAIL("segment %d lies outside the file", i);
        if (paddr > 0x10000 || filesz > 0x10000 - paddr)
            FAIL("segment %d at 0x%x does not fit in 64 KB", i, paddr);
        memcpy(emu->mem + paddr, buf + off, filesz);
        total += filesz;
    }

    uint32_t entry = rd32(buf + 24);
    if (entry <= 0xFFFF)
        emu->entry = entry;
    elf_symbols(emu, buf, size);
    free(buf);
    return total;
fail:
    free(buf);
    return -1;
}

/* ---- Intel HEX ---- */

static int hex_byte(const char *s)
{
    int v = 0;

    for (int i = 0; i < 2; i++) {
        int c = s[i];

        if (!isxdigit(c))
            return -1;
        v = v * 16 + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }
    return v;
}

long load_ihex(Emulator *emu, const char *path, char *err, size_t errsize)
{
    size_t size;
    char *text    = (char *)read_file(path, &size, err, errsize);
    char *line    = text;
    uint32_t base = 0;
    long total    = 0;
    int lineno    = 0;
    bool done     = false;

    if (text == NULL)
        return -1;

    while (!done && *line) {
        char *end  = line + strcspn(line, "\r\n");
        char *next = end + (end[0] == '\r');
        next += (next[0] == '\n');
        uint8_t rec[260];
        int n = 0, sum = 0;

        lineno++;
        *end = 0;
        if (line[strspn(line, " \t")] == 0) { /* blank */
            line = next;
            continue;
        }
        if (line[0] != ':')
            FAIL("line %d: missing ':'", lineno);
        for (char *p = line + 1; *p; p += 2) {
            int b = hex_byte(p);
            if (b < 0 || n == (int)sizeof rec)
                FAIL("line %d: bad hex digits", lineno);
            rec[n++] = b;
            sum += b;
        }
        if (n < 5 || n != rec[0] + 5)
            FAIL("line %d: bad record length", lineno);
        if (sum & 0xFF)
            FAIL("line %d: bad checksum", lineno);

        uint8_t len = rec[0], type = rec[3], *data = rec + 4;
        uint16_t offset = rec[1] << 8 | rec[2];

        switch (type) {
        default:
            FAIL("line %d: unknown record type %02x", lineno, type);
        case 0x00: /* data */
            if (base + offset + len > 0x10000)
                FAIL("line %d: data beyond 64 KB", lineno);
            memcpy(emu->mem + base + offset, data, len);
            total += len;
            break;
        case 0x01: /* end of file */
            done = true;
            break;
        case 0x02: /* extended segment address */
            if (len != 2)
                FAIL("line %d: bad record length", lineno);
            base = (data[0] << 8 | data[1]) << 4;
            break;
        case 0x04: /* extended linear address */
            if (len != 2)
                FAIL("line %d: bad record length", lineno);
            base = (uint32_t)(data[0] << 8 | data[1]) << 16;
            break;
        case 0x03: /* start segment address CS:IP */
        case 0x05: /* start linear address */
            if (len != 4)
                FAIL("line %d: bad record length", lineno);
            uint32_t hi = data[0] << 8 | data[1], lo = data[2] << 8 | data[3];
            uint32_t start = (type == 0x03) ? (hi << 4) + lo : hi << 16 | lo;
            if (start <= 0xFFFF)
                emu->entry = start;
            break;
        }
        line = next;
    }
    free(text);
    return total;
fail:
    free(text);
    return -1;
}

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug/debugger.h"
#include "harness.h"
#include "loader/loader.h"
#include "loader/symbols.h"

static char err[256];
static char path[256]; /* the current temporary file */

static void remove_temp(void)
{
    if (path[0])
        unlink(path);
    path[0] = 0;
}

/* Write data to a fresh temporary file, replacing the previous one; returns its path. */
static const char *temp_file(const void *data, size_t size)
{
    const char *dir = getenv("TMPDIR");

    if (path[0] == 0)
        atexit(remove_temp);
    remove_temp();
    snprintf(path, sizeof path, "%s/mspsim-test-XXXXXX", dir ? dir : "/tmp");
    int fd = mkstemp(path);
    if (fd < 0 || write(fd, data, size) != (ssize_t)size)
        abort();
    close(fd);
    return path;
}

static const char *temp_text(const char *text)
{
    return temp_file(text, strlen(text));
}

/* ---- ELF image builder ---- */

typedef struct {
    uint8_t b[2048];
    size_t n;
} Image;

static void put(Image *img, size_t off, uint32_t v, int size)
{
    for (int i = 0; i < size; i++)
        img->b[off + i] = v >> (8 * i);
    if (off + size > img->n)
        img->n = off + size;
}

static void put_bytes(Image *img, size_t off, const void *data, size_t size)
{
    memcpy(img->b + off, data, size);
    if (off + size > img->n)
        img->n = off + size;
}

/* main: mov #0x400, sp; call #sub; loop: jmp $; sub: ret */
static const uint8_t code[] = { 0x31, 0x40, 0x00, 0x04, 0xB0, 0x12,
                                0x0A, 0xC0, 0xFF, 0x3F, 0x30, 0x41 };

static const char strtab[] = "\0main\0sub\0loop\0buf\0.Lfoo\0undef\0";
enum { S_MAIN = 1, S_SUB = 6, S_LOOP = 10, S_BUF = 15, S_LFOO = 19, S_UNDEF = 25 };

static void put_sym(Image *img, size_t off, uint32_t name, uint32_t value, int type, int shndx)
{
    put(img, off, name, 4);
    put(img, off + 4, value, 4);
    put(img, off + 8, 0, 4);
    put(img, off + 12, 0x10 | type, 1); /* global */
    put(img, off + 13, 0, 1);
    put(img, off + 14, shndx, 2);
}

/*
 * Code at code_addr, plus a reset vector segment unless vector is 0.
 * Layout: header, 2 program headers, code, vector, strtab, symtab, sections.
 */
static Image build_elf(int machine, uint32_t code_addr, uint16_t vector, uint32_t entry)
{
    Image img       = { .n = 0 };
    const int phoff = 52, code_off = 116, vec_off = 128, str_off = 132;
    const int sym_off = 164, nsyms = 7, sh_off = sym_off + 16 * nsyms;
    int phnum = vector ? 2 : 1;

    memcpy(img.b, "\177ELF\1\1\1", 7);
    put(&img, 16, 2, 2);       /* ET_EXEC */
    put(&img, 18, machine, 2); /* e_machine */
    put(&img, 20, 1, 4);
    put(&img, 24, entry, 4);
    put(&img, 28, phoff, 4);
    put(&img, 32, sh_off, 4);
    put(&img, 40, 52, 2);
    put(&img, 42, 32, 2);
    put(&img, 44, phnum, 2);
    put(&img, 46, 40, 2);
    put(&img, 48, 3, 2); /* null, symtab, strtab */

    /* PT_LOAD for the code; vaddr differs from paddr to check paddr is used */
    put(&img, phoff, 1, 4);
    put(&img, phoff + 4, code_off, 4);
    put(&img, phoff + 8, 0x1234, 4);
    put(&img, phoff + 12, code_addr, 4);
    put(&img, phoff + 16, sizeof code, 4);
    put(&img, phoff + 20, sizeof code, 4);
    put_bytes(&img, code_off, code, sizeof code);

    if (vector) {
        put(&img, phoff + 32, 1, 4);
        put(&img, phoff + 36, vec_off, 4);
        put(&img, phoff + 40, 0xFFFE, 4);
        put(&img, phoff + 44, 0xFFFE, 4);
        put(&img, phoff + 48, 2, 4);
        put(&img, phoff + 52, 2, 4);
        put(&img, vec_off, vector, 2);
    }

    put_bytes(&img, str_off, strtab, sizeof strtab);

    put_sym(&img, sym_off + 16 * 1, S_MAIN, 0xC000, 2, 1);  /* FUNC */
    put_sym(&img, sym_off + 16 * 2, S_SUB, 0xC00A, 2, 1);   /* FUNC */
    put_sym(&img, sym_off + 16 * 3, S_LOOP, 0xC008, 0, 1);  /* NOTYPE */
    put_sym(&img, sym_off + 16 * 4, S_BUF, 0x0200, 1, 2);   /* OBJECT */
    put_sym(&img, sym_off + 16 * 5, S_LFOO, 0xC002, 0, 1);  /* local label */
    put_sym(&img, sym_off + 16 * 6, S_UNDEF, 0x0000, 0, 0); /* undefined */
    put(&img, sym_off + 16 * nsyms - 1, 0, 1);

    /* section 1: .symtab, linked to section 2: .strtab */
    put(&img, sh_off + 40 + 4, 2, 4);
    put(&img, sh_off + 40 + 16, sym_off, 4);
    put(&img, sh_off + 40 + 20, 16 * nsyms, 4);
    put(&img, sh_off + 40 + 24, 2, 4);
    put(&img, sh_off + 40 + 36, 16, 4);
    put(&img, sh_off + 80 + 4, 3, 4);
    put(&img, sh_off + 80 + 16, str_off, 4);
    put(&img, sh_off + 80 + 20, sizeof strtab, 4);
    put(&img, sh_off + 80 + 36, 0, 4);
    return img;
}

static long load_image(Emulator *emu, const Image *img)
{
    return load_firmware(emu, temp_file(img->b, img->n), err, sizeof err);
}

TEST(elf_segments_and_reset_vector)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xC000, 0xC000, 0xC004);

    poke(0xFFFE, 0xFFFF);
    CHECK_EQ(load_image(emu, &img), sizeof code + 2);
    CHECK_EQ(peek(0xC000), 0x4031);
    CHECK_EQ(peek(0xC00A), 0x4130);
    CHECK_EQ(emu->mem[0x1234], 0); /* loaded at paddr, not vaddr */
    emu_reset(emu);
    CHECK_EQ(emu->cpu->pc, 0xC000);
}

TEST(elf_entry_without_vector)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xC000, 0, 0xC004);

    poke(0xFFFE, 0xFFFF);
    CHECK_EQ(load_image(emu, &img), sizeof code);
    emu_reset(emu);
    CHECK_EQ(emu->cpu->pc, 0xC004);
}

TEST(elf_symbols)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xC000, 0xC000, 0xC000);
    uint16_t addr = 0;

    CHECK(load_image(emu, &img) > 0);
    CHECK_EQ(emu->nsymbols, 4);
    CHECK(symbol_lookup(emu, "main", &addr));
    CHECK_EQ(addr, 0xC000);
    CHECK(symbol_lookup(emu, "buf", &addr));
    CHECK_EQ(addr, 0x0200);
    CHECK(!symbol_lookup(emu, ".Lfoo", &addr));
    CHECK(!symbol_lookup(emu, "undef", &addr));
    CHECK_STR(symbol_at(emu, 0xC00A)->name, "sub");
    CHECK_STR(symbol_before(emu, 0xC00C)->name, "sub");
    CHECK(symbol_at(emu, 0xC002) == NULL);
}

TEST(elf_wrong_machine)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(40, 0xC000, 0, 0xC000); /* ARM */

    CHECK_EQ(load_image(emu, &img), -1);
    CHECK(strstr(err, "not an MSP430") != NULL);
}

TEST(elf_truncated)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xC000, 0, 0xC000);

    img.n = 100; /* cuts off the code */
    CHECK_EQ(load_image(emu, &img), -1);
    CHECK(strstr(err, "outside the file") != NULL);
}

TEST(elf_segment_beyond_64k)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xFFF8, 0, 0xC000);

    CHECK_EQ(load_image(emu, &img), -1);
    CHECK(strstr(err, "does not fit") != NULL);
}

TEST(break_at_symbol)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xC000, 0xC000, 0xC000);

    load_image(emu, &img);
    emu_reset(emu);
    exec_cmd(emu, "break sub");
    CHECK_EQ(emu_run(emu, 0), EMU_BREAKPOINT);
    CHECK_EQ(emu->cpu->pc, 0xC00A);
}

TEST(disassembly_shows_symbols)
{
    Emulator *emu = emu_new();
    Image img     = build_elf(105, 0xC000, 0xC000, 0xC000);

    load_image(emu, &img);
    exec_cmd(emu, "dis 4 main");
    CHECK_STR(console_text,
              "main:\n"
              "c000: 4031 0400        mov   #0x0400, sp\n"
              "c004: 12b0 c00a        call  #0xc00a <sub>\n"
              "loop:\n"
              "c008: 3fff             jmp   0xc008 <loop>\n"
              "sub:\n"
              "c00a: 4130             mov   @sp+, pc\n");
}

TEST(ihex_records)
{
    Emulator *emu    = emu_new();
    const char *path = temp_text(
        ":040000003140000487\r\n" /* 0x0000: 31 40 00 04 */
        "\n"
        ":040000050000C00037\n" /* start address 0xc000 */
        ":00000001FF\n");

    CHECK_EQ(load_ihex(emu, path, err, sizeof err), 4);
    CHECK_EQ(peek(0x0000), 0x4031);
    CHECK_EQ(emu->entry, 0xC000);
}

TEST(ihex_extended_address)
{
    Emulator *emu    = emu_new();
    const char *path = temp_text(
        ":020000020C00F0\n" /* segment 0x0C00 -> base 0xC000 */
        ":02001000FF3FB0\n" /* 0xC010: jmp $ */
        ":00000001FF\n");

    CHECK_EQ(load_ihex(emu, path, err, sizeof err), 2);
    CHECK_EQ(peek(0xC010), 0x3FFF);
}

TEST(ihex_errors)
{
    Emulator *emu = emu_new();

    CHECK_EQ(load_ihex(emu, temp_text(":040000003140000488\n"), err, sizeof err), -1);
    CHECK(strstr(err, "line 1: bad checksum") != NULL);

    CHECK_EQ(load_ihex(emu, temp_text(":00000001FF\nxyz\n"), err, sizeof err), 0);

    CHECK_EQ(load_ihex(emu, temp_text(":0100000000FF\nxyz\n"), err, sizeof err), -1);
    CHECK(strstr(err, "line 2: missing ':'") != NULL);

    CHECK_EQ(load_ihex(emu, temp_text(":020000040001F9\n:0100000000FF\n"), err, sizeof err), -1);
    CHECK(strstr(err, "beyond 64 KB") != NULL);

    CHECK_EQ(load_ihex(emu, temp_text(":00000007F9\n"), err, sizeof err), -1);
    CHECK(strstr(err, "unknown record type 07") != NULL);

    CHECK_EQ(load_ihex(emu, temp_text(":0400000031400004\n"), err, sizeof err), -1);
    CHECK(strstr(err, "bad record length") != NULL);
}

TEST(binary_at_address)
{
    Emulator *emu        = emu_new();
    const uint8_t data[] = { 0xFF, 0x3F };

    poke(0xFFFE, 0xFFFF);
    CHECK_EQ(load_binary(emu, temp_file(data, 2), 0xE000, err, sizeof err), 2);
    CHECK_EQ(peek(0xE000), 0x3FFF);
    emu_reset(emu);
    CHECK_EQ(emu->cpu->pc, 0xE000); /* vector erased: start at the image */

    CHECK_EQ(load_binary(emu, temp_file(data, 2), 0xFFFF, err, sizeof err), -1);
    CHECK(strstr(err, "do not fit") != NULL);
}

TEST(reset_vector_wins_over_entry)
{
    Emulator *emu = emu_new();

    emu->entry = 0xE000;
    poke(0xFFFE, 0xC100);
    emu_reset(emu);
    CHECK_EQ(emu->cpu->pc, 0xC100);
}

TEST(erased_vector_without_entry)
{
    Emulator *emu = emu_new();

    poke(0xFFFE, 0xFFFF);
    emu_reset(emu);
    CHECK_EQ(emu->cpu->pc, 0xFFFF);
}

TEST(unknown_format)
{
    Emulator *emu = emu_new();

    CHECK_EQ(load_firmware(emu, temp_text("hello"), err, sizeof err), -1);
    CHECK(strstr(err, "use -b ADDR") != NULL);
    CHECK_EQ(load_firmware(emu, "/nonexistent/file", err, sizeof err), -1);
    CHECK(strstr(err, "No such file") != NULL);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(elf_segments_and_reset_vector),
        T(elf_entry_without_vector),
        T(elf_symbols),
        T(elf_wrong_machine),
        T(elf_truncated),
        T(elf_segment_beyond_64k),
        T(break_at_symbol),
        T(disassembly_shows_symbols),
        T(ihex_records),
        T(ihex_extended_address),
        T(ihex_errors),
        T(binary_at_address),
        T(reset_vector_wins_over_entry),
        T(erased_vector_without_entry),
        T(unknown_format),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}

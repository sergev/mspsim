#include "asm.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { SECT_TEXT, SECT_DATA, SECT_VECTORS, NSECT };
static const uint16_t sect_org[NSECT] = { ASM_TEXT, ASM_DATA, 0xFFE0 };

typedef struct {
    char name[64];
    int32_t value;
    bool label; /* an address, so not a constant-generator candidate */
} Sym;

typedef struct {
    Emulator *emu;
    int pass, line;
    Sym *syms;
    int nsyms;
    int sect;
    uint16_t loc[NSECT];
    int local_defs[10]; /* definitions of 0: .. 9: seen so far */
    bool lazy;          /* evaluating .set: undefined symbols are not an error yet */
    bool undefined;     /* the last evaluation met an undefined symbol */
    char *err;
    size_t errsize;
    bool failed;
} Asm;

static void error(Asm *a, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (a->failed)
        return;
    a->failed = true;
    n         = snprintf(a->err, a->errsize, "line %d: ", a->line);
    va_start(ap, fmt);
    vsnprintf(a->err + n, a->errsize - n, fmt, ap);
    va_end(ap);
}

static Sym *find_sym(Asm *a, const char *name)
{
    for (int i = 0; i < a->nsyms; i++)
        if (strcmp(a->syms[i].name, name) == 0)
            return &a->syms[i];
    return NULL;
}

static void define(Asm *a, const char *name, int32_t value, bool label)
{
    Sym *s = find_sym(a, name);

    if (s == NULL) {
        a->syms = realloc(a->syms, (a->nsyms + 1) * sizeof(Sym));
        s       = &a->syms[a->nsyms++];
        snprintf(s->name, sizeof s->name, "%s", name);
    } else if (a->pass == 2 && label && s->value != value) {
        error(a, "label %s moved between passes", name);
    }
    s->value = value;
    s->label = label;
}

/* ---- expressions ---- */

typedef struct {
    Asm *a;
    const char *p;
    bool label; /* depends on an address or an undefined symbol */
} Expr;

static int32_t expr_sum(Expr *e);

static void skip_space(Expr *e)
{
    while (isspace((unsigned char)*e->p))
        e->p++;
}

static int32_t symbol_value(Expr *e, const char *name)
{
    Sym *s = find_sym(e->a, name);

    if (s == NULL) {
        e->a->undefined = true;
        if (e->a->pass == 2 && !e->a->lazy)
            error(e->a, "undefined symbol %s", name);
        e->label = true;
        return 0;
    }
    e->label |= s->label;
    return s->value;
}

static int32_t expr_unary(Expr *e)
{
    skip_space(e);
    char c = *e->p;

    if (c == '-' || c == '~' || c == '+') {
        e->p++;
        int32_t v = expr_unary(e);
        return c == '-' ? -v : c == '~' ? ~v : v;
    }
    if (c == '(') {
        e->p++;
        int32_t v = expr_sum(e);
        skip_space(e);
        if (*e->p != ')')
            error(e->a, "missing )");
        else
            e->p++;
        return v;
    }
    if (isdigit((unsigned char)c)) {
        char *end;
        long v = strtol(e->p, &end, 0);

        /* 1f / 1b: numeric local label reference */
        if (end == e->p + 1 && (*end == 'f' || *end == 'b') && !isalnum((unsigned char)end[1])) {
            int n = c - '0', k = e->a->local_defs[n] - (*end == 'b');
            char name[16];

            e->p = end + 1;
            if (k < 0) {
                error(e->a, "no previous label %d", n);
                return 0;
            }
            snprintf(name, sizeof name, "%d\001%d", n, k);
            return symbol_value(e, name);
        }
        e->p = end;
        return v;
    }
    if (c == '$' && !isalnum((unsigned char)e->p[1]) && e->p[1] != '_') {
        e->p++;
        e->label = true;
        return e->a->loc[e->a->sect];
    }
    if (isalpha((unsigned char)c) || c == '_' || c == '.') {
        char name[64];
        int n = 0;

        while ((isalnum((unsigned char)*e->p) || *e->p == '_' || *e->p == '.' || *e->p == '$') &&
               n < (int)sizeof name - 1)
            name[n++] = *e->p++;
        name[n] = 0;
        return symbol_value(e, name);
    }
    error(e->a, "bad expression at '%s'", e->p);
    return 0;
}

static int32_t expr_product(Expr *e)
{
    int32_t v = expr_unary(e);

    for (;;) {
        skip_space(e);
        if (*e->p == '*') {
            e->p++;
            v *= expr_unary(e);
        } else if (*e->p == '/') {
            e->p++;
            int32_t d = expr_unary(e);
            v         = d ? v / d : 0;
        } else {
            return v;
        }
    }
}

static int32_t expr_sum(Expr *e)
{
    int32_t v = expr_product(e);

    for (;;) {
        skip_space(e);
        if (*e->p == '+') {
            e->p++;
            v += expr_product(e);
        } else if (*e->p == '-') {
            e->p++;
            v -= expr_product(e);
        } else {
            return v;
        }
    }
}

/* Evaluate a whole string; *label tells whether it depends on an address. */
static int32_t eval(Asm *a, const char *s, bool *label)
{
    Expr e    = { .a = a, .p = s };
    int32_t v = expr_sum(&e);

    skip_space(&e);
    if (*e.p)
        error(a, "junk after expression: '%s'", e.p);
    if (label)
        *label = e.label;
    return v;
}

/* ---- operands ---- */

typedef enum { OP_REG, OP_IND, OP_INC, OP_IMM, OP_ABS, OP_IDX, OP_SYM } Mode;

typedef struct {
    Mode mode;
    int reg;
    int32_t value;
    bool label;
} Operand;

static int reg_number(const char *s)
{
    static const char *const special[] = { "pc", "sp", "sr", "cg" };

    for (int i = 0; i < 4; i++)
        if (strcasecmp(s, special[i]) == 0)
            return i == 3 ? 3 : i;
    if ((s[0] == 'r' || s[0] == 'R') && isdigit((unsigned char)s[1])) {
        char *end;
        long n = strtol(s + 1, &end, 10);
        if (*end == 0 && n >= 0 && n < 16)
            return n;
    }
    return -1;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = 0;
    return s;
}

static bool parse_operand(Asm *a, char *s, Operand *op)
{
    s = trim(s);
    memset(op, 0, sizeof *op);

    if (*s == 0) {
        error(a, "missing operand");
        return false;
    }
    if (*s == '#') {
        op->mode  = OP_IMM;
        op->value = eval(a, s + 1, &op->label);
    } else if (*s == '&') {
        op->mode  = OP_ABS;
        op->value = eval(a, s + 1, NULL);
    } else if (*s == '@') {
        size_t n = strlen(s);

        op->mode = (s[n - 1] == '+') ? OP_INC : OP_IND;
        if (op->mode == OP_INC)
            s[n - 1] = 0;
        op->reg = reg_number(trim(s + 1));
        if (op->reg < 0) {
            error(a, "bad register '%s'", s + 1);
            return false;
        }
    } else if ((op->reg = reg_number(s)) >= 0) {
        op->mode = OP_REG;
    } else {
        size_t n    = strlen(s);
        char *paren = strrchr(s, '(');

        if (s[n - 1] == ')' && paren != NULL) {
            s[n - 1] = 0;
            op->reg  = reg_number(trim(paren + 1));
            if (op->reg >= 0) {
                *paren    = 0;
                op->mode  = OP_IDX;
                op->value = eval(a, s, NULL);
                return !a->failed;
            }
            s[n - 1] = ')';
        }
        op->mode  = OP_SYM;
        op->value = eval(a, s, NULL);
    }
    return !a->failed;
}

/* ---- output ---- */

static void emit(Asm *a, uint16_t word)
{
    uint16_t at = a->loc[a->sect];

    if (a->pass == 2) {
        a->emu->mem[at]                 = word;
        a->emu->mem[(uint16_t)(at + 1)] = word >> 8;
    }
    a->loc[a->sect] = at + 2;
}

static void emit_byte(Asm *a, uint8_t b)
{
    if (a->pass == 2)
        a->emu->mem[a->loc[a->sect]] = b;
    a->loc[a->sect]++;
}

/* Addressing fields of an operand; *ext is set when an extension word follows. */
static void encode(Asm *a, Operand *op, bool byte, bool is_source, int *mode, int *reg, bool *ext)
{
    *ext  = false;
    *reg  = op->reg;
    *mode = 0;

    switch (op->mode) {
    case OP_REG:
        *mode = 0;
        return;
    case OP_IND:
        if (!is_source) { /* @Rn as destination means 0(Rn) */
            op->mode  = OP_IDX;
            op->value = 0;
            *mode     = 1;
            *ext      = true;
            return;
        }
        *mode = 2;
        return;
    case OP_INC:
        if (!is_source)
            error(a, "@Rn+ is not a destination");
        *mode = 3;
        return;
    case OP_IMM:
        if (!is_source) {
            error(a, "immediate is not a destination");
            return;
        }
        if (!op->label) {
            uint16_t v = op->value;

            if (byte && v == 0xFF)
                v = 0xFFFF;
            switch (v) {
            case 0x0000:
                *mode = 0, *reg = 3;
                return;
            case 0x0001:
                *mode = 1, *reg = 3;
                return;
            case 0x0002:
                *mode = 2, *reg = 3;
                return;
            case 0x0004:
                *mode = 2, *reg = 2;
                return;
            case 0x0008:
                *mode = 3, *reg = 2;
                return;
            case 0xFFFF:
                *mode = 3, *reg = 3;
                return;
            }
        }
        *mode = is_source ? 3 : 1, *reg = 0, *ext = true;
        return;
    case OP_ABS:
        *mode = 1, *reg = 2, *ext = true;
        return;
    case OP_IDX:
        *mode = 1, *ext = true;
        return;
    case OP_SYM:
        *mode = 1, *reg = 0, *ext = true;
        return;
    }
}

static void emit_ext(Asm *a, const Operand *op)
{
    if (op->mode == OP_SYM) /* PC-relative to the extension word */
        emit(a, op->value - a->loc[a->sect]);
    else
        emit(a, op->value);
}

/* ---- instructions ---- */

static const char *const two_ops[] = { "mov",  "add", "addc", "subc", "sub", "cmp",
                                       "dadd", "bit", "bic",  "bis",  "xor", "and" };
static const char *const one_ops[] = { "rrc", "swpb", "rra", "sxt", "push", "call", "reti" };
static const struct {
    const char *name;
    int cond;
} jumps[] = { { "jne", 0 }, { "jnz", 0 }, { "jeq", 1 }, { "jz", 1 },  { "jnc", 2 }, { "jlo", 2 },
              { "jc", 3 },  { "jhs", 3 }, { "jn", 4 },  { "jge", 5 }, { "jl", 6 },  { "jmp", 7 } };

/* Emulated instructions: "%s" stands for the operand, "%b" for the size suffix. */
static const struct {
    const char *name;
    const char *expansion;
} emulated[] = {
    { "nop", "mov #0, r3" },       { "ret", "mov @sp+, pc" },     { "br", "mov %s, pc" },
    { "pop", "mov%b @sp+, %s" },   { "clr", "mov%b #0, %s" },     { "inc", "add%b #1, %s" },
    { "incd", "add%b #2, %s" },    { "dec", "sub%b #1, %s" },     { "decd", "sub%b #2, %s" },
    { "tst", "cmp%b #0, %s" },     { "inv", "xor%b #-1, %s" },    { "rla", "add%b %s, %s" },
    { "rlc", "addc%b %s, %s" },    { "adc", "addc%b #0, %s" },    { "sbc", "subc%b #0, %s" },
    { "dadc", "dadd%b #0, %s" },   { "setc", "bis #1, sr" },      { "setz", "bis #2, sr" },
    { "setn", "bis #4, sr" },      { "clrc", "bic #1, sr" },      { "clrz", "bic #2, sr" },
    { "clrn", "bic #4, sr" },      { "eint", "bis #8, sr" },      { "dint", "bic #8, sr" },
    { "pass", "mov #0, &0x01fe" }, { "fail", "mov #1, &0x01fe" },
};

static void instruction(Asm *a, char *mnemonic, char *args);

static bool expand(Asm *a, const char *name, const char *suffix, char *args)
{
    for (size_t i = 0; i < sizeof emulated / sizeof *emulated; i++) {
        if (strcmp(emulated[i].name, name) != 0)
            continue;

        char text[256] = "", *out = text;
        for (const char *t = emulated[i].expansion; *t; t++) {
            if (t[0] == '%' && t[1] == 's') {
                out += snprintf(out, text + sizeof text - out, "%s", args);
                t++;
            } else if (t[0] == '%' && t[1] == 'b') {
                out += snprintf(out, text + sizeof text - out, "%s", suffix);
                t++;
            } else if (out < text + sizeof text - 1) {
                *out++ = *t;
                *out   = 0;
            }
        }
        char *sp = strchr(text, ' ');
        *sp      = 0;
        instruction(a, text, sp + 1);
        return true;
    }
    return false;
}

static void instruction(Asm *a, char *mnemonic, char *args)
{
    char name[16], suffix[4] = "";
    bool byte = false;
    char *dot = strchr(mnemonic, '.');

    if (dot) {
        if (strcasecmp(dot, ".b") == 0)
            byte = true;
        else if (strcasecmp(dot, ".w") != 0)
            return error(a, "bad size suffix %s", dot);
        snprintf(suffix, sizeof suffix, "%s", dot);
        *dot = 0;
    }
    snprintf(name, sizeof name, "%s", mnemonic);
    for (char *p = name; *p; p++)
        *p = tolower((unsigned char)*p);
    args = trim(args);

    for (int op = 0; op < 12; op++) {
        if (strcmp(name, two_ops[op]) != 0)
            continue;

        char *comma = strchr(args, ',');
        Operand src, dst;
        int as, sreg, ad, dreg;
        bool sext, dext;

        if (comma == NULL)
            return error(a, "%s needs two operands", name);
        *comma = 0;
        if (!parse_operand(a, args, &src) || !parse_operand(a, comma + 1, &dst))
            return;
        encode(a, &src, byte, true, &as, &sreg, &sext);
        encode(a, &dst, byte, false, &ad, &dreg, &dext);
        emit(a, (op + 4) << 12 | sreg << 8 | ad << 7 | byte << 6 | as << 4 | dreg);
        if (sext)
            emit_ext(a, &src);
        if (dext)
            emit_ext(a, &dst);
        return;
    }

    for (int op = 0; op < 7; op++) {
        if (strcmp(name, one_ops[op]) != 0)
            continue;

        Operand src = { .mode = OP_REG };
        int as = 0, reg = 0;
        bool ext = false;

        if (op == 6) { /* reti */
            emit(a, 0x1300);
            return;
        }
        if (!parse_operand(a, args, &src))
            return;
        encode(a, &src, byte, true, &as, &reg, &ext);
        emit(a, 0x1000 | op << 7 | byte << 6 | as << 4 | reg);
        if (ext)
            emit_ext(a, &src);
        return;
    }

    for (size_t j = 0; j < sizeof jumps / sizeof *jumps; j++) {
        if (strcmp(name, jumps[j].name) != 0)
            continue;

        int32_t offset = eval(a, args, NULL) - (a->loc[a->sect] + 2);

        if (a->pass == 2 && (offset & 1 || offset < -1024 || offset > 1022))
            error(a, "jump target out of range");
        emit(a, 0x2000 | jumps[j].cond << 10 | ((offset / 2) & 0x3FF));
        return;
    }

    if (!expand(a, name, suffix, args))
        error(a, "unknown instruction %s", name);
}

/* ---- lines and directives ---- */

static void directive(Asm *a, char *name, char *args)
{
    args = trim(args);

    if (strcmp(name, ".set") == 0 || strcmp(name, ".equ") == 0) {
        char *comma = strchr(args, ',');
        bool label;

        if (comma == NULL)
            return error(a, "%s needs a name and a value", name);
        *comma       = 0;
        a->lazy      = true;
        a->undefined = false;
        int32_t v    = eval(a, comma + 1, &label);
        a->lazy      = false;
        if (!a->undefined) /* like gas, complain only if it is used */
            define(a, trim(args), v, label);
    } else if (strcmp(name, ".word") == 0 || strcmp(name, ".byte") == 0) {
        for (char *item = strtok(args, ","); item; item = strtok(NULL, ",")) {
            int32_t v = eval(a, item, NULL);
            if (name[1] == 'w')
                emit(a, v);
            else
                emit_byte(a, v);
        }
    } else if (strcmp(name, ".text") == 0) {
        a->sect = SECT_TEXT;
    } else if (strcmp(name, ".data") == 0) {
        a->sect = SECT_DATA;
    } else if (strcmp(name, ".section") == 0) {
        char *comma = strchr(args, ',');
        if (comma)
            *comma = 0;
        args = trim(args);
        if (strcmp(args, ".vectors") == 0)
            a->sect = SECT_VECTORS;
        else if (strcmp(args, ".text") == 0)
            a->sect = SECT_TEXT;
        else if (strcmp(args, ".data") == 0 || strcmp(args, ".bss") == 0)
            a->sect = SECT_DATA;
        else
            error(a, "unknown section %s", args);
    } else if (strcmp(name, ".even") == 0) {
        if (a->loc[a->sect] & 1)
            emit_byte(a, 0);
    } else if (strcmp(name, ".global") == 0 || strcmp(name, ".globl") == 0 ||
               strcmp(name, ".type") == 0 || strcmp(name, ".include") == 0) {
        /* nothing to do; start/pass/fail stand in for testutils.inc */
    } else {
        error(a, "unknown directive %s", name);
    }
}

static void assemble_line(Asm *a, char *line)
{
    char *p = trim(line);

    /* labels */
    for (;;) {
        char *colon = p;

        while (isalnum((unsigned char)*colon) || *colon == '_' || *colon == '.' || *colon == '$')
            colon++;
        if (colon == p || *colon != ':')
            break;
        *colon = 0;
        if (isdigit((unsigned char)p[0]) && p[1] == 0) {
            char local[16];
            int n = p[0] - '0';

            snprintf(local, sizeof local, "%d\001%d", n, a->local_defs[n]++);
            define(a, local, a->loc[a->sect], true);
        } else {
            define(a, p, a->loc[a->sect], true);
        }
        p = trim(colon + 1);
    }
    if (*p == 0)
        return;

    char *args = p;
    while (*args && !isspace((unsigned char)*args))
        args++;
    if (*args)
        *args++ = 0;

    if (strcmp(p, "start") == 0)
        return;
    if (p[0] == '.')
        directive(a, p, args);
    else
        instruction(a, p, args);
}

/* Blank out comments: block comments (keeping newlines), ';' to end of line,
 * and lines starting with '#'. */
static void strip_comments(char *s)
{
    bool line_start = true;

    for (char *p = s; *p; p++) {
        if (p[0] == '/' && p[1] == '*') {
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p != '\n')
                    *p = ' ';
                p++;
            }
            if (*p) {
                p[0] = p[1] = ' ';
                p++;
            }
            continue;
        }
        if (*p == ';' || (line_start && *p == '#')) {
            while (*p && *p != '\n')
                *p++ = ' ';
            if (*p == 0)
                break;
        }
        if (*p == '\n')
            line_start = true;
        else if (!isspace((unsigned char)*p))
            line_start = false;
    }
}

int asm_text(Emulator *emu, const char *text, char *err, size_t errsize)
{
    Asm a     = { .emu = emu, .err = err, .errsize = errsize };
    char *src = strdup(text);

    strip_comments(src);
    for (a.pass = 1; a.pass <= 2 && !a.failed; a.pass++) {
        char *copy = strdup(src), *line = copy, *next;

        memcpy(a.loc, sect_org, sizeof a.loc);
        memset(a.local_defs, 0, sizeof a.local_defs);
        a.sect = SECT_TEXT;
        a.line = 0;
        define(&a, "__data_start", ASM_DATA, false);

        for (; line && !a.failed; line = next) {
            next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            a.line++;
            assemble_line(&a, line);
        }
        free(copy);
    }
    free(src);
    free(a.syms);
    return a.failed ? -1 : 0;
}

int asm_file(Emulator *emu, const char *path, char *err, size_t errsize)
{
    FILE *f = fopen(path, "r");
    char *text;
    long n;
    int status;

    if (f == NULL) {
        snprintf(err, errsize, "cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    text    = malloc(n + 1);
    n       = fread(text, 1, n, f);
    text[n] = 0;
    fclose(f);
    status = asm_text(emu, text, err, errsize);
    free(text);
    return status;
}

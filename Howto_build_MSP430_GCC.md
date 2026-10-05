# How to build GCC for MSP430 from source

This guide builds a C cross-compiler for the Texas Instruments MSP430
microcontrollers on a Mac (Apple Silicon, macOS). You end up with
`msp430-elf-gcc`, the assembler, the linker and a small C library, all
installed in `~/.local/bin`.

It was tested in October 2026 with binutils 2.47, GCC 16.2.0, newlib 4.6.0
and macOS 27. Expect about 30–40 minutes of build time on an 8-core machine.
The build trees need about 5.5 GB of disk.

## 1. What you are building, and why there are four steps

A *cross-compiler* runs on your Mac but produces code for another CPU, here
the MSP430. The target name is **`msp430-elf`**, and every tool gets it as a
prefix: `msp430-elf-gcc`, `msp430-elf-ld` and so on.

A working toolchain has three parts:

| Part | Package | What it gives you |
|---|---|---|
| Assembler, linker, binary tools | **binutils** | `msp430-elf-as`, `-ld`, `-objdump`, `-size`, … |
| Compiler | **GCC** | `msp430-elf-gcc`, plus `libgcc` (helper routines) |
| C library | **newlib** (and its **libgloss**) | `printf`, `strcpy`, `malloc`, startup code, linker scripts |

They depend on each other in a circle: GCC needs the C library's headers, and
the C library must be compiled by GCC. You break the circle by building GCC
twice:

1. **binutils**
2. **GCC, stage 1**: a bare C compiler that needs no C library
3. **newlib**, compiled by the stage-1 GCC
4. **GCC, final**: rebuilt so that it knows about newlib

All four install into the same place (the *prefix*). Each later step finds
the earlier ones there.

## 2. Prepare

### Tools and libraries

You need the Xcode command-line tools (they provide `clang`, `make` and the
macOS SDK) and Homebrew:

```sh
xcode-select --install       # skip if already installed
brew install gmp mpfr libmpc
```

GMP, MPFR and MPC are maths libraries that GCC itself uses at compile time.

**Optional:** `brew install texinfo`. It provides `makeinfo`, which builds the
manuals. Without it the build fails, unless you pass `MAKEINFO=true` as shown
below, which skips the manuals.

### Directories and variables

Use one working directory for sources and builds. Never build inside a source
tree: GCC refuses to, and the others misbehave. Set these variables in the
terminal you build in; every command below uses them:

```sh
export WORK=~/msp430-gcc           # sources and build trees
export PREFIX=~/.local             # where the toolchain is installed
export PATH=$PREFIX/bin:$PATH      # so later steps find earlier tools
export BREW=/opt/homebrew/opt      # where Homebrew keeps gmp/mpfr/libmpc
mkdir -p $WORK && cd $WORK
```

The tools land in `$PREFIX/bin`. Make sure that directory is in your `PATH`
permanently too (add the `export PATH` line to `~/.zshrc` or `~/.bash_profile`).

## 3. Download the sources

```sh
cd $WORK
curl -LO https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz
curl -LO https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz
curl -LO https://sourceware.org/pub/newlib/newlib-4.6.0.20260123.tar.gz
for f in *.tar.*; do tar xf $f; done
```

To use newer versions, look at the directory listings at
<https://ftp.gnu.org/gnu/binutils/>, <https://ftp.gnu.org/gnu/gcc/> and
<https://sourceware.org/pub/newlib/>, then adjust the names everywhere below.

## 4. Step 1: binutils

```sh
mkdir -p $WORK/build-binutils && cd $WORK/build-binutils
../binutils-2.47/configure --target=msp430-elf --prefix=$PREFIX \
    --disable-nls --disable-werror --disable-gdb --disable-sim --disable-gprofng
make -j8
make install
```

| Option | Meaning |
|---|---|
| `--target=msp430-elf` | Build tools for the MSP430 |
| `--prefix=$PREFIX` | Install into `$PREFIX/bin`, `$PREFIX/msp430-elf`, … |
| `--disable-nls` | No translated messages; English only, faster build |
| `--disable-werror` | Do not stop on compiler warnings (new compilers warn more) |
| `--disable-gdb --disable-sim --disable-gprofng` | Skip the debugger, simulator and profiler, which are not needed |

`-j8` runs 8 compile jobs in parallel. Use your number of CPU cores
(`sysctl -n hw.ncpu`).

Check it: `msp430-elf-as --version` should print the version.

## 5. Step 2: GCC, stage 1

```sh
mkdir -p $WORK/build-gcc1 && cd $WORK/build-gcc1
../gcc-16.2.0/configure --target=msp430-elf --prefix=$PREFIX \
    --enable-languages=c --without-headers --with-newlib \
    --with-system-zlib --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libquadmath --disable-libgomp \
    --with-gmp=$BREW/gmp --with-mpfr=$BREW/mpfr --with-mpc=$BREW/libmpc \
    MAKEINFO=true
make -j8 all-gcc all-target-libgcc MAKEINFO=true
make install-gcc install-target-libgcc MAKEINFO=true
```

| Option | Meaning |
|---|---|
| `--enable-languages=c` | C only. C++ does not build for MSP430 (see §9) |
| `--without-headers` | There is no C library yet; don't look for its headers |
| `--with-newlib` | The C library will be newlib |
| `--with-system-zlib` | Use macOS's zlib. GCC's bundled copy does not compile with recent macOS SDKs |
| `--disable-shared --disable-threads` | A microcontroller has no shared libraries and no threads |
| `--disable-libssp/-libquadmath/-libgomp` | Skip runtime libraries that make no sense on the MSP430 |
| `--with-gmp/-mpfr/-mpc` | Where Homebrew put the maths libraries |
| `MAKEINFO=true` | Skip the manuals if `makeinfo` is missing |

`all-gcc all-target-libgcc` builds only the compiler and its helper library.
A full `make` would fail at this stage, because the rest needs a C library.

Check it: `msp430-elf-gcc --version`.

## 6. Step 3: newlib

```sh
mkdir -p $WORK/build-newlib && cd $WORK/build-newlib
../newlib-4.6.0.20260123/configure --target=msp430-elf --prefix=$PREFIX \
    --disable-newlib-supplied-syscalls --enable-newlib-reent-small \
    --disable-newlib-fseek-optimization --disable-newlib-wide-orient \
    --enable-newlib-nano-formatted-io --disable-newlib-io-float \
    --enable-newlib-nano-malloc --disable-newlib-unbuf-stream-opt \
    --enable-lite-exit --enable-newlib-global-atexit --disable-nls
make -j8 MAKEINFO=true
make install MAKEINFO=true
```

These are the options TI uses for its own MSP430 toolchain. Together they
make the library as small as possible, since the chips have only kilobytes of
memory. The most noticeable effects:

- `--enable-newlib-nano-formatted-io`: a compact `printf`.
- `--disable-newlib-io-float`: no floating point in `printf`/`scanf`; `%f`,
  `%e` and `%g` don't work. If you need them, rebuild newlib without this
  option and link with `-u _printf_float` (several KB of extra code).
- `--enable-newlib-nano-malloc`: a small, simple `malloc`.
- `--enable-lite-exit`: a smaller `exit()`.

The build compiles the library once per *multilib* (CPU and memory-model
variant, see §8), so it takes a while.

## 7. Step 4: GCC, final

The same as stage 1, minus `--without-headers`, in a fresh build directory:

```sh
mkdir -p $WORK/build-gcc2 && cd $WORK/build-gcc2
../gcc-16.2.0/configure --target=msp430-elf --prefix=$PREFIX \
    --enable-languages=c --with-newlib \
    --with-system-zlib --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libquadmath --disable-libgomp \
    --with-gmp=$BREW/gmp --with-mpfr=$BREW/mpfr --with-mpc=$BREW/libmpc \
    MAKEINFO=true
make -j8 MAKEINFO=true
make install MAKEINFO=true
```

This overwrites the stage-1 compiler with the final one.

During `make` you may see `clang++: error: unsupported option
'-print-multi-os-directory'`. That is a harmless probe; ignore it. A real
failure stops the build with `*** [...] Error` and a non-zero exit status.

## 8. Test it

Write a small program, `t.c`:

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[16];
    strcpy(buf, "hello");
    printf("%s %d\n", buf, (int)sizeof(int));
    return 0;
}
```

Compiling (`-c`, `-S`) needs no extra options:

```sh
msp430-elf-gcc -O2 -c t.c            # object file t.o
msp430-elf-gcc -O2 -S t.c            # assembly listing t.s
```

**Linking needs a linker script**, which describes the chip's memory map.
newlib installed two generic ones for the simulator:

```sh
msp430-elf-gcc -O2 t.c -T msp430-sim.ld -o t.elf                 # MSP430X, small model
msp430-elf-gcc -O2 -mlarge t.c -T msp430xl-sim.ld -o t.elf       # MSP430X, large model
msp430-elf-gcc -O2 -mcpu=msp430 t.c -T msp430-sim.ld -o t.elf    # original MSP430
msp430-elf-size t.elf
```

The `-mcpu`/`-mlarge` options pick the *multilib*, the library variant
compiled for that CPU and memory model. `msp430-elf-gcc -print-multi-lib`
lists them all:

| Options | CPU | Pointers / code reach |
|---|---|---|
| *(none)* | MSP430X | 16-bit, 64 KB |
| `-mcpu=msp430` | original MSP430 (no 430X instructions) | 16-bit, 64 KB |
| `-mlarge` | MSP430X | 20-bit, 1 MB |

The linker may warn `LOAD segment with RWX permissions`. That is normal for
these simple scripts.

## 9. Problems you may hit

**`undefined reference to 'end'`** when linking without `-T`. There is no
default memory map, so pass a linker script (see §8).

**`cannot open linker script file msp430f5529.ld`** or `could not locate MCU
data file 'devices.csv'` when you use `-mmcu=<chip>`. The per-chip linker
scripts and headers (`msp430.h`, `msp430f5529.ld`, …) are not part of GCC.
They are in TI's free "MSP430 GCC support files" archive, from the
MSP430-GCC-OPENSOURCE page on ti.com. Unpack it and add
`-I<dir>/include -L<dir>/include` to the command line.

**`makeinfo is missing` / `porting.info Error 127`.** Add `MAKEINFO=true` to
the `make` command, or `brew install texinfo`.

**`error: expected identifier or '('` in macOS's `_stdio.h`, while building
`zlib`.** GCC's bundled zlib clashes with new macOS SDKs. Configure GCC with
`--with-system-zlib`.

**`internal compiler error: in extract_constrain_insn` in `fs_path.cc`.** You
enabled C++ (`--enable-languages=c,c++`). The MSP430 code generator crashes
while compiling the C++ library. This happens with both GCC 15.3 and 16.2.
Build C only.

**`Error: file was compiled for the 430 ISA but the 430X ISA is selected`.**
The CPU options of different files or libraries don't match. Use the same
`-mcpu`/`-mlarge` options for every compile and link command.

**A step failed halfway.** Fix the cause, delete that step's build directory
(`rm -rf $WORK/build-…`) and start the step again from `configure`. For a
failure in the docs only (`MAKEINFO`), simply re-running `make` with the
missing option is enough.

## 10. Clean up

Once `make install` of the final GCC has succeeded, the work directory is no
longer needed:

```sh
rm -rf $WORK
```

The installed toolchain lives in:

- `$PREFIX/bin/msp430-elf-*`: the programs
- `$PREFIX/msp430-elf/`: newlib headers, libraries, linker scripts
- `$PREFIX/lib/gcc/msp430-elf/` and `$PREFIX/libexec/gcc/msp430-elf/`: compiler internals

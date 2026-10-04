#!/bin/sh
# End-to-end tests of the mspsim binary: cli_test.sh path/to/mspsim
MSPSIM=$1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
failed=0

# bin FILE WORD...: write little-endian 16-bit words
bin() {
    out=$1
    shift
    : >"$out"
    for w; do
        printf "\\$(printf %o $((w & 255)))\\$(printf %o $((w >> 8)))" >>"$out"
    done
}

# tohex BINFILE ADDR: Intel HEX data records, 16 bytes each
tohex() {
    od -An -v -tx1 "$1" | tr -s ' \n' '\n\n' | grep . | {
        addr=$2
        n=0
        line=""
        sum=0
        flush() {
            [ $n -eq 0 ] && return
            sum=$((sum + n + (addr >> 8) + (addr & 255)))
            printf ':%02X%04X00%s%02X\n' $n $addr "$line" $(((256 - sum % 256) % 256))
            addr=$((addr + n))
            n=0
            line=""
            sum=0
        }
        while read -r b; do
            line="$line$(printf %02X 0x$b)"
            sum=$((sum + 0x$b))
            n=$((n + 1))
            [ $n -eq 16 ] && flush
        done
        flush
    }
}

# check NAME EXPECTED_STATUS EXPECTED_STDOUT STATUS STDOUT
check() {
    if [ "$4" != "$2" ] || [ "$5" != "$3" ]; then
        echo "FAIL $1: status $4 (expected $2), stdout '$5' (expected '$3')"
        failed=1
    else
        echo "ok   $1"
    fi
}

# Prints "Hello\n" from 0xC100 over the UART, then writes 0 to the stop register.
bin "$T/hello.bin" 0x403F 0xC100 0x4F7E 0x934E 0x2406 0xB3E2 0x0003 0x27FD \
    0x4EC2 0x0067 0x3FF7 0x4382 0x01FE
i=13
while [ $i -lt 128 ]; do
    bin "$T/w" 0xFFFF
    cat "$T/w" >>"$T/hello.bin"
    i=$((i + 1))
done
bin "$T/w" 0x6548 0x6C6C 0x0A6F 0x0000
cat "$T/w" >>"$T/hello.bin"

# Echoes input until 'q', then stops with exit code 5.
bin "$T/echo.bin" 0xB3D2 0x0003 0x27FD 0x4255 0x0066 0x45C2 0x0067 0x9075 0x0071 \
    0x23F6 0x40B2 0x0005 0x01FE

bin "$T/loop.bin" 0x3FFF            # jmp $
bin "$T/illegal.bin" 0x0000         # illegal
bin "$T/sleep.bin" 0xD032 0x0010    # bis #CPUOFF, sr
printf '\177ELF' >"$T/fake.elf"

# hello as Intel HEX, with a reset vector record, then end of file
{
    tohex "$T/hello.bin" 49152
    echo ":02FFFE0000C041"
    echo ":00000001FF"
} >"$T/hello.hex"
sed 's/^:02FFFE0000C041$/:02FFFE0000C042/' "$T/hello.hex" >"$T/badsum.hex"

out=$("$MSPSIM" -q -b 0xC000 "$T/hello.bin" 2>"$T/err")
check hello 0 "Hello" $? "$out"
check quiet_stderr "" "" "" "$(cat "$T/err")"

out=$("$MSPSIM" -b 0xC000 "$T/hello.bin" 2>"$T/err")
grep -q "Loaded" "$T/err" && grep -q "Exit code 0" "$T/err"
check banner 0 "Hello" $? "$out"

out=$(printf 'abc q tail' | "$MSPSIM" -q -b 0xC000 "$T/echo.bin")
check echo 5 "abc q" $? "$out"

out=$("$MSPSIM" -q -n 1000 -b 0xC000 "$T/loop.bin")
check max_cycles 124 "" $? "$out"

out=$("$MSPSIM" -q -b 0xC000 "$T/illegal.bin")
check illegal 132 "" $? "$out"

out=$("$MSPSIM" -q -b 0xC000 "$T/sleep.bin")
check sleep 125 "" $? "$out"

out=$("$MSPSIM" -q -b 0xC000 "$T/hello.bin")
check binary_addr 0 "Hello" $? "$out"

out=$("$MSPSIM" -q -b 0x10000 "$T/hello.bin" 2>/dev/null)
check bad_addr 2 "" $? "$out"

out=$("$MSPSIM" -q "$T/fake.elf" 2>/dev/null)
check bad_elf 1 "" $? "$out"

out=$("$MSPSIM" -q "$T/hello.bin" 2>"$T/err")
status=$?
grep -q "not an ELF or Intel HEX file" "$T/err"
check raw_needs_b "1 0" "" "$status $?" "$out"

out=$("$MSPSIM" -q "$T/hello.hex")
check ihex 0 "Hello" $? "$out"

out=$("$MSPSIM" -q "$T/badsum.hex" 2>"$T/err")
status=$?
grep -q "bad checksum" "$T/err"
check ihex_checksum "1 0" "" "$status $?" "$out"

out=$("$MSPSIM" -q "$T/missing.bin" 2>/dev/null)
check missing_file 1 "" $? "$out"

out=$("$MSPSIM" 2>/dev/null)
check no_args 2 "" $? "$out"

out=$("$MSPSIM" -q -o "$T/trace" -b 0xC000 "$T/hello.bin")
status=$?
head -1 "$T/trace" | grep -q "^c000: 403f c100        mov   #0xc100, r15$" && grep -q "Writeb \[0067\] = 48" "$T/trace"
check trace_file "0 0" "Hello" "$status $?" "$out"

out=$("$MSPSIM" -q -t -b 0xC000 "$T/hello.bin" 2>"$T/err")
status=$?
grep -q "^c016: 4382 01fe        mov   #0x0000, &0x01fe$" "$T/err"
check trace_stderr "0 0" "Hello" "$status $?" "$out"

out=$(printf 'step\n\ndis 1 C016\nquit\n' | "$MSPSIM" -q -g -b 0xC000 "$T/hello.bin")
status=$?
echo "$out" | grep -q "^c004:" && echo "$out" | grep -q "^c006:" && echo "$out" | grep -q "^c016:"
check debugger_step "0 0" "" "$status $?" ""

out=$(printf 'break C016\nrun\nquit\n' | "$MSPSIM" -q -g -b 0xC000 "$T/hello.bin")
status=$?
echo "$out" | grep -q "Breakpoint 1 hit" && echo "$out" | grep -q "^Hello"
check debugger_break "0 0" "" "$status $?" ""

exit $failed

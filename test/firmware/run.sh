#!/bin/sh
# run.sh MSPSIM FIRMWARE.elf EXPECTED [INPUT]: output and exit status 0 must match
input=${4:-/dev/null}
out=$("$1" -q -n 100000000 "$2" < "$input")
status=$?
if [ $status -ne 0 ]; then
    echo "$2: exit status $status"
    exit 1
fi
if [ "$out" != "$(cat "$3")" ]; then
    echo "$2: output differs:"
    echo "$out"
    exit 1
fi

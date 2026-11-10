#!/bin/sh
# Spike: generate C for the Thumb functions of the ROM, compile it with TinyCC and compare it against Unicorn.
#   tools/spike.sh <rom.gba> <work dir> [cases per function] [seed]
# Needs: cargo build --release; TCC=<path to tcc>; PY=<python with unicorn installed>
set -e
ROM=$1; W=$2; CASES=${3:-8}; SEED=${4:-1}
TCC=${TCC:-tcc}; PY=${PY:-python}
mkdir -p "$W"
./target/release/gba-recomp gen "$ROM" "$W/gen.c" --pointers --overlay 030056F0:080B197C:135C --overlay 0300404C:080AF3A4:380
time "$TCC" -shared -rdynamic -Iruntime -o "$W/gen.dll" "$W/gen.c" runtime/gba_rt.c
"$PY" tools/spike_diff.py "$ROM" "$W/gen.dll" "$W/gen.c.list" "$CASES" "$SEED"

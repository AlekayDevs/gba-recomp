#!/bin/sh
# The run-and-complete loop: static discovery misses some targets of indirect jumps; the runtime reports each one it hits
# ("jump to 0x... from 0x..., not a translated function"), the address goes into a roots file, and the C is generated again.
#   tools/complete.sh <rom.gba> <work dir> <frames> [extra gba-recomp options...]
# Needs: cargo build --release; TCC=<path to tcc>. The roots file (<work dir>/roots.txt) is what the game's profile should keep.
set -e
ROM=$1; W=$2; FRAMES=$3; shift 3
TCC=${TCC:-tcc}
mkdir -p "$W"; touch "$W/roots.txt"
i=0
while [ $i -lt 200 ]; do
  ./target/release/gba-recomp gen "$ROM" "$W/gen.c" --pointers --roots-file "$W/roots.txt" "$@" | sed -n 2p
  "$TCC" -Iruntime -o "$W/run.exe" "$W/gen.c" runtime/gba_cpu.c runtime/gba_bus.c runtime/gba_sys.c runtime/gba_bios.c runtime/run_headless.c
  set +e
  "$W/run.exe" "$ROM" "$FRAMES" > "$W/out.txt" 2> "$W/err.txt"
  rc=$?
  set -e
  miss=$(sed -n 's/^jump to 0x\([0-9A-F]*\) from.*/\1/p' "$W/err.txt" | head -1)
  if [ -z "$miss" ]; then cat "$W/err.txt" "$W/out.txt"; echo "rc=$rc after $i added roots"; exit 0; fi
  echo "$miss" >> "$W/roots.txt"
  i=$((i+1))
done
echo "gave up after $i roots"; exit 1

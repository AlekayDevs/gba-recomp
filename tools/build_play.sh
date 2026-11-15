#!/bin/sh
# Builds the windowed player: generates the C (with the roots found by tools/complete.sh), compiles it with TinyCC and links SDL2.
#   tools/build_play.sh <rom.gba> <work dir> <SDL2 dir (include/ and lib/x64/SDL2.dll)> [extra gba-recomp options...]
# Needs: cargo build --release; TCC=<path to tcc>. The roots file is <work dir>/roots.txt (the one complete.sh keeps).
set -e
ROM=$1; W=$2; SDL=$3; shift 3
TCC=${TCC:-tcc}
mkdir -p "$W"; touch "$W/roots.txt"
cp "$SDL/lib/x64/SDL2.dll" "$W/SDL2.dll"
(cd "$W" && "$TCC" -impdef SDL2.dll)
./target/release/gba-recomp gen "$ROM" "$W/gen.c" --pointers --roots-file "$W/roots.txt" "$@" | sed -n 2p
"$TCC" -Iruntime -I"$SDL/include" -I"$SDL/include/SDL2" -L"$W" -o "$W/play.exe" "$W/gen.c" runtime/gba_cpu.c runtime/gba_bus.c runtime/gba_sys.c runtime/gba_ppu.c runtime/gba_apu.c runtime/gba_state.c runtime/gba_save.c runtime/gba_bios.c runtime/play.c -lSDL2
echo "built $W/play.exe: run it as  $W/play.exe <rom.gba>"

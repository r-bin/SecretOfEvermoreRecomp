#!/usr/bin/env bash
# A console reset must bring the game back as power-on does: 900 frames after power-on and 900 NMIs after a reset
# (taken at frame 300), the NMI counter $7E0100 must be equal and VRAM, CGRAM and OAM identical. WRAM is not
# compared whole: the attract demo seeds its RNG from the H counter, and after a reset the beam stands elsewhere
# relative to the program, as on hardware (36 bytes differ: the RNG state $0133-$013A, direct-page scratch, the
# stack below $1FE2). Pins the bug where g_cpu kept the game's 16-bit widths through a reset (docs/bugs-and-todos.md).
#
#   tests/reset_equals_boot.sh <game executable> <rom>     (ctest: -DSOE_TEST_ROM=/path/to/rom.sfc)
set -euo pipefail
EXE=$1 ROM=$2
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/dev/null

run() {   # <name> <script>
    mkdir -p "$WORK/$1" "$WORK/$1-saves"
    printf '%b' "$2" > "$WORK/$1.txt"
    SNESRECOMP_DUMP_DIR="$WORK/$1" SOE_COMPARE_SAVE_ROOT="$WORK/$1-saves" \
        "$EXE" --no-launcher --script "$WORK/$1.txt" "$ROM" > "$WORK/$1.log" 2>&1
}
run boot  'turbo on\nwait 900\ndump s\nquit\n'
run reset 'turbo on\nwait 300\nreset\nwait 898\ndump s\nquit\n'

nmi() { od -An -tu2 -j256 -N2 "$WORK/$1/s.wram.bin" | tr -d ' '; }
fail=0
[ "$(nmi boot)" = "$(nmi reset)" ] || { echo "NMI counter: power-on $(nmi boot), after reset $(nmi reset)"; fail=1; }
for r in vram cgram oam; do
    cmp -s "$WORK/boot/s.$r.bin" "$WORK/reset/s.$r.bin" || { echo "$r differs after the reset"; fail=1; }
done
[ $fail = 0 ] && echo "reset equals power-on at NMI $(nmi boot)"
exit $fail

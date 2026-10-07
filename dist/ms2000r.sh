#!/bin/sh
# MS2000R emulator - standalone GUI (Linux).
#   ./ms2000r.sh            run from the current folder (or from $MS2K_HOME)
#   ./ms2000r.sh --log      also write ms2000_gui.log there (for bug reports)
# The working folder must be writable and hold flash.bin, "full FW/boot-362.ms2000.bin" and optionally
# hd44780_a00.bin. It receives thin_gui.ini, the flash state after a WRITE and recordings/*.wav.
# The binary is looked for beside this script, then in ../build.
here=$(cd "$(dirname "$0")" && pwd)
exe=
for c in "$here/ms2000_emulator" "$here/../build/ms2000_emulator"; do
    [ -x "$c" ] && exe=$c && break
done
[ -n "$exe" ] || { echo "ms2000_emulator not found (build it first, see README)" >&2; exit 1; }

[ -n "$MS2K_HOME" ] && cd "$MS2K_HOME" || true
[ -f flash.bin ] || { echo "flash.bin not found in $(pwd) - cd to the ROM folder or set MS2K_HOME" >&2; exit 1; }

log=/dev/null
if [ "$1" = "--log" ]; then log=ms2000_gui.log; shift; fi
exec "$exe" --rom flash.bin --gui --audio --power-on "$@" > "$log" 2>&1

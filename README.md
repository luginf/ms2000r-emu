# ms2000r-emu  (v0.9.3)

A hardware-level emulator of the **KORG MS2000R** (rack version of the MS2000 analog-modeling synthesizer):
the H8S/2350 main CPU runs the original firmware instruction by instruction, and the DSP56362 runs the
original DSP code on the dsp56300 emulator library. Nothing of the synth engine is re-implemented - the sound
comes from the machine's own code.

Standalone application (Windows, ImGui) with the full MS2000R front panel, and a VST3 instrument plugin (JUCE).

## Status

- The firmware boots and runs: programs, the vocoder, the arpeggiator, the motion/mod sequencer, the demo songs,
  MIDI IN and MIDI OUT (knob moves send CCs), Global and program WRITE (the flash is emulated, non-volatile).
- Front panel: all 32 knobs, the switch matrix, the LED matrix (as the firmware multiplexes it) and the 16x2 LCD
  including its custom characters. Shift+click on a program key 1-16 or EXIT keeps it held (chords, EXIT+GLOBAL).
- Audio In 1/2 (vocoder, OSC1 AUDIO IN) through a model of the input stage and the AK4522 codec.
- .syx: load a bank or program file into the machine and save all 128 programs as .syx (standalone: the buttons
  under the panel; plugin: Settings). The file goes into the emulated MIDI IN and the firmware itself stores it,
  as the real unit does from an editor; its DATA LOAD COMPLETED / ERROR answer is shown.
- Plugin extras: a Library page (open a .syx, click a program to hear it - it goes into the edit buffer, the memory is
  not changed); the host tempo as MIDI clock and the host transport as Start/Stop (GLOBAL: MIDI Clock = Ext, the
  arpeggiator starts on beat 1 with the host); Settings -> "Choose the MS2000 folder...".
- "Knobs show the program" (plugin Settings, standalone checkbox): the pots are drawn where the program being
  edited has its values (read from the machine's edit buffer); turning one takes over from there without a jump.
- VST3 (phase 2): the panel in the plugin window; the machine's state (knob positions, settings and the written
  flash sectors) is saved in the host project.
- Several plugin instances in one host: the plugin bundle carries the engine as
  `Contents\Resources\MS2000R_engine.dll`; every instance after the first runs from its own copy of it in
  `%TEMP%\MS2000R_engines` (removed when the host unloads the plugin), so each instance is a separate machine.

Known limits: Windows and Linux (standalone only) - the VST3 plugin is Windows only; old hosts (VSTHost) can hang when leaving the demo
songs. The emulator needs a recent CPU - it runs about 2x real time on a current desktop; the plugin runs at
96 kHz with a 128-sample buffer (measured: 0.62 ms median, 0.96 ms worst per 1.33 ms block after the boot).
Next: a preset library page.

## Download

Windows builds are on the [Releases](../../releases) page: `ms2000_emulator.exe` with its two launchers and the
`MS2000R.vst3` plugin. Put your ROM files (below) beside the exe, or in a folder above it, and start
`MS2000R.bat` (or `MS2000R (log).bat` to get `ms2000_gui.log` for a bug report). Copy `MS2000R.vst3` to your
VST3 folder (usually `C:\Program Files\Common Files\VST3`); on its first start open Settings -> "Choose the
MS2000 folder..." and pick the folder with the ROMs (remembered in `%APPDATA%\MS2000R\home.txt`). The Microsoft Visual C++ 2015-2022 x64 runtime is needed.

## What you need - NOT included

This repository contains **no firmware, no ROM and no manual**. You need dumps from your own hardware:

The working folder is found by itself: `MS2K_HOME` if set, else the current folder if it holds the two required ROMs,
else the exe's (or the plugin's) folder or the first folder above it that holds them. Settings (`thin_gui.ini`), the
machine's memory and `recordings\` are kept there.

| file (in the working folder) | what | required |
|---|---|---|
| `flash.bin` | the MS2000R main flash (1 MB, Am29LV800B) | yes |
| `full FW/boot-362.ms2000.bin` | the DSP56362 boot ROM (768 bytes = 192 words x 4, little endian) | yes (no sound without it) |
| `hd44780_a00.bin` | the LCD controller's character generator ROM (4096 bytes) | no (the LCD falls back to text) |

The emulator never writes `flash.bin`: what the firmware writes (WRITE) goes to `ms2000_flash_state.bin` /
`.sectors` beside it (standalone) or into the host project (plugin).

## Build (Windows)

Visual Studio 2022, CMake 3.16+, [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set (ImGui comes
from `vcpkg.json`).

```
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target ms2000_emulator
build\Release\ms2000_emulator.exe --gui
```

`dist\MS2000R.bat` and `dist\MS2000R (log).bat` are the launchers to put beside the exe.

## Build (Linux, standalone only)

g++ 13+ (or another C++17 compiler), CMake, Ninja, and the development packages for GLFW, OpenGL, ALSA and ImGui
(Debian/Ubuntu: `libglfw3-dev libasound2-dev libimgui-dev`). No vcpkg needed. The VST3 plugin is not ported.

```
cmake -S . -B build -G Ninja
cmake --build build --target ms2000_emulator
cd /folder/with/flash.bin && /path/to/dist/ms2000r.sh     # or: MS2K_HOME=/folder dist/ms2000r.sh [--log]
```

`dist/ms2000r.sh` is the Linux launcher (the counterpart of `MS2000R.bat`): it runs
`ms2000_emulator --rom flash.bin --gui --audio --power-on` in the current folder, or in `$MS2K_HOME` if set, finding the
binary beside the script or in `../build/`. `--log` writes `ms2000_gui.log` there (for a bug report); any other argument is
passed on to the emulator. The folder must be writable and hold `flash.bin`, `full FW/boot-362.ms2000.bin` and, optionally,
`hd44780_a00.bin`.

- Audio out/in go through ALSA PCM (`default`, `pipewire`, `pulse` and the `sysdefault:` endpoints are listed).
- MIDI goes through the ALSA sequencer: entry 0 of each list is a virtual port "MS2000R" (connect it with
  `aconnect` or a patchbay), the other entries are the existing hardware or software ports.
- .syx import/export asks for the file with `zenity` or `kdialog` (one of them must be installed).
- The working folder (ROMs, `thin_gui.ini`, `recordings/`) must be writable.
- Your user needs access to the sound server's session (and to `/dev/snd`, group `audio`, for hardware ports).

### AppImage

`.github/workflows/appimage.yml` builds `MS2000R-<version>-x86_64.AppImage` in a Debian 10 container (glibc 2.28, so it runs
on Debian 10+, Ubuntu 20.04+ and newer; CMake and ImGui v1.90.1 are downloaded and checksummed, GLFW is bundled; X11, GL and
ALSA come from the host). Run it by hand from the Actions tab, or push a `v*` tag to also publish it as a release.
The image contains **no firmware**: it looks for `flash.bin` in `$MS2K_HOME`, then in the folder it was started from, then in
`~/.local/share/MS2000R` (where it tells you to put the files if it finds none). The same
`-DMS2K_IMGUI_DIR=/path/to/imgui` CMake option builds against an ImGui source tree instead of the system library.

VST3 plugin (JUCE is fetched at configure time):

```
cmake -S . -B build_vst -G "Visual Studio 17 2022" -DMS2K_BUILD_VST3=ON
cmake --build build_vst --config Release --target MS2000R_Shim
```

The finished plugin bundle is `build_vst\VST3_multi\MS2000R.vst3` (the loader module plus the engine).

The plugin looks for the folder holding `flash.bin` in `MS2K_HOME`, then `Documents\MS2000R`, then the folders
above the plugin binary.

## Credits

- Written by **Claude** (Anthropic's AI), in a long collaboration with **Tamás Bársony (Tuth)**, who owns the
  hardware, directed the work, tested it by ear against the real unit and supplied the photos, manuals and dumps
  it was measured against.
- The DSP56300 emulation is **dsp56300 by The Usual Suspects (TUS)** from the
  [gearmulator](https://github.com/dsp56300/gearmulator) project - without it there would be no sound. A patched
  copy is in `external/dsp56300`; the changes are in `patches/dsp56300`.
- Thanks to **Unknown Technologies** for the help, the discussions and the bug reports.
- JUCE (via the dsp56300/JUCE fork) for the plugin, Dear ImGui for the standalone GUI.

## License

GPL-3.0 (see `LICENSE.md`), as the dsp56300 library it is built on.

KORG and MS2000 are trademarks of KORG Inc. This project is not affiliated with or endorsed by KORG.

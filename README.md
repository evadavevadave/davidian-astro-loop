# Davidian Astro-Loop

A model of a classic 1970s three-head tape echo with spring reverb, as an LV2, VST3 and Audio Unit plugin. It follows the unit's 1978 service notes: the block diagram, the echo board circuit, the twelve-position mode selector table and the reference data. Figures the service notes leave out were checked against published measurements; anything still unverified is listed under [Estimates](#estimates).

**A close model, not an exact copy.** It is built from the service notes and published figures, not sampled or measured from a unit. It has not been compared side by side with real hardware, so expect it to sound close to the original rather than identical.

![The GUI](docs/gui.png)

Stereo in and out with no latency. The tape and springs are mono, as on the unit: the two inputs are summed for the echo and reverb, the dry signal keeps its stereo image, and the wet signal goes to both outputs. Every format is built from the same code, shows the same GUI and sounds identical. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |
| VST3 | yes | | yes |
| Audio Unit (AUv2) | | yes | |

The Windows builds are tested locally. The macOS and Linux builds are built and tested by GitHub Actions (see [Tests](#tests)).

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-astro-loop/releases):

| Download | Contains | For |
|---|---|---|
| `…-windows-x64.zip` | LV2, VST3 | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | LV2, Audio Unit | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | LV2, VST3 | 64-bit Intel/AMD Linux (LV2: glibc 2.29 or later, e.g. Ubuntu 20.04 or Debian 11; VST3: glibc 2.34 or later, e.g. Ubuntu 22.04 or Debian 12; X11 or XWayland for the GUI) |
| `…-linux-arm64.zip` | LV2, VST3 | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 with a 64-bit OS (same glibc needs as x64) |

Unzip, copy the plugin folder for your host's format into its plugin folder, and restart your host (or rescan plugins):

| Format | Copy | Into |
|---|---|---|
| VST3 (Windows) | `Davidian Astro-Loop.vst3` | `C:\Program Files\Common Files\VST3` |
| VST3 (Linux) | `Davidian Astro-Loop.vst3` | `~/.vst3` |
| Audio Unit (macOS) | `Davidian Astro-Loop.component` | `~/Library/Audio/Plug-Ins/Components` (or `/Library/Audio/Plug-Ins/Components` for all users) |
| LV2 (Windows) | `astro_loop.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist) |
| LV2 (macOS) | `astro_loop.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `astro_loop.lv2` | `~/.lv2` |

In the host it is listed as **Davidian Astro-Loop** (VST3, LV2; under Delay effects in VST3 hosts) or under **Davidian** as **Astro-Loop** (Audio Unit; manufacturer code `Dvdn`, subtype `Astl`). The LV2 URI is `urn:davidian:astro-loop`.

The macOS build isn't notarized, so macOS may block it after download. Run these once in Terminal after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Davidian Astro-Loop.component"
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/astro_loop.lv2
```

Logic Pro and GarageBand only show an Audio Unit once macOS has registered it. If it doesn't appear, log out and back in, or restart the Mac. In Logic, Plug-in Manager can rescan it, and `auval -v aufx Astl Dvdn` in Terminal shows why it fails if it does.

## Controls

| Panel | Control | Unit part / behaviour |
|---|---|---|
| Input | Input | Instrument volume (VR3). Sets the record level and the dry level together, as on the unit. 5 is unity; an input peaking at -12 dBFS then records at 0 VU |
| Input | VU meter | Record level, average-responding with 300 ms ballistics, -20 to +3 VU |
| Mode Selector | 1–12 | The service notes' table (below). Click a number, drag, or use the wheel |
| Tape Echo | Repeat Rate | Capstan motor speed (VR4), 15.6 to 40 cm/s; the readout shows head 1's echo time. The motor takes about a second or two to follow, so turning it gives the pitch-bending glide |
| Tape Echo | Intensity | Regeneration (VR5). With one head, repeats run away from about 7 |
| Tape Echo | Echo | Echo volume (VR6) |
| Echo Tone | Bass, Treble | ±10 dB at 100 Hz and 5 kHz, on the echo only (not the dry or the reverb) |
| Spring | Reverb | Reverb volume (VR9). The ON lamp shows whether the mode selector has the reverb in |
| Transport | Wow | Wow and flutter, 0 to 10 (not on the unit; a worn transport has more) |
| Transport | Tape Age | Not on the unit: darker repeats, more hiss and more flutter as the tape and heads wear |
| (footswitch) | Echo | Echo on/cancel, with a red LED. Cancel stops new sound reaching the record head and lets the repeats already on the tape die away |
| Level | Output | Not on the unit: -24 to +12 dB |
| (host) | Enabled | Plugin bypass with a 20 ms crossfade to the dry input. In VST3 it is the host's bypass switch, and in an Audio Unit host's bypass turns it off. |

The three amber lamps over the playback heads in the tape drawing show which heads the mode selector has switched in. The readout shows the name and value of the last control you touched.

Drag a knob up or down, hold Shift to fine-tune, use the mouse wheel, and double-click to reset. In VST3 and Audio Unit hosts every move on the panel is a proper edit, so automation records it.

### Mode selector (from the service notes)

| Position | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Head 1 | • | | | | • | | | • | | • | • | |
| Head 2 | | • | | • | | • | | • | • | | • | |
| Head 3 | | | • | • | | | • | | • | • | • | |
| Reverb | | | | | • | • | • | • | • | • | • | • |

## How it is modelled

- **Tape transport** (`src/tape.h`): an endless loop with a record head and three playback heads. The model stores the tape position of every recorded sample, and each playback head reads whatever was recorded a fixed distance upstream. Speed changes therefore glide in pitch the way tape does, and wow and flutter come out at the right size: the pitch moves by the difference between record and playback speed, not by the change in delay.
- **Motor** (`src/engine.c`): the speed follows the repeat rate with measured time constants, 1.89 s when slowing and 1.07 s when speeding up. Wow is mostly random drift below 1 Hz, plus once-per-turn components from the pinch roller and capstan.
- **Record and playback chain**: input volume, then the intensity feedback is added at the recording pre-amp input. Pre-emphasis follows, then tape saturation with an error-function curve, so loud treble compresses first. On playback come de-emphasis, a speed-dependent high-frequency loss (about 7 kHz at top speed, 3.5 kHz at the slowest), a small head bump and the coupling high-pass. Intensity taps the playback pre-amp ahead of the echo volume and tone controls, as in the circuit, so those two don't change the regeneration.
- **Spring reverb** (`src/spring.h`): the service notes name the tank as a Z-3F, the same part as in the Davidian Jade Choir (the two-channel chorus combo), and the springs are that model's. The Z-3F's listed replacement is a short three-spring tank with medium decay (1.75–3.0 s). Each spring is a delay line: the pickup coil hears it one transit after the drive coil, and the wave reflects and comes back round through stretched allpass filters (the falling chirps), a loop low-pass and a DC block. Neighbouring springs are read in opposite polarity. The decay measures 2.1 s. The drive IC clips gently. The reverb is fed from the recording pre-amp input, so it reverberates the repeats as well as the input. That follows the block diagram, but no published description confirms it.
- **Tone**: bass and treble shelves set to hit the service notes' ±10 dB at 100 Hz and 5 kHz exactly, either side of the 750 Hz crossover measured for the unit's Baxandall stage.

### Checked against the service notes and published figures

| Item | Model | Source |
|---|---|---|
| Echo time, head 1 | 69–177 ms | Published measurements: 69–177 ms (two commercial emulations' manuals) |
| Heads 2 and 3 | 1.94× and 2.85× head 1 (134–343 ms, 197–504 ms) | Same: 1.94× and 2.85×, "500 ms max at head 3" |
| Tape speed | 15.6–40 cm/s | Service notes: "12cm – 40cm/sec (approx)" |
| Mode selector | 12 positions, all checked by `test_dsp` | Service notes' table |
| Bass / treble | ±10.0 dB at 100 Hz / 5 kHz | Service notes: ±10 dB at 100 Hz / 5 kHz |
| Intensity | Repeats run away from 7.2 (one head) | Service notes' intensity adjustment drawing |
| Motor ballistics | τ 1.89 s slowing, 1.07 s speeding up | Downing and Terjesen, University of Rochester, 2016 (measured on a unit) |
| Wow spectrum, saturation shape | Drift below 1 Hz; error-function curve | Same paper |

The service notes don't give the head spacing, so the speed range was worked out from the published echo times and the notes' 40 cm/s top speed. The slowest speed that gives 177 ms at head 1 is 15.6 cm/s, inside the notes' approximate range.

### Estimates

No published figure was found for these, so they are reasoned estimates:

- **Spring tank:** transit times 29.5, 33.7 and 37.1 ms, a 2.4 s design decay (the middle of the replacement tank's medium range), the allpass dispersion and the band limits, all shared with the Jade Choir's model of the same tank.
- **Echo cancel footswitch:** the service notes show it wired to the power supply board but don't say what it switches. The plugin mutes the record input and lets the repeats decay. If the hardware cuts the repeats instead, this is the part to change.
- **Record level and saturation point; pre-emphasis** (+8 dB above 2.5 kHz); **playback high-frequency loss and head bump; wow and flutter depths** (up to about 0.35% drift at full).
- **Pot tapers:** input, echo and reverb volumes are audio taper, and the repeat rate is linear in speed.
- **Hiss** at -84 dBFS on new tape, rising to -60 dBFS with Tape Age.

### Simplifications

- The unit's two mic inputs, the instrument ECHO/NORMAL switch, the output level selector and the bias oscillator are left out. The input volume stands for the instrument channel.
- Transistor stages are linear apart from the tape, the reverb drive IC and the output buffer's limit at about +12 dBFS.

## Build

The LV2 build is plain C11 (the macOS window layer is compiled as Objective-C) and needs no LV2 SDK or other libraries. The VST3 and the Audio Unit need their SDKs, which CMake downloads while configuring: the Steinberg VST3 SDK 3.8.1 (MIT) and Apple's AudioUnitSDK 1.2.0 (Apache 2.0).

| Source | What it is |
|---|---|
| `src/tape.h`, `src/spring.h`, `src/dsp.h` | The model: tape transport, spring tank, filters |
| `src/engine.c`, `src/engine.h` | The signal path, controls and the per-sample loop, behind a small C API that every format uses |
| `src/params.h`, `src/ports.h` | Parameter names, ranges and text for VST3 and Audio Units, the LV2 port numbers they match, and the mode selector table |
| `src/plugin.c`, `src/ui/ui_lv2.c` | LV2 plugin and GUI |
| `src/vst3/` | VST3 processor, controller and editor, and its CMake build |
| `src/au/` | Audio Unit, its Cocoa view and its Info.plist |
| `src/ui/panel.h`, `src/ui/view.c` | The panel and the window layer under it (Win32, Cocoa, X11), shared by every GUI |
| `src/ui/canvas.h`, `src/ui/font.h`, `src/ui/style.h` | Davidian GUI house style: software renderer, stroke font and palette |
| `src/lv2_abi.h` | The LV2 C interface, so no SDK is needed |
| `bundle/` | LV2 metadata |
| `licenses/` | The VST3 SDK and AudioUnitSDK licences, shipped inside the bundles that use them |

### Windows (Visual Studio)

Needs Visual Studio 2017 or later with the C++ tools. `build.bat` builds the LV2 bundle, `build\astro_loop.lv2`, and the Windows-only LV2 checks, without CMake.

```bash
build.bat
```

### Every platform (CMake)

Needs CMake 3.25 or later and a C/C++ compiler. On macOS, the Xcode command-line tools; add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build. On Linux, gcc or clang plus the X11 headers (`libx11-dev` on Debian/Ubuntu) for the GUI. The VST3 needs them too, and the VST3 SDK's validator build asks for the packages listed under "Install Linux packages" in `.github/workflows/build.yml`. With `-DASTRO_LOOP_VST3=OFF` and no X11 headers, CMake builds the LV2 plugin without its GUI.

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build-cmake --config Release
```

The VST3 lands in `build-cmake/VST3/Release` (Windows and Linux) and the Audio Unit in `build-cmake/AU` (macOS, signed ad hoc). The VST3 is on by default on Windows and Linux, the Audio Unit on macOS; turn them off with `-DASTRO_LOOP_VST3=OFF` or `-DASTRO_LOOP_AU=OFF`, for example to build offline.

To copy the LV2 bundle into `~/.lv2` (Linux):

```bash
cmake --install build-cmake --prefix ~/.lv2 -DLV2_INSTALL_DIR=.
```

Without `-DLV2_INSTALL_DIR=.`, `cmake --install` puts it in `<prefix>/lib/lv2`.

## Tests

Windows, with `build.bat`:

```bash
build-test\test_dsp.exe build\astro_loop.lv2
```

```bash
build-test\lilv_check.exe build
```

```bash
build-test\ui_check.exe build\astro_loop.lv2
```

Every platform, with CMake:

```bash
ctest --test-dir build-cmake -C Release
```

- `test_dsp` checks echo times per head at both rate extremes and two sample rates, every mode selector position, the tone controls, where regeneration runs away, motor ballistics, the spring tank's first return and decay, stability with every control at full (44.1 and 192 kHz), bypass, and the built LV2 library.
- `ui_render` draws the panel to a BMP file without opening a window, on any platform: `build-test\ui_render.exe out.bmp`.
- `ui_check` (Windows) hosts the built GUI and drives it with mouse messages.
- `lilv_check` (Windows) validates the bundle with the lilv LV2 parser, without installing anything.
- `params_check` checks the VST3 and Audio Unit parameters against the LV2 ports and the panel.
- `vst3_check` (Windows) hosts the VST3, clicks and drags on its editor and checks the edits it reports, then compares its sound with the LV2 build. The VST3 build also runs Steinberg's validator.
- `au_check` (macOS) loads the Audio Unit from its bundle, compares its sound with the LV2 build (stereo, and mono input), and opens its view. CI also runs Apple's `auval` on it.

Both sound comparisons cover parameter changes and the host's bypass, and expect the output to match the LV2 build exactly.

CPU: under 1% of one core at 48 kHz, with the defaults or with every control at full (measured on the development machine; twice that at 96 kHz).

GitHub Actions builds and tests all four platforms from the same source on every push (see `.github/workflows/build.yml`), and publishes a release when a version is tagged.

## Use of AI

Made with generative AI (Claude Code), directed and reviewed by Davidian. See [AI_USAGE.md](AI_USAGE.md) for what the author and the AI each did, and what has and hasn't been checked.

## References

- The unit's service notes, second edition (July 1978): block diagram, echo board, miniature board and filter board circuit diagram, mode selector table, electrical adjustment and reference data. A scan is archived at the Internet Archive.
- Downing and Terjesen, tape echo modelling project paper, University of Rochester ECE 472, 2016: motor time constants, wow spectrum, saturation curve, tone stack crossover.
- Universal Audio's and Cherry Audio's manuals for their emulations: echo time ranges and head ratios.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md) and [AI_USAGE.md](AI_USAGE.md). Every source file carries an `SPDX-License-Identifier` line. `src/lv2_abi.h` is under the ISC licence, which is GPL-compatible, because it reproduces the LV2 interface declarations. The VST3 plugin includes code from the Steinberg VST3 SDK (MIT) and the Audio Unit from Apple's AudioUnitSDK (Apache 2.0), both GPL-3.0-compatible. The build copies `LICENSE`, `NOTICE.md` and `AI_USAGE.md` into every bundle. VST is a registered trademark of Steinberg Media Technologies GmbH.

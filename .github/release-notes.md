Ready-made **Davidian Astro-Loop** plugins for Windows, macOS and Linux, built and tested from the same source by GitHub Actions. A three-head tape echo with spring reverb, as an LV2, a **VST3** for Windows and Linux, and an **Audio Unit** for macOS. This release adds the Linux VST3.

## What's new in 1.2

- **VST3 for Linux**, on 64-bit x86 and ARM, with the same panel as the other formats. It needs glibc 2.34 or later (Ubuntu 22.04, Debian 12) and an X11 or XWayland session.
- The sound is the same as 1.1.

## New in 1.1

- **New spring reverb.** It is now the same model of the three-spring tank that the Davidian Jade Choir uses. Each spring is a delay line with a falling chirp on every return. The first return arrives at about 30 ms and the reverb dies away in about 2 seconds. The Reverb knob gives the same level as in 1.0.
- **About 1.7× faster.** The plugin uses under 1% of one CPU core at 48 kHz, even with every control at full. Apart from the new reverb, the sound is unchanged: the largest difference is 132 dB below peak, far below hearing.
- Projects saved with 1.0 open in 1.1 with the same settings.

## Downloads

| File | For | Contains |
|---|---|---|
| `...-windows-x64.zip` | 64-bit Windows 10 and 11 | VST3 and LV2 |
| `...-macos-universal.zip` | macOS 10.13 and later, Apple silicon and Intel | Audio Unit and LV2 |
| `...-linux-x64.zip` | 64-bit Linux on x86 (LV2: glibc 2.29 or later, such as Ubuntu 20.04 or Debian 11; VST3: glibc 2.34 or later, such as Ubuntu 22.04 or Debian 12) | VST3 and LV2 |
| `...-linux-arm64.zip` | 64-bit Linux on ARM, such as a Raspberry Pi 4 or 5 (same glibc needs) | VST3 and LV2 |

## Install

Unzip, close your host, and copy the format it uses:

- **VST3:** copy the `Davidian Astro-Loop.vst3` folder into `C:\Program Files\Common Files\VST3` (Windows) or `~/.vst3` (Linux).
- **Audio Unit (macOS):** copy `Davidian Astro-Loop.component` into `~/Library/Audio/Plug-Ins/Components`. It isn't notarised, so clear the download quarantine once in Terminal: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Davidian Astro-Loop.component"`.
- **LV2:** copy the `astro_loop.lv2` folder into `%APPDATA%\LV2` (Windows), `~/Library/Audio/Plug-Ins/LV2` (macOS, then clear the quarantine the same way) or `~/.lv2` (Linux).

Restart the host (or rescan its plugins) and look for **Astro-Loop** by **Davidian**.

## Features

- **Tape transport:** a record head and three playback heads at the published echo times (69–177 ms at head 1, 1.94× and 2.85× for heads 2 and 3). The motor follows the repeat rate with measured time constants, so turning it gives the pitch-bending glide.
- **Twelve-position mode selector** from the unit's service notes, with lamps over the heads that are switched in.
- **Intensity, echo volume and ±10 dB echo tone**, with tape saturation, pre-emphasis and speed-dependent high-frequency loss.
- **Three-spring reverb** with a dispersive model of each spring, an echo cancel footswitch, and wow, tape age and output trims that the original doesn't have.
- All formats share one engine and give identical output. Settings mean the same thing in every format.

## Testing status

- Every build passes the DSP tests (including a check of the spring reverb's first return and decay time) and the GUI render test.
- **VST3:** passes Steinberg's validator on Windows and on Linux (x86 and ARM). On Windows, a test host drives its editor with simulated mouse input, and its output matches the LV2 build sample for sample. The Linux editor hasn't been opened in a host yet.
- **Audio Unit:** built and checked only by CI on macOS: Apple's `auval` in strict mode for Apple silicon and Intel, and a test host that compares its output with the LV2 build.
- The Linux bundles are also checked with the lilv LV2 parser.
- Not yet tried in a DAW by a person, and not compared with a real unit.

## Licence and AI usage

- **Licence:** GPL-3.0-or-later. The licence, `NOTICE.md` and `AI_USAGE.md` are inside each bundle. The VST3 includes the Steinberg VST3 SDK (MIT) and the Audio Unit includes Apple's AudioUnitSDK (Apache 2.0). Their licences are in the bundles too.
- **AI usage:** this plugin was made with generative AI (Claude Code). See `AI_USAGE.md`.

VST is a registered trademark of Steinberg Media Technologies GmbH.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

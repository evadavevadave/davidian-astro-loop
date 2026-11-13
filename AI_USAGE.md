# AI usage

This plugin was built with generative AI.

- **Tool:** Claude Code, Anthropic's coding assistant, using Anthropic's Claude models (Opus).
- **Human role:** Davidian set the requirements and the design direction, supplied the reference service notes, asked for estimates to be checked against published sources, and decides what is released.
- **What the AI did:**
  - Read the signal flow, component values, mode selector table and reference data off the scanned service notes.
  - Searched online for the figures the service notes leave out (head spacing, motor ballistics, wow and flutter, spring tank) and used published measurements where it found them. Where it found none, the value is marked as an estimate in the code and the README.
  - Wrote the tape, spring and circuit model, the LV2 plugin and GUI code, and the panel artwork (drawn in code, in the Davidian house style shared with the Bi-Lumen).
  - Adapted the Bi-Lumen's VST3 and Audio Unit wrappers, window layer and test harness to this plugin.
  - Wrote the build files, the LV2 metadata, the tests and the documentation.
- **How it was checked:**
  - Automated tests in `test/`: echo times per head and repeat rate against the published figures, every mode selector position against the service notes' table, the tone controls against the notes' ±10 dB figures, where regeneration runs away, motor ballistics, stability at extreme settings and sample rates, plugin and GUI loading, and the lilv LV2 validator.
  - The VST3 build is hosted by a test program that compares its output with the LV2 build sample for sample and drives its editor with simulated mouse input, and it passes Steinberg's VST3 validator.
  - The Audio Unit is built and tested only by GitHub Actions on macOS: it passes Apple's `auval` in strict mode (Apple silicon and Intel), and a test host compares its output with the LV2 build. It has not been run on a Mac by a person.
  - No listening test in a DAW, and no comparison with a real unit, has been done yet.
- **Training data and copyright:** no third-party code was knowingly copied. The only reproduced material is the LV2 interface declarations, credited in [NOTICE.md](NOTICE.md). The VST3 and Audio Unit builds link the Steinberg VST3 SDK and Apple's AudioUnitSDK unmodified, under their own licences (also in NOTICE.md). The GUI artwork and font are generated from code written for this project, not from images.

The AI's output is released under the project licence, GPL-3.0-or-later (see `LICENSE`). Under the GPL, there is no warranty.

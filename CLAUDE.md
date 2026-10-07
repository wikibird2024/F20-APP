# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this folder is

The **F20 automation project**: source code for our software plus the
reference documents for the **Filmetrics F20 thin-film analyzer** (spectral
reflectance: film thickness, n and k). It is a git repo. This is also a
learning project: Hao Tran writes the steps marked 👤 in the plan; explain
patterns, don't silently write their parts.

Layout: docs at the root (table below); code in `common/` (protocol library,
no Qt), `sim/` (f20bridge-sim), `app/` (f20app, Qt 6), `bridge/` (f20bridge,
Windows-only, added in phase 6), `tests/`.

## Building (Linux, development)

```bash
./tool/build.sh            # configure + build + verify (Debug; or: Release, asan)
ctest --preset Debug       # run unit tests (f20tests: doctest, f20apptests: QtTest)
```

`asan` = Debug + AddressSanitizer/UBSan. f20app reads `f20.ini` next to
its executable (`--config` overrides); the build copies it and `recipes/`
into `build/<preset>/bin/`.

Binaries land in `build/<preset>/bin/`. `tool/build.sh` follows the dotfiles
template (`~/dotfiles/project_scripts/build.sh`): only its settings block is
project-specific — keep the body in sync with the template.

The production targets are Windows/MSVC (spec §8); Linux is for development
against the simulator only.

| File | What it is | Pages |
|---|---|---|
| `F20_User_Manual.pdf` | Filmetrics operations manual, rev 6.1.0 (2011), FILMeasure 6 software. **Main reference.** | 101 |
| `filmetrics_f20_manual.pdf` | Older operations manual, rev 2.2.7 (2000), FILMeasure 4 era. Use only for old setups. | 69 |
| `F20-Filmetrics-SOP_REV-D.pdf` | Arizona State University NanoFab SOP, rev D (2019): baseline and measuring steps for lab users. | 8 |
| `F20 1.pdf` | Product datasheet (2025): specs and configurations. | 4 |
| `F20_Qt_Software_Specification.md` | Specification of our own software: Qt 6 operator app + C++/CLI bridge to FIRemote, MQTT to the server. Start here for the automation project. | — |

When the two manuals disagree, trust `F20_User_Manual.pdf` (newer) and say
which one you used.

The formal, shareable edition of the specification is a Claude Doc (exports
to Word/PDF/Google Docs):
https://claude.ai/code/artifact/df45a690-ff30-4e76-95d1-5278810079a4
The `.md` file is the source of truth — when one changes, sync the other.

## Reading the PDFs

`pdftotext` and `pdfinfo` (poppler) are installed. Useful forms:

```bash
pdftotext -f 84 -l 100 F20_User_Manual.pdf -          # page range to stdout
pdftotext -layout F20_User_Manual.pdf - | grep -n Baseline
```

Page numbers in this file are PDF page numbers (as `pdftotext -f/-l` uses),
not the numbers printed in the manual.

## Software control: FIRemote (.NET API)

The only programming interface documented here. Pages 83-100 of
`F20_User_Manual.pdf`.

- `FIRemote` is a public .NET class inside `FILMeasure.exe`; signatures are
  given in VB.NET. A client program references it and FILMeasure does the
  hardware work - there is no documented low-level USB/serial protocol.
- Sample code and a test tool are installed with FILMeasure on the Windows PC:
  `C:\Program Files\Filmetrics\FILMeasure\SourceCode\FIRemoteTest` and
  `FIRemoteTest.exe` (every command can be tried there).
- Constructor `New(showFILMeasureGUI, ByRef ConstructorWarning, ByRef warningMessage)`;
  the one-argument form is deprecated.
- A baseline must exist before measuring. Order (pp. 88-90):
  `BaselineSetRefMat` (before the reference) → `BaselineAcquireSpectrumFromSample`
  (step 1) → `BaselineAcquireReference` (step 2) →
  `BaselineAcquireBackgroundAfterRef` (step 3) → `BaselineCommit`.
  Several of these throw exceptions when their rules are not met; the
  `FIRemoteTest` sample shows the handling.
- Measuring: `SetRecipe`, then `Measure(...)` returns `FIMeasResults`;
  `AcquireSpectrum` / `AnalyzeSpectrum` split acquire and fit.
- Channels/systems are addressed by GUID (`MeasChannelGuids`,
  `MeasSystemGuids`). The manual warns the channel order is not stable - find
  a channel by GUID or serial number, never by list index.
- `SystemMeasurementCompleted` event handler: keep it short; long work
  (like file writes) blocks the monitor loop (p. 100).

# F20 control software

Automation for the Filmetrics F20 thin-film analyzer: a Qt 6 operator app
(f20app), a FIRemote bridge (f20bridge), and a simulator (f20bridge-sim).

## Build (Linux, development)

    ./tool/build.sh              # Debug (default), or: Release, asan
    ctest --preset Debug         # unit tests (doctest + QtTest)

`asan` is Debug with AddressSanitizer and UBSan: run its tests
(`ctest --preset asan`) before trusting a change to memory or lifetimes.

Run against the simulator (two terminals):

    ./build/Debug/bin/F20BRIDGE-SIM
    ./build/Debug/bin/F20APP

f20app reads `f20.ini` next to its executable (or `--config <file>`); the
build copies `f20.ini` and `recipes/` there. Relative paths in the ini -
database, recipes, spectra, logs - are relative to the ini's folder, so in
development the data lands in `build/<preset>/bin/`.

## Documents

Specification: F20_Qt_Software_Specification.md (manuals and reports are in
this folder too; see CLAUDE.md for the file list).

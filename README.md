# F20 control software

Automation for the Filmetrics F20 thin-film analyzer: a Qt 6 operator app
(f20app), a FIRemote bridge (f20bridge), and a simulator (f20bridge-sim).

## Build (Linux, development)

    cmake --preset default
    cmake --build build
    ./build/sim/f20bridge-sim

## Documents

Specification: F20_Qt_Software_Specification.md (manuals and reports are in
this folder too; see CLAUDE.md for the file list).

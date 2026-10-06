# F20 control software

Automation for the Filmetrics F20 thin-film analyzer: a Qt 6 operator app
(f20app), a FIRemote bridge (f20bridge), and a simulator (f20bridge-sim).

## Build (Linux, development)

    ./tool/build.sh              # Debug (default), or: ./tool/build.sh Release
    ctest --preset Debug         # unit tests

Run against the simulator (two terminals):

    ./build/Debug/bin/f20bridge-sim
    ./build/Debug/bin/f20app

## Documents

Specification: F20_Qt_Software_Specification.md (manuals and reports are in
this folder too; see CLAUDE.md for the file list).

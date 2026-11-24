# MyOS

Open-source C++ operating system prototype built and tested in Termux (aarch64).
Licensed under the **MIT License**.

## Features
- **Execution gatekeeper (binos)**: verifies binary permissions (+x), ELF header integrity, and target paths from os.conf ("Not found file on path in os.conf!").
- **Reserve mode (.resrevcommands)**: interactive [y/n] fallback to reserve/built-in commands when binos is damaged or non-executable.
- **Interactive line editing**: arrow-key history navigation and cursor movement in shell.cpp.
- **Sessions & modifiers**: saving/loading session files (.s) and session modification utilities.

## Build (Termux / C++17)
clang++ -std=c++17 ~/myos/src/binos.cpp -o ~/myos/bin/binos && chmod +x ~/myos/bin/binos
clang++ -std=c++17 -I~/myos/src ~/myos/src/shell.cpp -o ~/myos/bin/myos

## MyOS Developer Team
Open for mods, custom builds, and bug reports:
- Core & Boot (binos, os.conf, boot/sessions)
- Shell & CLI UX
- Bug Triage, Testing & Custom Builds

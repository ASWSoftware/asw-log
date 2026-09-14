# ASWLog - https://github.com/ASWSoftware/asw-log

ASWLog is a thread-safe light-weight C++ logging tool for Windows and Linux projects.

# Features

- Log levels
- Log rotation by size and/or date.
- Thread safe
- Various line configuration options, such as UTC date-time, process ID, etc.

# Donations:

If you find this tool helpful, donations are always appreciated:

PayPal:
donate@aswsoftware.com

Bitcoin:
15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

Add the source in `ASWLog` to your C++ project.

## CMake

The `cmake` folder contains a portable CMake project for building the example with CMake, JetBrains CLion,
Visual Studio, Clang, or MinGW. From the repository root, configure and build it with:

```
cmake -S cmake -B build
cmake --build build --config Release
```

The executable is written to `build/bin/Release/ASWLogExample.exe`.
Example logs are written to `build/bin/Release/logs/`

# Coding Standards

To use uncrustify (for coding standards (pretty formatting) for this repo):

1. Get `uncrustify` and ensure that the path to `uncrustify.exe` is added to the Windows `Path` environment variable.
    https://github.com/uncrustify/uncrustify

2. Run the following command locally to set the git hooks directory (uncrustify will run upon commit):
```
git config --local core.hooksPath .githooks/
```

# ASWLog - https://github.com/ASWSoftware/asw-log

ASWLog is a thread-safe light-weight C++ logging tool for Windows and Linux projects.

## Features

- Portable C++20 logger for Windows and Linux, with CMake and RAD Studio examples.
- Thread-safe file logging with singleton or independent logger instances.
- Configurable log levels, metadata, line endings, flushing, file paths, and rotation.
- Formatted, raw, forced, and force-raw logging APIs with source-location support.
- Runtime `Open()`, `Close()`, `Flush()`, reconfiguration, and log rotation controls.
- Optional application and system memory, OS, drive, time, and command-line diagnostics.
- Retry handling for temporary file access conflicts and Windows reader-sharing support.
- Wildcard-based cleanup for logs older than a specified age.

# Donations:

If you find this tool helpful, donations are always appreciated:

PayPal:
donate@aswsoftware.com

Bitcoin:
15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

Add the source in `ASWLog` to your C++ project.

## CMake

The `example/cmake` folder contains a portable CMake project for building the example with CMake, JetBrains CLion,
Visual Studio, Clang, or MinGW. From the repository root, configure and build it with:

```
cmake -S example/cmake -B example/cmake/build
cmake --build example/cmake/build --config Release
```

The executable is written to `example/build/bin/Release/ASWLogExample.exe`, alongside RAD Studio output.

# Coding Standards

To use uncrustify (for coding standards (pretty formatting) for this repo):

1. Get `uncrustify` and ensure that the path to `uncrustify.exe` is added to the Windows `Path` environment variable.
    https://github.com/uncrustify/uncrustify

2. Run the following command locally to set the git hooks directory (uncrustify will run upon commit):
```
git config --local core.hooksPath .githooks/
```

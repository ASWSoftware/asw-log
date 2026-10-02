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
- Extensible: assign your own formatter (`IASWLogFormatter`) to `TASWLogConfig::Formatter` for your own line layout,
  or derive a new text logger from `TASWTextLogBase` and implement only its output.

# Donations:

If you find this tool helpful, donations are always appreciated:

PayPal:
donate@aswsoftware.com

Bitcoin:
15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

Add the source in `ASWLog` to your C++ project.

## CMake

The repository's root `CMakeLists.txt` defines the static library target `ASWLog::ASWLog`. Linking to it adds the
`ASWLog` include folder, C++20, and the system libraries the logger needs (`psapi` on Windows). Add it from a copy of
the repository, such as a git submodule:

```
add_subdirectory(third_party/asw-log)
target_link_libraries(MyApp PRIVATE ASWLog::ASWLog)
```

or let CMake download it with `FetchContent`:

```
include(FetchContent)
FetchContent_Declare(ASWLog
    GIT_REPOSITORY https://github.com/ASWSoftware/asw-log.git
    GIT_TAG v0.45.0     # A release tag, 0.45.0 or later
    GIT_SUBMODULES ""   # Skip the unit-test framework submodule
)
FetchContent_MakeAvailable(ASWLog)
target_link_libraries(MyApp PRIVATE ASWLog::ASWLog)
```

Either way, only the library is built. When ASWLog is the top-level project, `-DASWLOG_BUILD_EXAMPLE=ON` and
`-DASWLOG_BUILD_TESTS=ON` also build the example and the unit tests, and `ASWLOG_WARNINGS` (on by default only there)
compiles the library with extra warnings.

The `example/cmake` folder contains a portable CMake project for building the example with CMake, JetBrains CLion,
Visual Studio, Clang, or MinGW. From the repository root, configure and build it with:

```
cmake -S example/cmake -B example/cmake/build
cmake --build example/cmake/build --config Release
```

The executable is written to `example/build/bin/Release/ASWLogExample.exe`, alongside RAD Studio output.

# Unit Tests

Using the logger only requires the `ASWLog` source. The unit tests in `tests` additionally use the
[ASWUnitTests](https://github.com/ASWSoftware/asw-unit-tests) framework, included as a git submodule in
`third_party/asw-unit-tests`. Clone with submodules:

```
git clone --recurse-submodules https://github.com/ASWSoftware/asw-log.git
```

or, in an existing clone:

```
git submodule update --init
```

Build and run the tests with CMake from the repository root:

```
cmake -S tests/cmake -B tests/cmake/build
cmake --build tests/cmake/build --config Release
```

The executable is written to `tests/cmake/build/bin` (in a `Release` sub folder for multi-config generators such as
Visual Studio). For RAD Studio, use `tests/rad370/ASWLogTests.cbproj` or its `Build_Win64x_*.bat` scripts; output is
written to `tests/build/bin/<Config>`. GitHub Actions builds and runs the tests on Windows and Linux for each push and
pull request to `main` and `develop`.

With GCC or Clang (on Linux), the tests can also be built with a sanitizer, which fails the run with a report when it
finds a data race (`thread`), or a memory error or undefined behavior (`address`, which includes
UndefinedBehaviorSanitizer):

```
cmake -S tests/cmake -B tests/cmake/build-tsan -DCMAKE_BUILD_TYPE=Release -DASWLOG_SANITIZE=thread
cmake --build tests/cmake/build-tsan
```

CI runs both with Clang. `ASWLOG_SANITIZE` applies to the library and everything that links to it.

On Windows with RAD Studio, the tests can also run in the ASWUnitTests VCL GUI runner, a window for choosing tests and
reading their results: build `tests/vcl/gui/rad370/ASWLogTests_VCL_GUI.cbproj` (or its `Build_Win64x_*.bat` scripts),
which writes `ASWLogTests_VCL_GUI.exe` to `tests/build/bin/<Config>`. `ASWLogTests_Group.groupproj` in the same folder
opens it together with the console test project. The GUI takes the console runner's command line options plus a few of
its own, e.g. `--run --exit` to run every test and exit with the console runner's exit code; see the ASWUnitTests
README's "VCL GUI Runner" section.

# Coding Standards

To use uncrustify (for coding standards (pretty formatting) for this repo):

1. Get `uncrustify` and ensure that the path to `uncrustify.exe` is added to the Windows `Path` environment variable.
    https://github.com/uncrustify/uncrustify

2. Run the following command locally to set the git hooks directory (uncrustify will run upon commit):
```
git config --local core.hooksPath .githooks/
```

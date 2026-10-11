# ASWLog - https://github.com/ASWSoftware/asw-log

ASWLog is a thread-safe, dependency-free C++20 logging library for Windows and Linux.

## Design

ASWLog is a folder of standard C++ sources, or one CMake target, with no third-party code. Its
defaults put crash durability first: a logging call is synchronous and every entry is flushed before
the call returns, so the last entries before a crash are in the file. A call below the logger's
level costs a few nanoseconds, so Debug and Trace calls can stay in release builds and be turned on
at run time. The rest (asynchronous writing, backtrace, JSON and pattern formatters, crash handlers,
and the other extras below) is opt-in.

## Features

- Portable C++20 logger for Windows and Linux, with CMake and RAD Studio examples.
- Thread-safe file logging with singleton or independent logger instances.
- Configurable log levels, metadata, line endings, flushing, file paths, and rotation.
- ISO 8601 timestamps in UTC (the default) or local time with its offset, to the millisecond,
  microsecond or nanosecond (`Line.TimestampZone`, `Line.TimestampPrecision`); daily rolling and
  backup names follow the same clock.
- Categories (`ASWLog::TASWCategoryLog`, in `ASWLog_CategoryLog.h`): a named logger for one part of
  an application, e.g. `Net`, that wraps any logger and marks its entries `[Net]`. Give one a level
  of its own to see that part's Debug entries while the rest of the log stays at Info.
- Formatted, raw, forced, and force-raw logging APIs with source-location support. Format strings
  are checked against their arguments at compile time; wrap one built at run time in
  `ASWLog::RuntimeFormat()`.
- Runtime `Open()`, `Close()`, `Flush()` and log rotation controls, and `Reconfigure()` to change
  settings safely while other threads log.
- Optional startup diagnostics: the ASWLog and application versions (on Windows, the application's
  is read from its version resource unless set), application and system memory, OS, drive, time, and
  the command line, written again at the top of each new file after a rotation, saying which file
  the log continues from.
- Retry handling for temporary file access conflicts and Windows reader-sharing support.
- Crash durability: every entry is flushed by default, and with buffered flushing Error and Critical
  entries still are (`File.FlushImmediatelyAtLevel`). Entries at a chosen level can also be synced
  to disk to survive a power loss (`File.SyncToDiskAtLevel`).
- Optional asynchronous writing (`TASWLogConfig::Async`): a logging call only formats and queues its
  entry, and the logger's own thread writes it, while Error entries still wait until they are in the
  file. Off by default, since a synchronous logger has written every entry when the call returns.
- Optional crash handlers (`ASWLog::InstallCrashHandlers()`, in `ASWLog_CrashHandler.h`): on
  `std::terminate()`, `abort()`, or a fatal fault (an access violation on Windows, SIGSEGV and the
  like on Linux), every text logger is flushed, including the entries still queued or buffered, and
  gets a "Crash: <reason>" line. The crash is then passed on to the handler installed before, so
  error reports and core dumps still happen. Best effort, since a crashed process may be in any
  state; the header lists what is and isn't handled. An application with its own crash reporter can
  call `ASWLog::HandleCrash()` from it instead.
- Optional backtrace (`TASWLogConfig::Backtrace`): keep the last N Debug/Trace entries in memory
  while logging at Info, and write them just before the next error (or on `DumpBacktrace()`, or a
  crash), so the log shows what led up to it.
- Internal failures (e.g. a log file that can't be opened, written or rotated) are reported to a
  `TASWLogConfig::OnError` handler, or to stderr, instead of silently dropping entries.
- Backup cleanup after each rotation, by age, count and total size, plus wildcard-based cleanup of
  files older than a specified age (`TASWFileLog::DeleteOldLogs()`). An event for each new backup
  (`File.OnBackupCreated`) lets the application compress, upload or move it.
- Your own line layout as a pattern, e.g. `"{time} {level:5} {[category] }{message}"`
  (`ASWLog::TASWPatternFormatter`, assigned to `TASWLogConfig::Line.Formatter`).
- JSON Lines output for log shippers and tools such as `jq`: one escaped JSON object per entry
  (`ASWLog::TASWJSONFormatter`, assigned to `TASWLogConfig::Line.Formatter`).
- Multi-line messages (e.g. an HTTP body) written as they are, or, with
  `TASWLogConfig::Line.Multiline`, each line after the first marked with `    | `
  (`MultilineMode::Indent`) or the line breaks escaped as `\r` and `\n` (`MultilineMode::Escape`),
  so that every entry is recognizable as one.
- Rate-limited logging for entries that can repeat often: `ASWLOG_ONCE`, `ASWLOG_EVERY_N` and
  `ASWLOG_EVERY_INTERVAL` (`ASWLog_Limiter.h`), e.g.
  `ASWLOG_EVERY_INTERVAL(log, std::chrono::seconds(5), ASWLog::Level::Warn, "Retrying {}", host);`,
  which counts the skipped entries in a `suppressed` field.
- Settings from environment variables (`TASWLogConfig::ApplyEnvironment()`), e.g.
  `ASWLOG_LEVEL=Trace` to see everything a deployed release build logs, without rebuilding it.
- Wide-string messages: every logging method also takes `wchar_t` or `char16_t` text (e.g. `L"..."`,
  a `std::wstring`, or C++Builder's `UnicodeString::c_str()`), converted to UTF-8 only if the entry
  is written.
- A hook to drop or change entries before they are written (`TASWLogConfig::OnBeforeWrite`), e.g. to
  redact secrets or drop noisy entries by content, category or field.
- Structured fields per entry, e.g. `logger.LogInfo("Order placed", {{"orderId", 17}})`, and scoped
  context that every entry on the thread carries, e.g.
  `ASWLog::TASWLogScope scope{{"requestId", id}};` (`ASWLog_Fields.h`).
- A debugger output logger (`ASWLog::TASWDebuggerLog`) for the RAD Studio Event Log, Visual Studio's
  Output window and DebugView (Windows; it writes nothing elsewhere).
- A memory logger (`ASWLog::TASWMemoryLog`) that keeps the newest lines, limited by count and size
  (`TASWLogConfig::Memory`), for an in-app log viewer (`GetLinesSince()` returns only the new ones),
  a crash report or tests.
- A null logger (`ASWLog::TASWNullLog`) that writes nothing, to turn logging off behind an
  `IASWLog&` or to pass to code under test.
- A syslog logger (`ASWLog::TASWSyslogLog`) for the Linux system log and the systemd journal, with
  the level as the priority and the ident and facility in `TASWLogConfig::Syslog` (Linux; it writes
  nothing on Windows).
- A Windows Event Log logger (`ASWLog::TASWWindowsEventLog`) for warnings and errors in Event
  Viewer, with an event ID per level and a helper to register the event source (Windows; it writes
  nothing elsewhere).
- Extensible: assign your own formatter (`IASWLogFormatter`) to `TASWLogConfig::Line.Formatter` for
  your own line layout, or derive a new text logger from `TASWTextLogBase` and implement only its
  output.

# Donations:

If you find this library helpful, donations are always appreciated:

PayPal: donate@aswsoftware.com

Bitcoin: 15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

## Quick start

```cpp
#include "ASWLog_CrashHandler.h"
#include "ASWLog_FileLog.h"

int main()
{
    ASWLog::TASWLogConfig config;
    config.File.FilePath = "myapp.txt"; // In the "logs" folder
    config.ApplyEnvironment(); // Optional: e.g. ASWLOG_LEVEL=Debug overrides the level

    auto& log = ASWLog::TASWFileLog::GetInstance();
    if (!log.Initialize(config))
        return 1;

    ASWLog::InstallCrashHandlers(); // Flushes the log and adds a "Crash:" line if the application crashes

    log.LogInfo("Started");
    log.LogWarnFmt("Disk {} is {}% full", "C:", 93); // Checked against its arguments at compile time
    log.LogInfo("Order placed", { { "orderId", 17 }, { "total", 9.99 } });
    log.LogDebug("Not written: below the minimum level (Info by default)");

    return 0;
}
```

writes `logs/myapp.txt`, after the startup lines (ASWLog version, time, OS, drive, memory,
application; see `TASWLogConfig::Startup`):

```text
[2026-10-11T00:24:25.089Z][INFO][P:6180][T:6648]: Started
[2026-10-11T00:24:25.089Z][WARN][P:6180][T:6648]: Disk C: is 93% full
[2026-10-11T00:24:25.089Z][INFO][P:6180][T:6648][orderId=17 total=9.99]: Order placed
[2026-10-11T00:24:25.089Z][INFO][P:6180][T:6648]: Logger shutdown: 2026-10-11T00:24:25.089Z
```

The `example/src/main.cpp` app shows more: console, multi-log, categories, scopes, rate limiting and
wide text.

## Adding ASWLog to a project

### Copied sources (any build system)

Add every `.cpp` file in `ASWLog/` to the project, and the `ASWLog` folder to its include path, and
compile as C++20. On Windows, link `psapi` and `advapi32` (RAD Studio links them by default); on
Linux, the threads library (`-pthread`). `Deploy.bat <folder>` copies the library sources, the
examples and the docs to a folder, e.g. for a release package.

### RAD Studio

Add the `ASWLog/*.cpp` files to the project (Project > Add to Project) and the `ASWLog` folder to
the include path (Project > Options > Building > C++ Shared Options > Include path). RAD Studio 13.1
or later with the modern Clang toolchain (Win64x). `example/rad370/ASWLogExample.cbproj` is a
working project to copy settings from.

### CMake

The repository's root `CMakeLists.txt` defines the static library target `ASWLog::ASWLog`. Linking
to it adds the `ASWLog` include folder, C++20, and the system libraries the logger needs (the
threads library, and `psapi` and `advapi32` on Windows). Add it from a copy of the repository, such
as a git submodule:

```
add_subdirectory(third_party/asw-log)
target_link_libraries(MyApp PRIVATE ASWLog::ASWLog)
```

or let CMake download it with `FetchContent`:

```
include(FetchContent)
FetchContent_Declare(ASWLog
    GIT_REPOSITORY https://github.com/ASWSoftware/asw-log.git
    GIT_TAG v1.0.0      # A release tag
    GIT_SUBMODULES ""   # Skip the unit-test framework submodule
)
FetchContent_MakeAvailable(ASWLog)
target_link_libraries(MyApp PRIVATE ASWLog::ASWLog)
```

Either way, only the library is built. When ASWLog is the top-level project,
`-DASWLOG_BUILD_EXAMPLE=ON` and `-DASWLOG_BUILD_TESTS=ON` also build the example and the unit tests,
and `ASWLOG_WARNINGS` (on by default only there) compiles the library with extra warnings.

To use a prebuilt copy instead, build and install the library from the repository root (a Debug
build is named `ASWLogd`, so Debug and Release can share one install prefix):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --config Release --prefix C:/Libraries/ASWLog
```

and find it with `find_package` (pass the prefix in `CMAKE_PREFIX_PATH`):

```
find_package(ASWLog 1.0 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE ASWLog::ASWLog)
```

The headers are installed to `<prefix>/include/ASWLog`, the target's include folder, so code
includes them as it does from the sources (`#include "ASWLog_FileLog.h"`). A requested version is
met by any later release with the same major version. The install rules are on by default only when
ASWLog is the top-level project; set `ASWLOG_INSTALL` to change that. `tests/package` is a small
project that builds against an installed copy; CI runs it.

The `example/cmake` folder contains a portable CMake project for building the example with CMake,
JetBrains CLion, Visual Studio, Clang, or MinGW. From the repository root, configure and build it
with:

```
cmake -S example/cmake -B example/cmake/build
cmake --build example/cmake/build --config Release
```

The executable is written to `example/build/bin/Release/ASWLogExample.exe`, alongside RAD Studio
output.

## API overview

**Loggers.** Each implements `ASWLog::IASWLog` (`ASWLog_Interface.h`), so code that logs can take an
`IASWLog&` and not care where the entries go. Most have a `GetInstance()` singleton, and any of them
can also be created as an ordinary object.

| Class | Header | Writes to |
|---|---|---|
| `TASWFileLog` | `ASWLog_FileLog.h` | A file, with rotation, daily rolling and backup cleanup |
| `TASWConsoleLog` | `ASWLog_ConsoleLog.h` | stdout, and stderr for Warn and above, in color on a terminal |
| `TASWDebuggerLog` | `ASWLog_DebuggerLog.h` | The debugger's output (Windows) |
| `TASWMemoryLog` | `ASWLog_MemoryLog.h` | Memory: the newest lines, e.g. for an in-app log viewer |
| `TASWSyslogLog` | `ASWLog_SyslogLog.h` | The system log and the systemd journal (Linux) |
| `TASWWindowsEventLog` | `ASWLog_WindowsEventLog.h` | The Windows Event Log (Windows) |
| `TASWNullLog` | `ASWLog_NullLog.h` | Nothing |
| `TASWMultiLog` | `ASWLog_MultiLog.h` | Several loggers at once, e.g. a file and the console |
| `TASWCategoryLog` | `ASWLog_CategoryLog.h` | Another logger, marking the entries with a category (e.g. `[Net]`) that can have a level of its own |

The loggers that write to only one platform build on both and write nothing on the other, so the
same code can add them everywhere.

**Logging.** On any logger:

- `LogTrace()`, `LogDebug()`, `LogInfo()`, `LogWarn()`, `LogError()`, `LogCritical()`, and
  `Log(level, ...)`.
- `LogInfoFmt("Opened {} in {} ms", name, ms)` and the other `*Fmt` methods: `std::format`, with the
  format string checked against its arguments at compile time (wrap a format string built at run
  time in `ASWLog::RuntimeFormat()`). An entry below the minimum level isn't formatted.
- `LogRaw()` writes the message as it is, without the line layout; `LogForce()` and `LogForceRaw()`
  write it whatever the minimum level.
- Fields: `LogInfo("Order placed", {{"orderId", 17}})`,
  `LogInfoFmt({{"orderId", id}}, "Order {} placed", id)`, and
  `ASWLog::TASWLogScope scope{{"requestId", id}};` for fields that every entry on the thread carries
  while it exists (`ASWLog_Fields.h`).
- Wide text: `LogInfo(L"...")`, `LogInfoFmt(L"... {}", ...)`, and `ASWLog::Wide(text)` /
  `ASWLog::UTF8(text)` for arguments (`ASWLog_Unicode.h`).
- The caller's function and source line are captured automatically (shown with
  `Line.ShowFunctionName` and `Line.ShowSourceLine`).

**Control.** `Initialize(config)`, `Reconfigure(config)`, `GetConfig()`, `SetMinimumLevel()`,
`ShouldLog(level)` (to skip building an expensive message), `SetEnabled()`, `Flush()`, `Open()`,
`Close()` and `DumpBacktrace()`; a file logger also has `RotateLogFiles()` and `DeleteOldLogs()`.

**More.** `ASWLog::InstallCrashHandlers()` (`ASWLog_CrashHandler.h`), the `ASWLOG_ONCE`,
`ASWLOG_EVERY_N` and `ASWLOG_EVERY_INTERVAL` macros (`ASWLog_Limiter.h`),
`TASWLogConfig::ApplyEnvironment()`, the formatters (`ASWLog_Formatter.h`), and the version
(`ASWLog_Version.h`). The header comments document every class and method.

## Documentation

- [docs/Configuration.md](docs/Configuration.md): every setting, its default, and which loggers use
  it, plus the environment variables.
- [docs/Guarantees.md](docs/Guarantees.md): thread safety, the order of entries, which thread runs
  each callback, object lifetimes, and choosing settings for crash durability.
- [docs/CustomLoggers.md](docs/CustomLoggers.md): writing a formatter or a logger of your own.
- [CHANGELOG.md](CHANGELOG.md): the changes in each release.

## Versions

ASWLog follows [Semantic Versioning](https://semver.org). Releases are tagged on `main` (e.g.
`v1.1.0`). Between releases, the `develop` branch carries the next planned version with a
pre-release, e.g. `1.1.0-dev.1`, which comes before `1.1.0`. `ASWLog/ASWLog_Version.h` has the
version as macros, for code that supports several ASWLog versions, and as constants in the `ASWLog`
namespace:

```
#include "ASWLog_Version.h"

#if ASWLOG_VERSION_MAJOR > 1 || (ASWLOG_VERSION_MAJOR == 1 && ASWLOG_VERSION_MINOR >= 1)
    // Uses something added in 1.1 (also present in 1.1.0-dev.N builds)
#endif
```

### Upgrading from 0.x

1.0.0 is the first release with a stable interface; the 0.x releases changed it as the design
settled. The releases that can need changes in code that uses ASWLog are 0.43.0, 0.45.0, 0.65.0,
0.75.0 (only for a class that implements `IASWLog` directly) and 1.0.0. In
[CHANGELOG.md](CHANGELOG.md), read the **Changed** and **Removed** sections, and the entries marked
"Breaking", of every release after the one you use: each says what existing code must change.
Projects that list the ASWLog sources must also add the files that new releases added (the CHANGELOG
names them, e.g. `ASWLog_Unicode.cpp`).

# Unit Tests

Using the logger only requires the `ASWLog` source. The unit tests in `tests` additionally use the
[ASWUnitTests](https://github.com/ASWSoftware/asw-unit-tests) framework, included as a git submodule
in `third_party/asw-unit-tests`. Clone with submodules:

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

The executable is written to `tests/cmake/build/bin` (in a `Release` sub folder for multi-config
generators such as Visual Studio). For RAD Studio, use `tests/rad370/ASWLogTests.cbproj` or its
`Build_Win64x_*.bat` scripts; output is written to `tests/build/bin/<Config>`. GitHub Actions builds
and runs the tests on Windows and Linux for each push and pull request to `main` and `develop`. The
crash handler tests start the test executable again with `--aswlog-crash-helper=<scenario>`, which
makes it crash on purpose before its `main()` runs; a debugger attached to the test run doesn't see
those processes.

With GCC or Clang (on Linux), the tests can also be built with a sanitizer, which fails the run with
a report when it finds a data race (`thread`), or a memory error or undefined behavior (`address`,
which includes UndefinedBehaviorSanitizer):

```
cmake -S tests/cmake -B tests/cmake/build-tsan -DCMAKE_BUILD_TYPE=Release -DASWLOG_SANITIZE=thread
cmake --build tests/cmake/build-tsan
```

CI runs both with Clang. `ASWLOG_SANITIZE` applies to the library and everything that links to it.

CI also measures which lines of the library the tests run (a Linux GCC build with `--coverage`, read
by gcovr): the "Linux (GCC, coverage)" job shows a table on its summary page and keeps an HTML
report as the `coverage-report` artifact. It is informational and never fails the run.

On Windows with RAD Studio, the tests can also run in the ASWUnitTests VCL GUI runner, a window for
choosing tests and reading their results: build `tests/vcl/gui/rad370/ASWLogTests_VCL_GUI.cbproj`
(or its `Build_Win64x_*.bat` scripts), which writes `ASWLogTests_VCL_GUI.exe` to
`tests/build/bin/<Config>`. `ASWLogTests_Group.groupproj` in the same folder opens it together with
the console test project. The GUI takes the console runner's command line options plus a few of its
own, e.g. `--run --exit` to run every test and exit with the console runner's exit code; see the
ASWUnitTests README's "VCL GUI Runner" section.

# Coding Standards

To use uncrustify (for coding standards (pretty formatting) for this repo):

1. Get `uncrustify` and ensure that the path to `uncrustify.exe` is added to the Windows `Path`
   environment variable. https://github.com/uncrustify/uncrustify

2. Run the following command locally to set the git hooks directory (uncrustify will run upon
   commit):
```
git config --local core.hooksPath .githooks/
```

# Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to propose a change, build
and test it, and report a bug.

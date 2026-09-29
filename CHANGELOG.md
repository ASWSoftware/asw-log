# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Versions before 0.26.1 are not itemized here;
see [0.26.1](#0261---2026-09-17) for the initial versioned baseline.

## [Unreleased]

### Added

- CI workflow (`.github/workflows/ci.yml`), building and running the unit
  tests on Windows (MSVC, MinGW) and Linux (GCC, Clang), plus a
  `test-report.yml` workflow publishing the JUnit results as check runs,
  including for pull requests from forks.
- VCL GUI test runner (`tests/vcl/gui/rad370/ASWLogTests_VCL_GUI.cbproj`),
  running the unit tests in the ASWUnitTests GUI (a window with a test tree,
  per-test results and log, and Run Failed), built from the submodule's
  `vcl/gui/src` sources. Its project group also opens the console test
  project.
- This CHANGELOG.md file.
- `.gitattributes`, normalizing line endings.
- `GetWindowsEditionName()` (Windows only), naming the Windows edition for a
  `GetProductInfo()` product type, as used by `GetOSInfoString()`.

### Changed

- Unit tests now use [ASWUnitTests](https://github.com/ASWSoftware/asw-unit-tests)
  1.0.0 as a git submodule in `third_party/asw-unit-tests`, replacing the
  ASWUnitTests 0.26.3 copy in `unit-tests/`. Test modules moved to
  `tests/tests/` and self-register with `ASW_REGISTER_TEST_GROUP`; their
  CMake project is `tests/cmake/` and their RAD Studio project is
  `tests/rad370/`. Building the tests requires the submodule
  (`git clone --recurse-submodules`, or `git submodule update --init`); using
  the logger itself does not.

### Fixed

- MinGW `-Wcast-function-type` warning in `GetOSInfoString()`, when casting
  `GetProcAddress()`'s result to the `RtlGetVersion` signature.
- `GetOSInfoString()` reporting the edition as "Unknown" on Windows 10/11 Home
  and on every Windows Server edition. The Server checks used product type
  constants that no Windows SDK defines, so they never compiled in. It now
  names the Home, Pro for Workstations, Enterprise LTSC, and Server
  Standard/Datacenter editions, and reports any other product type as
  "Server" or "Unknown" plus its product type number.
- `ResolveLogFileDir_CustomFolder` unit test failing on Linux: it used a
  backslash-separated path, which is only a separator on Windows. The
  backslash case is now checked on Windows only.

## [0.26.4] - 2026-09-21

### Fixed

- `Deploy.bat` not copying the `TASWConsoleLog` and `TASWMultiLog` source
  files added in 0.26.3.
- Missing closing quote in the unrecognized-platform `#error` message added
  in 0.26.3.

## [0.26.3] - 2026-09-21

### Added

- `TASWConsoleLog`, logging to the console: `Warn` and above to `stderr`,
  everything else to `stdout`, with optional per-level ANSI colors.
- `TASWMultiLog`, fanning each log call out to several `IASWLog` sinks (e.g.
  a `TASWFileLog` and a `TASWConsoleLog`). It doesn't own the sinks; the
  caller manages their lifetime.
- `TASWLogConfig::OnLogEntry`, an optional callback invoked with each
  successfully written line at or above `CallbackMinimumLevel` (default
  `Error`), e.g. for alerting or crash reporting. It runs outside the
  logger's lock, so it may log again, and exceptions it throws are
  swallowed. Supported by `TASWFileLog` and `TASWConsoleLog`.
- `TASWLogConfig::RetentionMaxAge`, making `TASWFileLog` delete this log's
  backups older than the given age after each rotation (`0`, the default,
  disables it).
- `GetMinimumLevel()` and `SetMinimumLevel()` on loggers, reading or changing
  the minimum level at runtime without locking.
- `LevelCount`, the number of `Level` values.
- Example usage of `TASWConsoleLog` and `TASWMultiLog`, and unit tests for
  them, plus a multi-threaded `TASWFileLog` test.

### Changed

- `TASWLogConfig::MinimumLevel` renamed to `InitialMinimumLevel`, since it
  now only seeds the level at `Initialize()`; use `SetMinimumLevel()` to
  change it afterward. Code setting `MinimumLevel` must be updated. Entries
  below the minimum level are now filtered before taking the logger's lock.
- `GenerateLogFileName()` takes a new leading `prefix` parameter, producing
  `[prefix_]YYYYMMDD_HHMMSS_mmm_PID_TID[_customPostfix]`; pass an empty
  string to omit either part. Existing calls must add the argument.
- An unrecognized target platform is now a compile error in
  `WriteApplicationInfo()`, instead of logging a placeholder platform name.

## [0.26.1] - 2026-09-17

Initial versioned release, switching the project to semantic versioning.
Everything already present in the logger at this point (`TASWFileLog`,
`TASWLogConfig`, log rotation and cleanup, memory/OS diagnostics, RAD Studio
and CMake example projects, unit tests, `Deploy.bat`, etc.) is treated as the
baseline and is not itemized commit-by-commit.

[Unreleased]: https://github.com/ASWSoftware/asw-log/compare/v0.26.4...HEAD
[0.26.4]: https://github.com/ASWSoftware/asw-log/compare/v0.26.3...v0.26.4
[0.26.3]: https://github.com/ASWSoftware/asw-log/compare/v0.26.1...v0.26.3
[0.26.1]: https://github.com/ASWSoftware/asw-log/releases/tag/v0.26.1

# AGENTS.md

## Scope

These instructions apply to the entire repository unless a more specific `AGENTS.md` is added in a subdirectory. Keep changes focused on the requested behavior and preserve existing public APIs unless a change is necessary.

## Repository Overview

ASWLog is a thread-safe light-weight C++ logging tool for Windows and Linux projects.

- `ASWLog/` contains the framework implementation.
- `tests/` contains the unit tests: test modules in `tests/tests/`, their CMake project in `tests/cmake/`, their RAD Studio console project in `tests/rad370/`, and their RAD Studio VCL GUI runner project in `tests/vcl/gui/rad370/` (built from the submodule's `vcl/gui/src`). `tests/compile_checks/` holds "must not compile" checks, which `tests/cmake/CMakeLists.txt` runs with `try_compile` when configuring (a failed check stops the configure).
- `third_party/asw-unit-tests/` is the ASWUnitTests framework, a git submodule pinned to a release tag. Never edit files in it; test modules self-register with `ASW_REGISTER_TEST_GROUP`, so adding a test only touches `tests/` and its build files.
- `.github/workflows/ci.yml` builds and runs the unit tests on Windows (MSVC, MinGW) and Linux (GCC, Clang), plus Linux Clang builds with ThreadSanitizer and with AddressSanitizer + UBSan (`ASWLOG_SANITIZE`).
- `example/` contains example app that uses the logger.
- `example/rad370/` contains the RAD Studio 13.1 project and Windows build scripts.
- `README.md` contains repo details.
- `CHANGELOG.md` records notable changes per release (see Changelog below).
- `CONTRIBUTING.md` contains the guidelines for contributors (issues, branches, commit history, pull requests, and bug reports); it defers to this file for coding rules, so keep the two consistent.
- `.uncrustify.cfg` and `.githooks/` define the repository formatting workflow.

## Accuracy-First Engineering

Contributors and AI coding agents are expected to be expert, skeptical, and careful. Verify assumptions by inspecting nearby code, call sites, tests, project files, and relevant history or documentation when needed. Neither the human contributor nor the AI is infallible; treat requirements and proposed fixes as hypotheses to check, and prefer evidence from a focused build or test over intuition.

Before changing behavior:

1. Identify the code path that owns the behavior.
2. State or test a concrete hypothesis about the cause or intended result.
3. Make the smallest change that addresses the cause.
4. Run the narrowest relevant validation, then broader checks when practical.

Do not hide unrelated failures by changing tests or weakening diagnostics. Do not revert changes that were already present unless explicitly asked.

## Language and Toolchain Compatibility

New and modified code must be compatible with C++ projects using C++20 and with:

- RAD Studio 13.1 and later, using the modern Clang tool-set.
- CMake
- JetBrains CLion
- Visual Studio
- Clang
- MinGW
- Windows and Linux

Prefer standard C++ and portable library facilities. Avoid compiler-, IDE-, or operating-system-specific extensions unless they are isolated behind a clear portability boundary. For OS-specific code, use valid preprocessor checks such as `#if defined(_WIN32)` and provide the corresponding Linux or portable path where appropriate. Keep headers self-contained and avoid relying on transitive includes.

## Code Organization

- Insert new class methods in alphabetical order within their existing section. If the class has named or visibly separated sections, preserve those sections and alphabetize only within the relevant section.
- Insert new free functions in alphabetical order within their existing section. Apply the same section rule when functions are grouped.
- Follow the surrounding naming, indentation, brace, include, and comment style. Avoid unrelated formatting changes.
- Keep declarations and definitions consistent, and update all relevant project or build files when adding source files.
- Add or update focused tests for behavior changes, including error and platform-specific cases where applicable.

## Building and Testing

The RAD Studio debug build can be run from `example/rad370/Build_Win64x_Debug.bat` when the `Rad370` environment variable points to the RAD Studio installation. The debug console prompts for "press enter to continue" after the run. The release script is `example/rad370/Build_Win64x_Release.bat`; it runs the tests and exits without that pause.

The unit tests need the `third_party/asw-unit-tests` submodule; run `git submodule update --init` if it is empty. Build them with `tests/rad370/Build_Win64x_Debug.bat` or `Build_Win64x_Release.bat` (output in `tests/build/bin/<Config>/`), or with CMake:

```text
cmake -S tests/cmake -B tests/cmake/build
cmake --build tests/cmake/build --config Release
```

The VCL GUI runner builds with `tests/vcl/gui/rad370/Build_Win64x_Debug.bat` or `Build_Win64x_Release.bat` into the same output folder. To run every test in it without interaction, use `start /wait ASWLogTests_VCL_GUI.exe --run --exit --layout-ignore --report-junit results.xml`; its exit code matches the console runner's.

When adding a test module, add its `.cpp`/`.h` to `tests/cmake/CMakeLists.txt`, `tests/rad370/ASWLogTests.cbproj`, and `tests/vcl/gui/rad370/ASWLogTests_VCL_GUI.cbproj`, and keep their include paths in sync. Never open the GUI's main form (`ASWUnitTests_GUI_MainForm`) in the RAD Studio designer and save it, since that writes into the submodule.

For other environments, use the repository's CMake configuration when present or the IDE's native project configuration. Tests should be run outside the debugger unless debugging an expected exception is intentional. Before considering a change complete:

- Build the affected configuration.
- Run the unit-test executable.
- Check compiler warnings and errors.
- Validate both Windows-specific and portable paths when the change touches platform code.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and Semantic Versioning.

- Record each notable change under `## [Unreleased]`, in the matching `### Added`, `### Changed`, `### Deprecated`, `### Removed`, `### Fixed`, or `### Security` section.
- Describe the effect on someone using the logger, not the commit. For a breaking change, say what existing code must change.
- The version lives only in `ASWLog/ASWLog_Version.h` (the `ASWLOG_VERSION_*` macros; the constants and `GetVersionStr()` come from them). `main` carries only release versions, with an empty `ASWLOG_VERSION_PRERELEASE`. Between releases, `develop` carries the next planned version with a pre-release such as `dev.1` (e.g. `1.1.0-dev.1` after `1.0.0`), which sorts before that release; bump its number only to tell dev builds apart, and never bump the patch number on `develop`. A unit test checks that `ASWLOG_VERSION_STRING` matches the parts.
- When preparing a release, set the release version in `ASWLog/ASWLog_Version.h` (empty pre-release; the number develop forecast may change, e.g. to a patch release or the next major), rename `[Unreleased]` to `[x.y.z] - YYYY-MM-DD`, start a new empty `[Unreleased]`, and update the compare links at the bottom of the file. After merging the release into `develop`, move `develop` to the next minor version with pre-release `dev.1`.

## Formatting and Review

Use the repository's `.uncrustify.cfg` configuration. The README documents how to enable the local Git hook with:

```text
git config --local core.hooksPath .githooks/
```

Review the final diff for accidental file changes, ordering regressions, missing includes, platform assumptions, and tests that do not actually exercise the changed behavior. Do not commit changes unless the user explicitly requests a commit.

# AGENTS.md

## Scope

These instructions apply to the entire repository unless a more specific `AGENTS.md` is added in a subdirectory. Keep changes focused on the requested behavior and preserve existing public APIs unless a change is necessary.

## Repository Overview

ASWLog is a thread-safe light-weight C++ logging tool for Windows and Linux projects.

- `ASWLog/` contains the framework implementation.
- `unit-tests/` contains unit tests.
- `example/` contains example app that uses the logger.
- `example/rad370/` contains the RAD Studio 13.1 project and Windows build scripts.
- `README.md` contains repo details.
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

For other environments, use the repository's CMake configuration when present or the IDE's native project configuration. Tests should be run outside the debugger unless debugging an expected exception is intentional. Before considering a change complete:

- Build the affected configuration.
- Run the unit-test executable.
- Check compiler warnings and errors.
- Validate both Windows-specific and portable paths when the change touches platform code.

## Formatting and Review

Use the repository's `.uncrustify.cfg` configuration. The README documents how to enable the local Git hook with:

```text
git config --local core.hooksPath .githooks/
```

Review the final diff for accidental file changes, ordering regressions, missing includes, platform assumptions, and tests that do not actually exercise the changed behavior. Do not commit changes unless the user explicitly requests a commit.

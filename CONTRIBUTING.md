# Contributing to ASWLog

Thanks for considering a contribution. This project is a small, thread-safe C++20
logging library for Windows and Linux, and contributions are expected to keep it
that way. The library in `ASWLog/` depends only on the C++ standard library and
the operating system's APIs; third-party code (`third_party/`) is only used by the
unit tests.

## Before you start

- For anything beyond a small fix, open an issue first to discuss the change. This
  avoids wasted work on something that doesn't fit the project's scope (see
  `AGENTS.md`).
- Coding standards, repository layout, and language/toolchain compatibility rules
  live in [AGENTS.md](AGENTS.md). Read it before making changes; it is the single
  source of truth for how code in this repository should look, and these
  guidelines don't repeat it.
- Base your branch on `develop`, not `main`. `main` tracks released versions;
  `develop` is where in-progress work is integrated before a release.
- The unit tests use the [ASWUnitTests](https://github.com/ASWSoftware/asw-unit-tests)
  framework, a git submodule in `third_party/asw-unit-tests`. Run
  `git submodule update --init` if that folder is empty. Never edit files in it.

## Making a change

1. Identify the code path that owns the behavior you're changing.
2. Make the smallest change that addresses the issue. Don't bundle unrelated
   refactoring, formatting, or reordering into the same change.
3. Add or update focused tests in `tests/tests/` for the behavior change,
   including error and platform-specific cases where applicable. A new test file
   goes into `tests/cmake/CMakeLists.txt`, `tests/rad370/ASWLogTests.cbproj`, and
   `tests/vcl/gui/rad370/ASWLogTests_VCL_GUI.cbproj`. A new library source file
   goes into the root `CMakeLists.txt`, the RAD Studio test and example projects,
   and `Deploy.bat`.
4. Build the affected configuration and run the unit-test executable. See
   [README.md](README.md#unit-tests) for the CMake build commands, and
   `tests/rad370/Build_Win64x_Debug.bat` / `Build_Win64x_Release.bat` for the RAD
   Studio build. Also build the example (`example/cmake`, or
   `example/rad370/Build_Win64x_Release.bat`).
5. Check compiler warnings and errors; a change should not introduce new ones.
6. If you touched platform-specific code (`#if defined(_WIN32)` or similar),
   validate both the Windows and the portable path if you can. CI builds and runs
   the tests on Windows (MSVC, MinGW) and Linux (GCC, Clang), plus Linux builds
   with ThreadSanitizer and with AddressSanitizer + UBSan; a pull request should
   pass all of them. [README.md](README.md#unit-tests) shows how to run a
   sanitizer build yourself.
7. Keep logging fast for the caller. A change to the path every log call takes
   (the level check, formatting, writing) must not add a measurable cost per
   entry; the file or console output itself is accepted as the slow part. If your
   change touches that path, say in the pull request how you measured it.
8. Keep `.cpp` and `.h` files ASCII-only. MSVC reads a source file without a BOM
   in the system's code page, so other characters can change meaning. In tests,
   write non-ASCII text with escapes (e.g. `"\xCE\xBB"` for UTF-8 bytes, or
   `L"\x03BB"` for a Windows path).
9. Describe a notable change in `CHANGELOG.md` under `## [Unreleased]`, in the
   matching section (see the Changelog rules in `AGENTS.md`). Describe the effect
   on someone using the logger; for a breaking change, say what callers must
   change. Test-only changes need no entry. Don't change the version in
   `ASWLog/ASWLog_Version.h`; that happens when a release is prepared.

## Formatting

This repository uses `uncrustify` for consistent formatting, enforced via a Git
hook. Enable it locally with:

```
git config --local core.hooksPath .githooks/
```

See [README.md](README.md#coding-standards) for how to install `uncrustify`
itself.

## Commit history

Each commit in a branch should focus on one specific task or fix, and the commits
should be in a logical order that tells the story of how the change was built.
Before opening a PR, review your own branch's commit history and confirm each
commit actually does what its message says.

[TortoiseGit](https://tortoisegit.org/) (free) is a good tool for this: it makes
editing a commit message, reordering commits, and squashing an "oops, fix typo"
commit into the commit it belongs with straightforward. Reviewing Anthony West's
own commit history in this repository is a good reference for how commits are
structured here.

Pull requests in this repository are merged with their commit history intact, not
squashed, so a clean history matters: it's what future readers use to see where a
bug was introduced and the reasoning behind a change.

Once a PR is open, one or two additional fixup commits are fine without revisiting
earlier history. Beyond that, edit or squash the fixups into the commits they
belong with and push with `git push --force-with-lease`.

## Submitting a pull request

- Target the `develop` branch.
- Keep the diff focused on the change described in your issue or PR description.
  Review your own diff for accidental file changes, ordering regressions, missing
  includes, and platform assumptions before requesting review.
- Describe what changed and why, not just what the code now does.
- Don't include generated build output or IDE files. The `build/` folders (e.g.
  `tests/build/`, `tests/cmake/build/`, `example/build/`), RAD Studio's
  `Win64x/` and `__history/` folders, and the logs the example writes are already
  git-ignored.
- Don't include changes to `third_party/asw-unit-tests`; changes to the framework
  belong in [its own repository](https://github.com/ASWSoftware/asw-unit-tests).

## Reporting bugs

Open a GitHub issue with:

- The platform, compiler, and toolchain (e.g. Windows 11 with RAD Studio 13.1 or
  MSVC 19.40, or Ubuntu 24.04 with GCC 13).
- The ASWLog version (`ASWLOG_VERSION_STRING` in `ASWLog/ASWLog_Version.h`, or the
  commit you built from).
- Which logger you use (`TASWFileLog`, `TASWConsoleLog`, `TASWMultiLog`, or your
  own), and the `TASWLogConfig` settings involved (e.g. `File.Flush`, rotation,
  `File.AutoOpenClosePerWrite`).
- Any errors the logger reported (to your `OnError` handler, or on stderr), and
  the relevant part of the log output.
- Whether several threads or processes write to the same log.
- The expected behavior versus the actual behavior.
- A minimal reproduction if possible.

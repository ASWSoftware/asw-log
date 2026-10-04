/* **************************************************************************
ASWLog_ConsoleLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

#pragma once

#ifndef ASWLog_ConsoleLogH
#define ASWLog_ConsoleLogH
//---------------------------------------------------------------------------
#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWConsoleLog
//
// Used for logging to the console (stdout/stderr).
//
// Takes the same TASWLogConfig as TASWFileLog, but ignores its File settings.
// Colors are set on the logger itself (see SetColorMode()).
/////////////////////////////////////////////////////////////////////////////
class TASWConsoleLog : public TASWTextLogBase
{
private:
    typedef TASWTextLogBase inherited;

private:
    // m_Mutex (see TASWTextLogBase) also protects m_LevelColors
    std::atomic<ColorMode> m_ColorMode{ ColorMode::Auto }; // Thread-safe via GetColorMode()/SetColorMode()
    std::atomic<bool> m_NoColorRequested{ false }; // The NO_COLOR environment variable was set when Initialize() ran
    std::atomic<bool> m_StdErrColorSupported{ false }; // Set by Initialize(), see DetectStreamColorSupport()
    std::atomic<bool> m_StdOutColorSupported{ false }; // Set by Initialize(), see DetectStreamColorSupport()

    // Per-level ANSI color escape sequences used when a line is colored (see SetColorMode()). Defaults suit a
    // typical dark-background terminal (see DefaultLevelColors() for the exact codes).
    // Overridable via SetLevelColor(), restorable via ResetLevelColor()/ResetLevelColors().
    // Indexed by each Level's underlying integer value (Trace=0 .. Critical=5); read/written
    // only via LevelColorUnlocked()/GetLevelColor()/SetLevelColor()/Reset*, never indexed directly.
    std::array<std::string, LevelCount> m_LevelColors{ DefaultLevelColors() };

private:
    [[nodiscard]] static std::array<std::string, LevelCount> DefaultLevelColors();

private:
    [[nodiscard]] std::string_view LevelColorUnlocked(Level level) const noexcept; // Caller must hold m_Mutex.
    [[nodiscard]] bool ShouldColorStream(bool isStdErr) const noexcept; // Applies GetColorMode() to stdout or stderr

protected: // TASWTextLogBase hooks
    bool CloseUnlocked() override;
    bool FlushUnlocked() override; // Flushes both stdout and stderr
    bool InitializeUnlocked() override;
    bool OpenUnlocked() override;
    void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) override;

protected:
    // Whether stdout (or stderr, if 'isStdErr') shows ANSI colors, checked by Initialize(). Windows: the stream is a
    // console that accepts virtual terminal sequences (turned on here if needed). POSIX: the stream is a terminal and
    // TERM isn't "dumb". False for a file or a pipe. Overridable, e.g. by tests, where the streams aren't terminals.
    virtual bool DetectStreamColorSupport(bool isStdErr);

    std::string_view GetLoggerClassName() const noexcept final
    {
        return "TASWConsoleLog";
    }

public: // Static methods
    // Singleton support for the common static instance. Never destroyed; finalized at exit in static destruction
    // order, like TASWFileLog::GetInstance() (see there).
    static TASWConsoleLog& GetInstance();

public:
    TASWConsoleLog() = default;
    ~TASWConsoleLog();

    // True if stdout or stderr shows ANSI colors, as detected by Initialize() (see DetectStreamColorSupport()). False
    // before Initialize(). Doesn't consider GetColorMode() or NO_COLOR.
    [[nodiscard]] bool IsColorSupported() const noexcept;

    [[nodiscard]] std::string GetLevelColor(Level level) const; // Current ANSI color escape sequence used for `level`.
    void SetLevelColor(Level level, std::string colorCode); // Overrides the color for `level`; pass an empty string to disable color.

    void ResetLevelColor(Level level) noexcept; // Restores `level`'s color to its built-in default.
    void ResetLevelColors() noexcept; // Restores every level's color to its built-in default.

    // Whether log lines are wrapped in the per-level ANSI color codes (see SetLevelColor()/GetLevelColor()):
    // ColorMode::Auto (the default) colors only a stream that shows colors, unless NO_COLOR is set; Always and Never
    // override that. Thread-safe to read/write from any thread.
    [[nodiscard]] ColorMode GetColorMode() const noexcept;
    void SetColorMode(ColorMode colorMode) noexcept;
};

} // namespace ASWLog

#endif // ASWLog_ConsoleLogH

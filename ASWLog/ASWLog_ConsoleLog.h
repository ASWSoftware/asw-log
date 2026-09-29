/* **************************************************************************
ASWLog_ConsoleLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

Copyright 2026 Anthony S. West

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
#include <mutex>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWConsoleLog
//
// Used for logging to the console (stdout/stderr).
//
// Reuses TASWLogConfig for consistency with TASWFileLog, but only honors the
// subset of fields that make sense for a console destination.
/////////////////////////////////////////////////////////////////////////////
class TASWConsoleLog : public TASWLogBase
{
private:
    typedef TASWLogBase inherited;

private:
    mutable std::mutex m_ConsoleMutex; // Protects console write bounds, and m_LevelColors, across multiple threads
    std::atomic<bool> m_IsOpen{ false };
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
    void AppendLineEnding(std::string& line);
    bool CloseUnlocked();
    void Finalize() noexcept;
    [[nodiscard]] std::string_view LevelColorUnlocked(Level level) const noexcept; // Caller must hold m_ConsoleMutex.
    void LogEntry(Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc) noexcept;
    bool OpenUnlocked();
    [[nodiscard]] bool ShouldColorStream(bool isStdErr) const noexcept; // Applies GetColorMode() to stdout or stderr
    void WriteApplicationInfo();
    void WriteDriveInfo();
    void WriteInitializationInfo();
    std::string WriteLogEntry(Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc);
    void WriteMemoryUsageInfo();
    void WriteOSInfo();
    void WriteSystemMemoryInfo();
    void WriteTimeInfo();

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

    bool Initialize(const TASWLogConfig& config) override;

    bool Open() override;
    bool Close() override;
    bool IsOpen() const noexcept override;

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

    void Log(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
    void LogRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;

    void LogForce(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
    void LogForceRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
};

} // namespace ASWLog

#endif // ASWLog_ConsoleLogH

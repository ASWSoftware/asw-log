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
    std::atomic<bool> m_ColorSupported{ false }; // Set by EnableAnsiColorSupport() during Initialize()
    std::atomic<bool> m_UseColor{ true }; // Guards ANSI color wrapping; thread-safe via GetUseColor()/SetUseColor()

    // Per-level ANSI color escape sequences used when GetUseColor() is true. Defaults suit a
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
    void EnableAnsiColorSupport();
    void Finalize();
    [[nodiscard]] std::string_view LevelColorUnlocked(Level level) const noexcept; // Caller must hold m_ConsoleMutex.
    bool OpenUnlocked();
    void WriteApplicationInfo();
    void WriteDriveInfo();
    void WriteInitializationInfo();
    std::string WriteLogEntry(Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc);
    void WriteMemoryUsageInfo();
    void WriteOSInfo();
    void WriteSystemMemoryInfo();
    void WriteTimeInfo();

protected:
    std::string_view GetLoggerClassName() const noexcept final
    {
        return "TASWConsoleLog";
    }

public: // Static methods
    static TASWConsoleLog& GetInstance(); // Singleton support for the common static instance

public:
    TASWConsoleLog() = default;
    ~TASWConsoleLog();

    bool Initialize(const TASWLogConfig& config) override;

    bool Open() override;
    bool Close() override;
    bool IsOpen() const noexcept override;

    // True if ANSI virtual terminal support was enabled during Initialize() (Windows) or is
    // assumed supported (POSIX terminals accept ANSI codes natively).
    [[nodiscard]] bool IsColorSupported() const noexcept;

    [[nodiscard]] std::string GetLevelColor(Level level) const; // Current ANSI color escape sequence used for `level`.
    void SetLevelColor(Level level, std::string colorCode); // Overrides the color for `level`; pass an empty string to disable color.

    void ResetLevelColor(Level level) noexcept; // Restores `level`'s color to its built-in default.
    void ResetLevelColors() noexcept; // Restores every level's color to its built-in default.

    // True if log lines are wrapped in the per-level ANSI color codes (see SetLevelColor()/
    // GetLevelColor()) when writing. Default: true. Thread-safe to read/write from any thread.
    [[nodiscard]] bool GetUseColor() const noexcept;
    // Enables/disables ANSI color wrapping. On Windows, virtual terminal processing is enabled
    // best-effort in Initialize(); if that fails (see IsColorSupported()), color escape codes
    // may print literally, so callers on older Windows consoles may want to pass false here.
    void SetUseColor(bool useColor) noexcept;

    void Log(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
    void LogRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;

    void LogForce(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
    void LogForceRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
};

} // namespace ASWLog

#endif // ASWLog_ConsoleLogH

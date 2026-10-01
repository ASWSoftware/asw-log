/* **************************************************************************
ASWLog_TextLogBase.h
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

#ifndef ASWLog_TextLogBaseH
#define ASWLog_TextLogBaseH
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWTextLogBase
//
// Base for loggers that write each entry as a line of text, such as TASWFileLog and TASWConsoleLog. It implements
// the public logging methods once for all of them: the level check, the lock, the safety net that keeps logging from
// throwing into the application, the startup and shutdown lines, the line format, and the OnLogEntry callback.
//
// A derived logger implements the protected hooks for its own output. Each hook whose name ends in "Unlocked" is
// called with m_Mutex held, so it must not call a public method of this logger (which would lock it again). The
// derived logger's destructor must call Finalize(), which writes the shutdown line and closes the output.
//
// To change the line layout, derive from a logger (e.g. TASWFileLog) and override FormatLine().
/////////////////////////////////////////////////////////////////////////////
class TASWTextLogBase : public TASWLogBase
{
private:
    typedef TASWLogBase inherited;

protected:
    mutable std::mutex m_Mutex; // Serializes the writes, and the derived logger's own state
    std::atomic<bool> m_IsOpen{ false };

private:
    void AppendLineEnding(std::string& line) const;
    void DispatchLogCallback(Level level, std::string_view formattedLine) const noexcept;
    void LogEntry(Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc) noexcept;
    void WriteApplicationInfo();
    void WriteDriveInfo();
    void WriteInitializationInfo();
    std::string WriteLogEntry(Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc);
    void WriteMemoryUsageInfo();
    void WriteOSInfo();
    void WriteSystemMemoryInfo();
    void WriteTimeInfo();

protected:
    // Called after each entry from the Log* methods, and after Initialize() wrote the startup lines. Does nothing by
    // default.
    virtual void AfterEntryUnlocked();

    // Closes the output. Must clear m_IsOpen and m_IsInitialized.
    virtual bool CloseUnlocked() = 0;

    // Called before each entry from the Log* methods; returns false to drop the entry. By default, true if the
    // logger is initialized and open.
    virtual bool EnsureReadyUnlocked();

    // Writes the shutdown line (if TASWLogConfig::WriteShutdownLog) and closes the output, if the logger is
    // initialized. Never throws. Each logger calls it from its destructor, since the hooks can't be called from this
    // class's destructor. A class that overrides FormatLine() calls it from its own destructor too, if it wants the
    // shutdown line in its own format (by the time a base destructor runs, the override is gone).
    void Finalize() noexcept;

    // Returns the line for an entry, without its line ending, laid out from the TASWLogConfig options:
    // "[time][LEVEL][P:pid][T:tid][WS:bytes][PWS:bytes][function][file:line]: message". 'now' is the entry's time
    // (from NowUTC()). Not called for LogRaw()/LogForceRaw(), which write the message as is. Called with m_Mutex held.
    virtual std::string FormatLine(Level level, std::string_view message, std::source_location loc,
        std::chrono::system_clock::time_point now) const;

    // Called by Initialize() after it has stored the config: prepares and opens the output. Returning false fails
    // Initialize().
    virtual bool InitializeUnlocked() = 0;

    // Opens the output. Must set m_IsOpen and m_IsInitialized.
    virtual bool OpenUnlocked() = 0;

    // Called before each line is formatted and written, including the startup and shutdown lines, with the line's
    // time; returns false to drop the line. E.g. a file logger rotates its file here. Returns true by default.
    virtual bool PrepareWriteUnlocked(std::chrono::system_clock::time_point now);

    // Writes a finished line. 'endsLine' is true if 'line' ends with the line ending (false for LogRaw()).
    virtual void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) = 0;

public:
    bool Initialize(const TASWLogConfig& config) final;

    bool Open() final;
    bool Close() final;
    bool IsOpen() const noexcept final;

    void Log(Level level, std::string_view message, std::source_location loc = std::source_location::current()) final;
    void LogRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) final;

    void LogForce(Level level, std::string_view message, std::source_location loc = std::source_location::current()) final;
    void LogForceRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) final;
};

} // namespace ASWLog

#endif // ASWLog_TextLogBaseH

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
#include "ASWLog_Formatter.h" // For TASWLogConfig::Formatter
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWTextLogBase
//
// Base for loggers that write each entry as a line of text, such as TASWFileLog and TASWConsoleLog. It implements
// WriteRecord() once for all of them: the lock, the safety net that keeps logging from throwing into the application,
// the line format, and the OnLogEntry callback, as well as the startup and shutdown lines. (TASWLogBase::Write() has
// already checked the level and stamped the record.)
//
// A derived logger implements the protected hooks for its own output. Each hook whose name ends in "Unlocked" is
// called with m_Mutex held, so it must not call a public method of this logger (which would lock it again). The
// derived logger's destructor must call Finalize(), which writes the shutdown line and closes the output.
//
// To change the line layout, assign a formatter to TASWLogConfig::Formatter (see IASWLogFormatter).
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
    void DispatchLogCallback(const TASWLogRecord& record, std::string_view formattedLine) const noexcept;
    void WriteApplicationInfo();
    void WriteDriveInfo();
    void WriteInfoLine(std::string_view message, std::source_location loc = std::source_location::current());
    void WriteInitializationInfo();
    std::string WriteLogEntry(const TASWLogRecord& record);
    void WriteMemoryUsageInfo();
    void WriteOSInfo();
    void WriteSystemMemoryInfo();
    void WriteTimeInfo();

protected:
    // Called after each entry passed to Write(), and after Initialize() wrote the startup lines. Does nothing by
    // default.
    virtual void AfterEntryUnlocked();

    // Closes the output. Must clear m_IsOpen and m_IsInitialized.
    virtual bool CloseUnlocked() = 0;

    // Called before each entry passed to Write(); returns false to drop the entry. By default, true if the logger is
    // initialized and open.
    virtual bool EnsureReadyUnlocked();

    // Writes the shutdown line (if TASWLogConfig::WriteShutdownLog) and closes the output, if the logger is
    // initialized. Never throws. Each logger calls it from its destructor, since the hooks can't be called from this
    // class's destructor.
    void Finalize() noexcept;

    // Called by Flush(): pushes the written entries out of the output's buffers. Returns false if the output isn't open
    // or the flush failed.
    virtual bool FlushUnlocked() = 0;

    // Called by Initialize() after it has stored the config: prepares and opens the output. Returning false fails
    // Initialize().
    virtual bool InitializeUnlocked() = 0;

    // Opens the output. Must set m_IsOpen and m_IsInitialized.
    virtual bool OpenUnlocked() = 0;

    // Called before each line is formatted and written, including the startup and shutdown lines, with the record's
    // time; returns false to drop the line. E.g. a file logger rotates its file here. Entries stamped by different
    // threads can get the lock out of time order, so 'now' can be a little earlier than for the previous line.
    // Returns true by default.
    virtual bool PrepareWriteUnlocked(std::chrono::system_clock::time_point now);

    // Writes a finished line. 'endsLine' is true if 'line' ends with the line ending (false for a Raw record).
    virtual void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) = 0;

protected: // TASWLogBase hook
    // Writes the entry under m_Mutex, through the hooks above, then calls OnLogEntry. Never throws.
    void WriteRecord(const TASWLogRecord& record) noexcept final;

public:
    bool Initialize(const TASWLogConfig& config) final;

    bool Open() final;
    bool Close() final;
    bool Flush() noexcept final;
    bool IsOpen() const noexcept final;
};

} // namespace ASWLog

#endif // ASWLog_TextLogBaseH

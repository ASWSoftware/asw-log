/* **************************************************************************
ASWLog_DebuggerLog.h
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

#ifndef ASWLog_DebuggerLogH
#define ASWLog_DebuggerLogH
//---------------------------------------------------------------------------
#include <chrono>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWDebuggerLog
//
// Writes each line to the debugger's output: on Windows with OutputDebugStringW, so it shows in the RAD Studio Event
// Log, Visual Studio's Output window, and DebugView (which needs no debugger). With nobody listening, each line still
// costs some microseconds, so add it where it's wanted, e.g. in debug builds. Each line is formatted like a file
// logger's (TASWLogConfig::Line), including its line ending; the text is converted from UTF-8 to UTF-16.
//
// Other platforms have no debugger output, so there it writes nothing: the class builds everywhere, so the same code
// can add it on every platform, but there ShouldLog() is false and no line is formatted. (On Linux, see the syslog
// logger.)
//
// Takes the same TASWLogConfig as the other text loggers, ignoring its File settings. Thread-safe.
/////////////////////////////////////////////////////////////////////////////
class TASWDebuggerLog : public TASWTextLogBase
{
private:
    typedef TASWTextLogBase inherited;

protected: // TASWTextLogBase hooks
    bool CloseUnlocked() override;
    bool FlushUnlocked() override; // Nothing is buffered: true while open
    bool InitializeUnlocked() override;
    bool OpenUnlocked() override;
    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point now) override; // False where it writes nothing
    void WriteCrashLineDirect(std::string_view line) noexcept override;
    void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) override;

protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TASWDebuggerLog";
    }

public: // Static methods
    // Singleton support for the common static instance. Never destroyed; finalized at exit in static destruction
    // order, like TASWFileLog::GetInstance() (see there).
    static TASWDebuggerLog& GetInstance();

public:
    TASWDebuggerLog() = default;
    // Writes the shutdown line (see TASWTextLogBase::Finalize())
    ~TASWDebuggerLog() override;

    TASWDebuggerLog(const TASWDebuggerLog&) = delete;
    TASWDebuggerLog& operator=(const TASWDebuggerLog&) = delete;

    // As for the other loggers on Windows; always false where it writes nothing
    bool ShouldLog(Level level) const noexcept override;
    bool ShouldLog(const TASWLogRecord& record) const noexcept override;
};

} // namespace ASWLog

#endif // #ifndef ASWLog_DebuggerLogH

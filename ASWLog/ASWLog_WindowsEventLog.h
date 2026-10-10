/* **************************************************************************
ASWLog_WindowsEventLog.h
Author: Anthony S. West - ASW Software

A logging library.

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

#ifndef ASWLog_WindowsEventLogH
#define ASWLog_WindowsEventLogH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWWindowsEventLog
//
// Reports each line to the Windows Event Log, in the Application log, under the event source
// TASWLogConfig::EventLog.Source (empty: the executable's file name without its extension). The level sets the event's
// type, as Event Viewer shows it: Trace, Debug and Info an Information event, Warn a Warning, Error and Critical an
// Error; and its event ID, from EventLog.EventIds (by default 1000 for Trace to 1005 for Critical). Each line is
// formatted like a file logger's (TASWLogConfig::Line) and reported without its line ending; a line longer than
// MaxMessageSize bytes is cut to fit, ending with " [cut]" (the Event Log refuses longer ones).
//
// Each event takes the Event Log service about a hundred microseconds, and the Application log has a limited size, so
// this logger suits warnings and errors, not a stream of details. Consider:
//
//     ASWLog::TASWWindowsEventLog::GetInstance().SetMinimumLevel(ASWLog::Level::Warn);
//
// and turning off config.Line.ShowTimestamp, since Event Viewer shows each event's time (it doesn't show the process
// id). A multi-log passes one config to all its loggers, so an Event Log logger behind one writes the same layout as
// the others.
//
// Registering the source: until a source is registered, Event Viewer shows each of its events with "The description
// for Event ID ... cannot be found" before the text. RegisterSource() registers it, with the .NET Framework's
// EventLogMessages.dll (part of Windows) as its message file, which shows the text as it is. That needs administrator
// rights, once, e.g. in the application's installer; the registry entries it makes are the same as these two commands
// (the first is shown on two lines; in a .bat file, write each % as %%):
//
//     reg add "HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\MyApp" /v EventMessageFile
//         /t REG_EXPAND_SZ /d "%SystemRoot%\Microsoft.NET\Framework\v4.0.30319\EventLogMessages.dll"
//     reg add "HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\MyApp" /v TypesSupported /t REG_DWORD /d 7
//
// Initialize(), Open() and Reconfigure() open the source with RegisterEventSourceW() (which doesn't need the
// registration); Close() closes it. A crash line formatted in normal context (e.g. std::terminate, the unhandled
// exception filter, HandleCrash()) is reported as an Error event; nothing is reported if the logger's lock stays busy
// in a crash (see ASWLog_CrashHandler.h), since reporting an event allocates and could wait for a lock the crashed
// thread holds.
//
// Other platforms have no Windows Event Log, so there it writes nothing: the class builds everywhere, so the same code
// can add it on every platform, but there ShouldLog() is false and no line is formatted. (On Linux, see the syslog
// logger.)
//
// Takes the same TASWLogConfig as the other text loggers, ignoring its File settings. Thread-safe.
/////////////////////////////////////////////////////////////////////////////
class TASWWindowsEventLog : public TASWTextLogBase
{
private:
    typedef TASWTextLogBase inherited;

public:
    // An event's type, as Event Viewer shows it (the values of EVENTLOG_ERROR_TYPE etc.)
    enum class EventType : std::uint16_t
    {
        Error       = 1,
        Warning     = 2,
        Information = 4,
    };

    // The longest message reported, in bytes of UTF-8: the Event Log's limit of 31,839 characters (UTF-16 code units),
    // which a text of that many bytes never exceeds
    static constexpr std::size_t MaxMessageSize = 31839;

private:
    void* m_EventSource = nullptr; // The HANDLE from RegisterEventSourceW(), or null

private:
    void CloseEventSource() noexcept;
    bool OpenEventSourceFromConfigUnlocked();

protected: // TASWTextLogBase hooks
    bool CloseUnlocked() override;
    bool FlushUnlocked() override; // Nothing is buffered: true while open
    bool InitializeUnlocked() override;
    bool OpenUnlocked() override;
    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point now) override; // False where it writes nothing
    bool ReconfigureUnlocked(const TASWLogConfig& previous) override;
    void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) override;

protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TASWWindowsEventLog";
    }

    // Called by Initialize(), Open() and Reconfigure() with the source (EventLog.Source, or the executable's file name
    // without its extension if that is empty): opens it with RegisterEventSourceW(), replacing the source opened
    // before. Reports an ErrorKind::OpenFailed error and returns false if it can't, keeping the previous source. Does
    // nothing and returns true where there is no Event Log.
    virtual bool OpenEventSourceUnlocked(const std::string& source);

    // Reports one event with 'message' (UTF-8, at most MaxMessageSize bytes) as its text, through ReportEventW().
    // Reports an ErrorKind::WriteFailed error if that fails. Does nothing where there is no Event Log.
    virtual void ReportEventUnlocked(EventType type, std::uint16_t eventId, std::string_view message);

public: // Static methods
    // Singleton support for the common static instance. Never destroyed; finalized at exit in static destruction
    // order, like TASWFileLog::GetInstance() (see there).
    static TASWWindowsEventLog& GetInstance();

    // Registers 'source' (empty: the executable's file name without its extension) in the Application log, with
    // EventLogMessages.dll as its message file (see above), replacing an earlier registration of that name. Needs
    // administrator rights; returns false if it couldn't (or where there is no Event Log, or if 'source' holds a '\').
    static bool RegisterSource(std::string_view source = {}) noexcept;

    // Removes the registration of 'source' (empty: as for RegisterSource()). Needs administrator rights; returns true
    // if the source isn't registered afterwards (also if it wasn't), false if it couldn't be removed (or where there is
    // no Event Log, or if 'source' holds a '\').
    static bool UnregisterSource(std::string_view source = {}) noexcept;

public:
    TASWWindowsEventLog() = default;
    // Writes the shutdown line (see TASWTextLogBase::Finalize())
    ~TASWWindowsEventLog() override;

    TASWWindowsEventLog(const TASWWindowsEventLog&) = delete;
    TASWWindowsEventLog& operator=(const TASWWindowsEventLog&) = delete;

    // As for the other loggers on Windows; always false where there is no Event Log
    bool ShouldLog(Level level) const noexcept override;
    bool ShouldLog(const TASWLogRecord& record) const noexcept override;
};

} // namespace ASWLog

#endif // #ifndef ASWLog_WindowsEventLogH

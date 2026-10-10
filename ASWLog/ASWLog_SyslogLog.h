/* **************************************************************************
ASWLog_SyslogLog.h
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

#ifndef ASWLog_SyslogLogH
#define ASWLog_SyslogLogH
//---------------------------------------------------------------------------
#include <chrono>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWSyslogLog
//
// Sends each line to the system log with syslog() from the C library; on systemd hosts it lands in the journal
// (journalctl). The level becomes the message's priority: Trace and Debug LOG_DEBUG, Info LOG_INFO, Warn LOG_WARNING,
// Error LOG_ERR, Critical LOG_CRIT; the facility is TASWLogConfig::Syslog.Facility. Each line is formatted like a file
// logger's (TASWLogConfig::Line) and sent without its line ending; line breaks inside it are sent as Line.Multiline
// says (with the default, Preserve, journald keeps them, and rsyslog writes them as #012). ASWLog doesn't limit a
// message's size, but the syslog daemon may cut a very long one.
//
// syslog adds the time, the host, the ident and the process id to each message itself, so the default line repeats
// two of them. For syslog alone, consider:
//
//     config.Line.ShowTimestamp = false;
//     config.Line.ShowProcessId = false;
//
// The timestamp is still worth keeping where the moment of the call matters: syslog's time is when the line is sent,
// which with TASWLogConfig::Async is a little later, and has less precision in a classic syslog file. A multi-log
// passes one config to all its loggers, so a syslog logger behind one writes the same layout as the others.
//
// Initialize() and Reconfigure() call openlog() with Syslog.Ident (or the executable's file name) and LOG_PID; the
// logger never calls closelog(). The ident is process-wide, so with several syslog loggers the one initialized or
// reconfigured last sets it, and the application's own openlog() call replaces it. The facility is sent with each
// message, so each logger keeps its own.
//
// A crash line formatted in normal context (e.g. std::terminate, HandleCrash()) is sent as LOG_CRIT; syslog() isn't
// async-signal-safe, so nothing is sent from a POSIX signal handler, or if the logger's lock stays busy (see
// ASWLog_CrashHandler.h).
//
// Windows has no syslog, so there it writes nothing: the class builds everywhere, so the same code can add it on every
// platform, but there ShouldLog() is false and no line is formatted. (On Windows, see the debugger logger.)
//
// Takes the same TASWLogConfig as the other text loggers, ignoring its File settings. Thread-safe.
/////////////////////////////////////////////////////////////////////////////
class TASWSyslogLog : public TASWTextLogBase
{
private:
    typedef TASWTextLogBase inherited;

private:
    void OpenSyslogFromConfigUnlocked();

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
        return "TASWSyslogLog";
    }

    // Called by Initialize() and Reconfigure() with the ident (Syslog.Ident, or the executable's file name if that is
    // empty) and Syslog.Facility: calls openlog() with LOG_PID. Does nothing where there is no syslog.
    virtual void OpenSyslogUnlocked(const std::string& ident, SyslogFacility facility);

    // Sends one message: 'priority' is the facility and the severity combined, as syslog() takes it (e.g. 14 for
    // LOG_USER | LOG_INFO). Does nothing where there is no syslog.
    virtual void SendToSyslogUnlocked(int priority, std::string_view message);

public: // Static methods
    // Singleton support for the common static instance. Never destroyed; finalized at exit in static destruction
    // order, like TASWFileLog::GetInstance() (see there).
    static TASWSyslogLog& GetInstance();

public:
    TASWSyslogLog() = default;
    // Writes the shutdown line (see TASWTextLogBase::Finalize())
    ~TASWSyslogLog() override;

    TASWSyslogLog(const TASWSyslogLog&) = delete;
    TASWSyslogLog& operator=(const TASWSyslogLog&) = delete;

    // As for the other loggers where there is a syslog; always false on Windows
    bool ShouldLog(Level level) const noexcept override;
    bool ShouldLog(const TASWLogRecord& record) const noexcept override;
};

} // namespace ASWLog

#endif // #ifndef ASWLog_SyslogLogH

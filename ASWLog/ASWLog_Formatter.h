/* **************************************************************************
ASWLog_Formatter.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog line formatters.

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

#ifndef ASWLog_FormatterH
#define ASWLog_FormatterH
//---------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// IASWLogFormatter
//
// Turns a log entry into the line a text logger writes. Assign one to
// TASWLogConfig::Formatter; without one, a logger uses TASWTextFormatter's
// layout.
//
// A formatter can be shared by several loggers, which may call it at the
// same time from different threads, so Format() must not change the
// formatter's state without its own synchronization.
/////////////////////////////////////////////////////////////////////////////
class IASWLogFormatter
{
public:
    virtual ~IASWLogFormatter() = default;

    // Returns the line for 'record', without its line ending (the logger adds TASWLogConfig::LogLineEnding).
    // 'config' is the calling logger's config. Not called for LogRaw()/LogForceRaw(), which write the message as is.
    // May throw, e.g. std::bad_alloc: the entry is then dropped.
    [[nodiscard]] virtual std::string Format(const TASWLogRecord& record, const TASWLogConfig& config) const = 0;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTextFormatter
//
// The built-in text layout, used when TASWLogConfig::Formatter is empty:
// "[time][LEVEL][P:pid][T:tid][WS:bytes][PWS:bytes][function][file:line]: message", where each bracketed field is
// written only if its TASWLogConfig option is on (LogUTCDateTime, LogLevelStr, LogProcessId, LogThreadId,
// LogAppMem_WorkingSet, LogAppMem_PeakWorkingSet, LogMethodName, LogSourceLine).
/////////////////////////////////////////////////////////////////////////////
class TASWTextFormatter : public IASWLogFormatter
{
public: // Static methods
    // The layout itself, callable without an instance
    [[nodiscard]] static std::string FormatLine(const TASWLogRecord& record, const TASWLogConfig& config);

public:
    [[nodiscard]] std::string Format(const TASWLogRecord& record, const TASWLogConfig& config) const override;
};

} // namespace ASWLog

#endif // ASWLog_FormatterH

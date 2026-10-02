/* **************************************************************************
ASWLog_Formatter.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_Formatter.h"
//---------------------------------------------------------------------------
// System includes here
#include <filesystem>
#include <format>
#include <iterator>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWTextFormatter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::string TASWTextFormatter::Format(const TASWLogRecord& record, const TASWLogConfig& config) const
{
    return FormatLine(record, config);
}

//---------------------------------------------------------------------------
std::string TASWTextFormatter::FormatLine(const TASWLogRecord& record, const TASWLogConfig& config)
{
    std::string line;
    line.reserve(record.Message.size() + 256);

    if (config.LogUTCDateTime)
        std::format_to(std::back_inserter(line), "[{}]", Time::ToISO8601String(record.Timestamp));

    if (config.LogLevelStr)
        std::format_to(std::back_inserter(line), "[{}]", Level_ToString(record.LogLevel));

    if (config.LogProcessId)
        std::format_to(std::back_inserter(line), "[P:{}]", record.ProcessId);

    if (config.LogThreadId)
        std::format_to(std::back_inserter(line), "[T:{}]", record.ThreadId);

    if (config.LogAppMem_WorkingSet || config.LogAppMem_PeakWorkingSet)
    {
        const auto memoryUsage = GetMemoryUsage();
        if (config.LogAppMem_WorkingSet)
            std::format_to(std::back_inserter(line), "[WS:{}]", memoryUsage.WorkingSetBytes);

        if (config.LogAppMem_PeakWorkingSet)
            std::format_to(std::back_inserter(line), "[PWS:{}]", memoryUsage.PeakWorkingSetBytes);
    }

    if (config.LogMethodName)
        std::format_to(std::back_inserter(line), "[{}]", record.Location.function_name());

    if (config.LogSourceLine)
    {
        std::filesystem::path fullPath(record.Location.file_name());
        std::format_to(std::back_inserter(line), "[{}:{}]", fullPath.filename().string(), record.Location.line());
    }

    line += ": ";
    line.append(record.Message);
    return line;
}

//---------------------------------------------------------------------------

} // namespace ASWLog

/* **************************************************************************
ASWLog_Formatter.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_Formatter.h"
//---------------------------------------------------------------------------
// System includes here
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iterator>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

void AppendNumberField(std::string& line, std::string_view label, std::uint64_t value);

//---------------------------------------------------------------------------

/*
  AppendNumberField

  Appends "[<label><value>]", the value in decimal
*/
void AppendNumberField(std::string& line, std::string_view label, std::uint64_t value)
{
    char digits[20];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);

    line += '[';
    line.append(label);
    line.append(digits, result.ptr);
    line += ']';
}

} // namespace

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

    // The fixed fields are written directly, which is several times faster than std::format here
    if (config.Line.ShowTimestamp)
    {
        char timestamp[Time::ISO8601BufferSize];
        line += '[';
        line.append(timestamp, Time::WriteISO8601(timestamp, record.Timestamp, config.Line.TimestampZone, config.Line.TimestampPrecision));
        line += ']';
    }

    if (config.Line.ShowLevel)
    {
        line += '[';
        line.append(Level_ToString(record.LogLevel));
        line += ']';
    }

    if (config.Line.ShowCategory && !record.Category.empty())
    {
        line += '[';
        line.append(record.Category);
        line += ']';
    }

    if (config.Line.ShowProcessId)
        AppendNumberField(line, "P:", record.ProcessId);

    if (config.Line.ShowThreadId)
        AppendNumberField(line, "T:", record.ThreadId);

    if (config.Line.ShowWorkingSet || config.Line.ShowPeakWorkingSet)
    {
        const auto memoryUsage = GetMemoryUsage();
        if (config.Line.ShowWorkingSet)
            AppendNumberField(line, "WS:", memoryUsage.WorkingSetBytes);

        if (config.Line.ShowPeakWorkingSet)
            AppendNumberField(line, "PWS:", memoryUsage.PeakWorkingSetBytes);
    }

    if (config.Line.ShowFunctionName)
        std::format_to(std::back_inserter(line), "[{}]", record.Location.function_name());

    if (config.Line.ShowSourceLine)
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

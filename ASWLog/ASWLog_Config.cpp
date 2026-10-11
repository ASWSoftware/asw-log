/* **************************************************************************
ASWLog_Config.cpp
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
#include "ASWLog_Config.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <format>
#include <optional>
#include <stdexcept>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_CategoryLog.h"
#include "ASWLog_Formatter.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The keys ApplyEnvironment() reads, in the order it applies them (PATTERN after FORMAT)
constexpr std::array<std::string_view, 15> SettingKeys{
    "LEVEL", "CATEGORIES", "FOLDER", "FILE", "FLUSH", "SYNC_AT_LEVEL", "ASYNC", "BACKTRACE", "TIME_ZONE",
    "TIME_PRECISION", "MULTILINE", "SHOW_FUNCTION", "SHOW_SOURCE", "FORMAT", "PATTERN"
};

constexpr std::string_view BoolValues = "1, true, yes or on; 0, false, no or off";
constexpr std::string_view LevelValues = "Trace, Debug, Info, Warn, Error, Critical or Off";

//---------------------------------------------------------------------------

bool ApplyNamedSetting(TASWLogConfig& config, std::string_view name, std::string_view key, std::string_view value);
template<typename T>
std::string Assign(const std::optional<T>& parsed, T& setting, std::string_view expected);
std::optional<bool> ParseBool(std::string_view text) noexcept;
std::optional<std::size_t> ParseCount(std::string_view text) noexcept;
void ReportInvalidSetting(const TASWLogConfig& config, std::string message);
std::filesystem::path UTF8ToPath(std::string_view text);

//---------------------------------------------------------------------------

/*
  ApplyNamedSetting

  Sets the setting 'key' of 'config' from 'value' (see TASWLogConfig::ApplySetting()), naming it 'name' in a report
*/
bool ApplyNamedSetting(TASWLogConfig& config, std::string_view name, std::string_view key, std::string_view value)
{
    const auto is = [key](std::string_view candidate)
        {
            return Detail::EqualsIgnoringCase(key, candidate);
        };

    if (std::none_of(SettingKeys.begin(), SettingKeys.end(), is))
    {
        ReportInvalidSetting(config, std::format("'{}' isn't a setting", name));
        return false;
    }

    const auto text = Detail::TrimSpaces(value);
    if (text.empty())
        return true;

    std::string problem; // Why 'text' isn't a valid value, if it isn't

    if (is("LEVEL"))
    {
        problem = Assign(Level_FromString(text), config.InitialMinimumLevel, LevelValues);
    }
    else if (is("CATEGORIES"))
    {
        TASWCategoryLog::ApplyLevels(text, problem);
    }
    else if (is("FOLDER"))
    {
        config.File.FolderPath = UTF8ToPath(text);
    }
    else if (is("FILE"))
    {
        config.File.FilePath = UTF8ToPath(text);
    }
    else if (is("FLUSH"))
    {
        problem = Assign(FlushMode_FromString(text), config.File.Flush, "EveryWrite, OnNewLine, Manual or Periodic");
    }
    else if (is("SYNC_AT_LEVEL"))
    {
        problem = Assign(Level_FromString(text), config.File.SyncToDiskAtLevel, LevelValues);
    }
    else if (is("ASYNC"))
    {
        problem = Assign(ParseBool(text), config.Async.Enabled, BoolValues);
    }
    else if (is("BACKTRACE"))
    {
        problem = Assign(ParseCount(text), config.Backtrace.Capacity, "a number of entries, 0 = none");
    }
    else if (is("TIME_ZONE"))
    {
        problem = Assign(TimeZone_FromString(text), config.Line.TimestampZone, "UTC or Local");
    }
    else if (is("TIME_PRECISION"))
    {
        problem = Assign(TimePrecision_FromString(text), config.Line.TimestampPrecision,
            "Milliseconds, Microseconds or Nanoseconds (ms, us, ns)");
    }
    else if (is("MULTILINE"))
    {
        problem = Assign(MultilineMode_FromString(text), config.Line.Multiline, "Preserve, Indent or Escape");
    }
    else if (is("SHOW_FUNCTION"))
    {
        problem = Assign(ParseBool(text), config.Line.ShowFunctionName, BoolValues);
    }
    else if (is("SHOW_SOURCE"))
    {
        problem = Assign(ParseBool(text), config.Line.ShowSourceLine, BoolValues);
    }
    else if (is("FORMAT"))
    {
        if (Detail::EqualsIgnoringCase(text, "TEXT"))
            config.Line.Formatter.reset();
        else if (Detail::EqualsIgnoringCase(text, "JSON"))
            config.Line.Formatter = std::make_shared<const TASWJSONFormatter>();
        else
            problem = "expected Text or JSON";
    }
    else if (is("PATTERN"))
    {
        try
        {
            config.Line.Formatter = std::make_shared<const TASWPatternFormatter>(text);
        }
        catch (const std::invalid_argument& exception)
        {
            problem = exception.what();
        }
    }

    if (problem.empty())
        return true;

    ReportInvalidSetting(config, std::format("{}='{}': {}", name, text, problem));

    return false;
}

/*
  Assign

  Sets 'setting' to the parsed value and returns an empty text, or, if the value couldn't be parsed, leaves 'setting'
  as it is and returns "expected <expected>"
*/
template<typename T>
std::string Assign(const std::optional<T>& parsed, T& setting, std::string_view expected)
{
    if (!parsed)
        return std::format("expected {}", expected);

    setting = *parsed;

    return {};
}

/*
  ParseBool

  1, true, yes or on as true; 0, false, no or off as false, ignoring case; otherwise nullopt
*/
std::optional<bool> ParseBool(std::string_view text) noexcept
{
    for (const std::string_view yes : { "1", "true", "yes", "on" })
    {
        if (Detail::EqualsIgnoringCase(text, yes))
            return true;
    }

    for (const std::string_view no : { "0", "false", "no", "off" })
    {
        if (Detail::EqualsIgnoringCase(text, no))
            return false;
    }

    return std::nullopt;
}

/*
  ParseCount

  'text' as a number of decimal digits that fits a std::size_t, or nullopt
*/
std::optional<std::size_t> ParseCount(std::string_view text) noexcept
{
    std::size_t count = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), count);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size())
        return std::nullopt;

    return count;
}

/*
  ReportInvalidSetting

  Reports an invalid setting to the config's OnError, or, if it has none, writes it to stderr. Swallows exceptions.
*/
void ReportInvalidSetting(const TASWLogConfig& config, std::string message)
{
    try
    {
        TASWLogError error;
        error.Kind = ErrorKind::InvalidSetting;
        error.Message = std::move(message);

        if (config.OnError != nullptr)
        {
            config.OnError(error);
        }
        else
        {
            const auto line = std::format("ASWLog: {}\n", error.ToString());
            std::fwrite(line.data(), 1, line.size(), stderr);
            std::fflush(stderr);
        }
    }
    catch (...)
    {
    }
}

/*
  UTF8ToPath

  The path named by 'text' (UTF-8), on Windows too, where a path made from a std::string reads it in the ANSI code page
*/
std::filesystem::path UTF8ToPath(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogConfig
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWLogConfig::ApplyEnvironment(std::string_view prefix)
{
    bool isValid = true;
    std::string name;

    for (const auto key : SettingKeys)
    {
        name.assign(prefix);
        name.append(key);

        const auto value = Detail::ReadEnvironmentVariable(name);
        if (value && !ApplyNamedSetting(*this, name, key, *value))
            isValid = false;
    }

    return isValid;
}

//---------------------------------------------------------------------------
bool TASWLogConfig::ApplySetting(std::string_view key, std::string_view value)
{
    return ApplyNamedSetting(*this, key, Detail::TrimSpaces(key), value);
}

//---------------------------------------------------------------------------

} // namespace ASWLog

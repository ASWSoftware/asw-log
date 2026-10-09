/* **************************************************************************
ASWLog_Formatter.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog line formatters.

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

#ifndef ASWLog_FormatterH
#define ASWLog_FormatterH
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
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
// TASWLogConfig::Line.Formatter; without one, a logger uses
// TASWTextFormatter's layout.
//
// A formatter can be shared by several loggers, which may call it at the
// same time from different threads, so Format() must not change the
// formatter's state without its own synchronization.
/////////////////////////////////////////////////////////////////////////////
class IASWLogFormatter
{
public:
    virtual ~IASWLogFormatter() = default;

    // Returns the line for 'record', without its line ending (the logger adds TASWLogConfig::Line.Ending).
    // 'config' is the calling logger's config. Not called for a Raw record (LogRaw()/LogForceRaw()), whose message is
    // written as is, unless FormatsRawEntries() says so.
    // May throw, e.g. std::bad_alloc: the entry is then dropped.
    [[nodiscard]] virtual std::string Format(const TASWLogRecord& record, const TASWLogConfig& config) const = 0;

    // True if Format() is called for a Raw record too, whose line the logger then ends with the line ending like any
    // other (e.g. for a format in which every line must be a whole entry, such as JSON Lines). False by default: the
    // logger writes a Raw record's message as is, without a line ending.
    [[nodiscard]] virtual bool FormatsRawEntries() const noexcept
    {
        return false;
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWJSONFormatter
//
// Writes each entry as a JSON object on one line (JSON Lines), e.g.
//     config.Line.Formatter = std::make_shared<const ASWLog::TASWJSONFormatter>();
// writes {"time":"2026-10-08T14:31:00.217Z","epoch_ms":1791469860217,"level":"DEBUG","category":"Net","pid":4120,
// "tid":7788,"message":"connected"}. Like TASWTextFormatter, it writes the fields whose TASWLineConfig Show* option
// is on, in the same order:
//   "time", "epoch_ms"  the timestamp, as TASWTextFormatter writes it (TimestampZone and TimestampPrecision), and as
//                       whole milliseconds since 1970-01-01 UTC (ShowTimestamp)
//   "level"             TRACE, DEBUG, INFO, WARN, ERROR or CRITICAL (ShowLevel)
//   "category"          the category's name, if the entry has one (ShowCategory; see TASWCategoryLog)
//   "pid", "tid"        the process and thread ids, as numbers (ShowProcessId, ShowThreadId)
//   "ws", "pws"         the process's working set and peak working set in bytes, as numbers (ShowWorkingSet,
//                       ShowPeakWorkingSet)
//   "function"          the function that logged the entry (ShowFunctionName)
//   "file", "line"      its source file's name (without folders) and line, a number (ShowSourceLine)
//   "raw"               true for a LogRaw()/LogForceRaw() entry, which gets a line of its own like any other
//   "message"           the message, always
// Texts are escaped as RFC 8259 asks, with each invalid UTF-8 sequence replaced by U+FFFD (see JSON::AppendString()),
// so a multi-line message stays on one line. The logger adds the line ending (TASWLineConfig::Ending): LF, as JSON
// Lines asks, or CRLF, which JSON readers accept too. A crash line written in a POSIX signal handler, or when the
// logger's lock stays busy, is a JSON object too, with fixed fields (see ASWLog_CrashHandler.h). Thread-safe, so
// loggers can share one.
/////////////////////////////////////////////////////////////////////////////
class TASWJSONFormatter : public IASWLogFormatter
{
public:
    [[nodiscard]] std::string Format(const TASWLogRecord& record, const TASWLogConfig& config) const override;
    [[nodiscard]] bool FormatsRawEntries() const noexcept override; // True
};


/////////////////////////////////////////////////////////////////////////////
// TASWPatternFormatter
//
// A line layout given as a pattern, parsed once when the formatter is made:
//     const auto pattern = "{time} {level:5} {[category] }{message}";
//     config.Line.Formatter = std::make_shared<const ASWLog::TASWPatternFormatter>(pattern);
// writes "2026-10-08T14:31:00.217Z DEBUG [Net] connected". Text outside braces is written as is; "{{" and "}}" write a
// brace. Each placeholder writes one field of the entry:
//   {time}          the timestamp, in TASWLineConfig::TimestampZone and TimestampPrecision
//   {level}         TRACE, DEBUG, INFO, WARN, ERROR or CRITICAL
//   {category}      the category's name, e.g. Net.Http (see TASWCategoryLog), or nothing
//   {pid}, {tid}    the process and thread ids
//   {ws}, {pws}     the process's working set and peak working set in bytes (the memory is read once per line)
//   {function}      the function that logged the entry
//   {file}, {line}  its source file's name (without folders) and line
//   {message}       the message
// A minimum width after the name, e.g. {level:5}, pads the field with spaces on the right (counted in bytes). Text
// inside the braces around the name is written only when the field isn't empty: punctuation and spaces as they are,
// e.g. {[category] }, or anything in single quotes, with '' for a quote, e.g. {'cat=' category ' '}; letters, digits,
// braces and ':' need the quotes. Spaces next to quoted text only separate it from the name and aren't written. An
// empty field writes nothing at all, not even its width's padding.
//
// Unlike TASWTextFormatter, it ignores TASWLineConfig's Show* options: the pattern decides which fields are written.
// The logger adds the line ending (TASWLineConfig::Ending). Thread-safe, so loggers can share one.
/////////////////////////////////////////////////////////////////////////////
class TASWPatternFormatter : public IASWLogFormatter
{
private:
    // What a part of the pattern writes
    enum class TField : std::uint8_t
    {
        Text, // Literal text
        Time,
        Level,
        Category,
        ProcessId,
        ThreadId,
        WorkingSet,
        PeakWorkingSet,
        Function,
        File,
        Line,
        Message,
    };

    // A part of the pattern: literal text (in Prefix), or a placeholder's field with its affixes and minimum width
    struct TPart
    {
        TField Field = TField::Text;
        std::size_t Width = 0;
        std::string Prefix;
        std::string Suffix;
    };

    std::string m_Pattern;
    std::vector<TPart> m_Parts;
    bool m_UsesMemoryUsage = false; // Has {ws} or {pws}

private:
    std::size_t ParsePlaceholder(std::size_t start);

public:
    // Throws std::invalid_argument if 'pattern' is invalid (e.g. an unknown placeholder, or an unclosed brace or
    // quote), naming the problem and its index in the pattern
    explicit TASWPatternFormatter(std::string_view pattern);

    [[nodiscard]] std::string Format(const TASWLogRecord& record, const TASWLogConfig& config) const override;

    // The pattern it was made with
    [[nodiscard]] std::string_view GetPattern() const noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TASWTextFormatter
//
// The built-in text layout, used when TASWLogConfig::Line.Formatter is empty:
// "[time][LEVEL][category][P:pid][T:tid][WS:bytes][PWS:bytes][function][file:line]: message", where each bracketed
// field is written only if its TASWLineConfig option is on (ShowTimestamp, ShowLevel, ShowCategory, ShowProcessId,
// ShowThreadId, ShowWorkingSet, ShowPeakWorkingSet, ShowFunctionName, ShowSourceLine), and the category only if the
// entry has one. For another layout, see TASWPatternFormatter; for JSON Lines, TASWJSONFormatter.
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

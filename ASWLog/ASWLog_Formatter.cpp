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
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The widest placeholder width a pattern may ask for
constexpr std::size_t MaxPatternWidth = 1000;

//---------------------------------------------------------------------------

void AppendField(std::string& line, std::string_view value, std::string_view prefix, std::string_view suffix,
    std::size_t width);
void AppendNumberField(std::string& line, std::string_view label, std::uint64_t value);
bool IsASCIIDigit(char character) noexcept;
bool IsASCIILetter(char character) noexcept;
std::invalid_argument MakePatternError(std::string_view problem, std::size_t index, std::string_view pattern);
std::string_view ToDecimal(char (& buffer)[Time::ISO8601BufferSize], std::uint64_t value) noexcept;

//---------------------------------------------------------------------------

/*
  AppendField

  Appends a pattern's field: nothing if 'value' is empty, else the prefix, the value padded with spaces on the right
  to 'width', and the suffix
*/
void AppendField(std::string& line, std::string_view value, std::string_view prefix, std::string_view suffix,
    std::size_t width)
{
    if (value.empty())
        return;

    line.append(prefix);
    line.append(value);

    if (value.size() < width)
        line.append(width - value.size(), ' ');

    line.append(suffix);
}

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

/*
  IsASCIIDigit

  True for '0' to '9' (unlike std::isdigit, whatever the C locale)
*/
bool IsASCIIDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

/*
  IsASCIILetter

  True for 'a' to 'z' and 'A' to 'Z' (unlike std::isalpha, whatever the C locale)
*/
bool IsASCIILetter(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

/*
  MakePatternError

  The exception for an invalid line pattern: the problem, where it is, and the pattern
*/
std::invalid_argument MakePatternError(std::string_view problem, std::size_t index, std::string_view pattern)
{
    return std::invalid_argument(std::format("{} at index {} of the line pattern \"{}\"", problem, index, pattern));
}

/*
  ToDecimal

  Writes 'value' in decimal into 'buffer' and returns the digits
*/
std::string_view ToDecimal(char (& buffer)[Time::ISO8601BufferSize], std::uint64_t value) noexcept
{
    static_assert(Time::ISO8601BufferSize >= 20, "The buffer must hold the largest 64-bit number");
    const auto result = std::to_chars(buffer, buffer + Time::ISO8601BufferSize, value);
    return std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWPatternFormatter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWPatternFormatter::TASWPatternFormatter(std::string_view pattern)
    : m_Pattern(pattern)
{
    std::string text; // Literal text since the last placeholder
    std::size_t index = 0;

    while (index < pattern.size())
    {
        const char character = pattern[index];
        const bool isDoubled = index + 1 < pattern.size() && pattern[index + 1] == character;
        if ((character == '{' || character == '}') && isDoubled)
        {
            text += character;
            index += 2;
        }
        else if (character == '{')
        {
            if (!text.empty())
            {
                m_Parts.push_back(TPart{ TField::Text, 0, std::move(text), {} });
                text.clear();
            }

            index = ParsePlaceholder(index);
        }
        else if (character == '}')
        {
            throw MakePatternError("Unmatched '}'", index, pattern);
        }
        else
        {
            text += character;
            ++index;
        }
    }

    if (!text.empty())
        m_Parts.push_back(TPart{ TField::Text, 0, std::move(text), {} });
}

//---------------------------------------------------------------------------
std::string TASWPatternFormatter::Format(const TASWLogRecord& record, const TASWLogConfig& config) const
{
    std::string line;
    line.reserve(record.Message.size() + 256);

    const TMemoryUsage memoryUsage = m_UsesMemoryUsage ? GetMemoryUsage() : TMemoryUsage{};
    std::string fileName;

    for (const auto& part : m_Parts)
    {
        char buffer[Time::ISO8601BufferSize];
        std::string_view value;

        switch (part.Field)
        {
            case TField::Text:
                line.append(part.Prefix);
                continue;

            case TField::Time:
                value = std::string_view(buffer, Time::WriteISO8601(buffer, record.Timestamp, config.Line.TimestampZone,
                    config.Line.TimestampPrecision));
                break;

            case TField::Level:
                value = Level_ToString(record.LogLevel);
                break;

            case TField::Category:
                value = record.Category;
                break;

            case TField::ProcessId:
                value = ToDecimal(buffer, record.ProcessId);
                break;

            case TField::ThreadId:
                value = ToDecimal(buffer, record.ThreadId);
                break;

            case TField::WorkingSet:
                value = ToDecimal(buffer, memoryUsage.WorkingSetBytes);
                break;

            case TField::PeakWorkingSet:
                value = ToDecimal(buffer, memoryUsage.PeakWorkingSetBytes);
                break;

            case TField::Function:
                value = record.Location.function_name();
                break;

            case TField::File:
                // As TASWTextFormatter writes it
                fileName = std::filesystem::path(record.Location.file_name()).filename().string();
                value = fileName;
                break;

            case TField::Line:
                value = ToDecimal(buffer, record.Location.line());
                break;

            case TField::Message:
                value = record.Message;
                break;
        }

        AppendField(line, value, part.Prefix, part.Suffix, part.Width);
    }

    return line;
}

//---------------------------------------------------------------------------
std::string_view TASWPatternFormatter::GetPattern() const noexcept
{
    return m_Pattern;
}

//---------------------------------------------------------------------------
/*
    TASWPatternFormatter::ParsePlaceholder

    Parses the placeholder whose '{' is at 'start' in m_Pattern, adds it to m_Parts, and returns the index after its
    '}'. Inside the braces: the affix before the name, the name, an optional ":<width>", and the affix after it. An
    affix is punctuation and spaces, or text in single quotes ('' for a quote), in any mix; spaces next to quoted text
    only separate it from the rest and aren't part of the affix.
*/
std::size_t TASWPatternFormatter::ParsePlaceholder(std::size_t start)
{
    struct TName
    {
        std::string_view Name;
        TField Field;
    };

    static constexpr TName Names[] = {
        { "time", TField::Time }, { "level", TField::Level }, { "category", TField::Category },
        { "pid", TField::ProcessId }, { "tid", TField::ThreadId }, { "ws", TField::WorkingSet },
        { "pws", TField::PeakWorkingSet }, { "function", TField::Function }, { "file", TField::File },
        { "line", TField::Line }, { "message", TField::Message }
    };

    const std::string_view pattern = m_Pattern;
    TPart part;
    std::string* affix = &part.Prefix;
    bool hasName = false;
    bool isAfterQuote = false; // Unquoted spaces right after quoted text separate it from what follows
    std::size_t unquotedSpaces = 0; // How many unquoted spaces end the affix; dropped if quoted text follows
    std::size_t index = start + 1;

    while (true)
    {
        if (index >= pattern.size())
            throw MakePatternError("Unclosed '{'", start, pattern);

        const char character = pattern[index];
        if (character == '}')
            break;

        if (character == '\'')
        {
            affix->resize(affix->size() - unquotedSpaces);
            unquotedSpaces = 0;
            const std::size_t quoteStart = index++;

            while (true)
            {
                if (index >= pattern.size())
                    throw MakePatternError("Unclosed quote", quoteStart, pattern);

                if (pattern[index] == '\'')
                {
                    if (index + 1 < pattern.size() && pattern[index + 1] == '\'')
                    {
                        *affix += '\'';
                        index += 2;
                        continue;
                    }

                    ++index;
                    break;
                }

                *affix += pattern[index++];
            }

            isAfterQuote = true;
        }
        else if (character == ' ' && isAfterQuote)
        {
            ++index;
        }
        else if (IsASCIILetter(character))
        {
            isAfterQuote = false;
            unquotedSpaces = 0;

            if (hasName)
                throw MakePatternError("Letters around a placeholder name must be in quotes", index, pattern);

            const std::size_t nameStart = index;

            while (index < pattern.size() && IsASCIILetter(pattern[index]))
                ++index;

            const auto name = pattern.substr(nameStart, index - nameStart);
            const auto* found = std::find_if(std::begin(Names), std::end(Names), [name](const TName& entry) {
                    return entry.Name == name;
                });
            if (found == std::end(Names))
                throw MakePatternError(std::format("Unknown placeholder name '{}'", name), nameStart, pattern);

            part.Field = found->Field;
            hasName = true;
            affix = &part.Suffix;

            if (index < pattern.size() && pattern[index] == ':')
            {
                const std::size_t widthStart = ++index;
                std::size_t width = 0;

                while (index < pattern.size() && IsASCIIDigit(pattern[index]) && width <= MaxPatternWidth)
                    width = width * 10 + static_cast<std::size_t>(pattern[index++] - '0');

                if (index == widthStart || width == 0 || width > MaxPatternWidth)
                {
                    const auto problem = std::format("A width must be 1 to {}", MaxPatternWidth);
                    throw MakePatternError(problem, widthStart, pattern);
                }

                part.Width = width;
            }
        }
        else if (IsASCIIDigit(character) || character == ':' || character == '{')
        {
            throw MakePatternError(std::format("'{}' around a placeholder name must be in quotes", character), index,
                pattern);
        }
        else
        {
            isAfterQuote = false;
            unquotedSpaces = character == ' ' ? unquotedSpaces + 1 : 0;
            *affix += character;
            ++index;
        }
    }

    if (!hasName)
        throw MakePatternError("Missing placeholder name", start, pattern);

    if (part.Field == TField::WorkingSet || part.Field == TField::PeakWorkingSet)
        m_UsesMemoryUsage = true;

    m_Parts.push_back(std::move(part));

    return index + 1;
}

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

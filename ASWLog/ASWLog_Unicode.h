/* **************************************************************************
ASWLog_Unicode.h
Author: Anthony S. West - ASW Software

A logging library.

Source for logging wide (UTF-16 and UTF-32) text: conversions to and from UTF-8, and the text wrappers the logging
methods and the *Fmt arguments take.

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

#ifndef ASWLog_UnicodeH
#define ASWLog_UnicodeH
//---------------------------------------------------------------------------
#include <format>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/*
    UTF8ToUTF16

    'text' (UTF-8) as UTF-16. Each invalid UTF-8 sequence becomes U+FFFD. Throws std::bad_alloc if out of memory.
*/
[[nodiscard]] std::u16string UTF8ToUTF16(std::string_view text);

/*
    UTF8ToWide

    'text' (UTF-8) as a wide string: UTF-16 where wchar_t has 16 bits (Windows, e.g. for the Windows API or a VCL
    control), UTF-32 where it has 32 (Linux). Each invalid UTF-8 sequence becomes U+FFFD. Throws std::bad_alloc if out
    of memory.
*/
[[nodiscard]] std::wstring UTF8ToWide(std::string_view text);

/*
    WideToUTF8

    'text' as UTF-8: a wide string (UTF-16 where wchar_t has 16 bits, UTF-32 where it has 32), or UTF-16. Each
    unpaired surrogate, or value that isn't a character (above U+10FFFF), becomes U+FFFD. Throws std::bad_alloc if out
    of memory.
*/
[[nodiscard]] std::string WideToUTF8(std::wstring_view text);
[[nodiscard]] std::string WideToUTF8(std::u16string_view text);

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWUTF8Text
//
// A view of UTF-8 text, made by UTF8(), that a *Fmt call with a wide format string formats as wide text (converted
// only if the entry is formatted), e.g. log.LogInfoFmt(L"{} from {}", name, ASWLog::UTF8(host)). A narrow *Fmt call
// formats it as it is.
/////////////////////////////////////////////////////////////////////////////
class TASWUTF8Text
{
private:
    std::string_view m_Text;

public:
    constexpr explicit TASWUTF8Text(std::string_view text) noexcept
        : m_Text(text)
    {
    }

    [[nodiscard]] constexpr std::string_view GetText() const noexcept
    {
        return m_Text;
    }
};

// Wraps UTF-8 text for a *Fmt call with a wide format string (see TASWUTF8Text). Holds a view of 'text', so the text
// must outlive the call.
[[nodiscard]] constexpr TASWUTF8Text UTF8(std::string_view text) noexcept
{
    return TASWUTF8Text(text);
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWWideText
//
// A view of wide text: wchar_t (UTF-16 on Windows, UTF-32 on Linux) or char16_t (UTF-16). The logging methods take it
// as a message, e.g. log.LogInfo(L"Opened") or, with C++Builder, log.LogInfo(caption.c_str()), and convert it to UTF-8
// only if the entry is written. Wide() makes one for a *Fmt argument, which is converted only if the entry is
// formatted. Made implicitly from a null-terminated string (null counts as empty), a string view or a string; it holds
// a view, so the text must outlive the call.
/////////////////////////////////////////////////////////////////////////////
class TASWWideText
{
private:
    // At most one of them holds the text
    std::wstring_view m_Wide;
    std::u16string_view m_UTF16;

public:
    constexpr TASWWideText(const wchar_t* text) noexcept
        : m_Wide(text != nullptr ? std::wstring_view(text) : std::wstring_view())
    {
    }

    constexpr TASWWideText(const char16_t* text) noexcept
        : m_UTF16(text != nullptr ? std::u16string_view(text) : std::u16string_view())
    {
    }

    constexpr TASWWideText(std::wstring_view text) noexcept
        : m_Wide(text)
    {
    }

    constexpr TASWWideText(std::u16string_view text) noexcept
        : m_UTF16(text)
    {
    }

    // A string converts to a view through a conversion of its own, and an implicit conversion can't chain two
    TASWWideText(const std::wstring& text) noexcept
        : m_Wide(text)
    {
    }

    TASWWideText(const std::u16string& text) noexcept
        : m_UTF16(text)
    {
    }

    // The text as a wide string (converted from UTF-16 if it is char16_t text where wchar_t has 32 bits). Throws
    // std::bad_alloc if out of memory.
    [[nodiscard]] std::wstring ToWide() const;
    // The text as UTF-8 (see WideToUTF8()). Throws std::bad_alloc if out of memory.
    [[nodiscard]] std::string ToUTF8() const;
};

// Wraps wide text for a *Fmt call with a narrow format string, e.g. log.LogInfoFmt("Opened {}", ASWLog::Wide(name)):
// converted to UTF-8 only if the entry is formatted (see TASWWideText)
[[nodiscard]] constexpr TASWWideText Wide(TASWWideText text) noexcept
{
    return text;
}

} // namespace ASWLog

//---------------------------------------------------------------------------

// The formatters of the text wrappers, each the string view formatter of its character type, so a width or precision
// applies too, e.g. "{:>20}" (counted in code units, e.g. UTF-8 bytes, not characters)
template<>
struct std::formatter<ASWLog::TASWUTF8Text, char> : std::formatter<std::string_view, char>
{
    template<typename TContext>
    auto format(const ASWLog::TASWUTF8Text& text, TContext& context) const
    {
        return std::formatter<std::string_view, char>::format(text.GetText(), context);
    }
};

template<>
struct std::formatter<ASWLog::TASWUTF8Text, wchar_t> : std::formatter<std::wstring_view, wchar_t>
{
    template<typename TContext>
    auto format(const ASWLog::TASWUTF8Text& text, TContext& context) const
    {
        const std::wstring wide = ASWLog::UTF8ToWide(text.GetText());

        return std::formatter<std::wstring_view, wchar_t>::format(wide, context);
    }
};

template<>
struct std::formatter<ASWLog::TASWWideText, char> : std::formatter<std::string_view, char>
{
    template<typename TContext>
    auto format(const ASWLog::TASWWideText& text, TContext& context) const
    {
        const std::string utf8 = text.ToUTF8();

        return std::formatter<std::string_view, char>::format(utf8, context);
    }
};

template<>
struct std::formatter<ASWLog::TASWWideText, wchar_t> : std::formatter<std::wstring_view, wchar_t>
{
    template<typename TContext>
    auto format(const ASWLog::TASWWideText& text, TContext& context) const
    {
        const std::wstring wide = text.ToWide();

        return std::formatter<std::wstring_view, wchar_t>::format(wide, context);
    }
};

//---------------------------------------------------------------------------
#endif // ASWLog_UnicodeH

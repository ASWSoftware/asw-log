/* **************************************************************************
ASWLog_Unicode.cpp
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
#include "ASWLog_Unicode.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstddef>
#include <cstdint>
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

constexpr char32_t ReplacementCharacter = 0xFFFD;

//---------------------------------------------------------------------------

void AppendUTF8(std::string& output, char32_t character);
template<typename TChar>
void AppendUTF16(std::basic_string<TChar>& output, char32_t character);
template<typename TChar>
char32_t DecodeUTF16(std::basic_string_view<TChar> text, std::size_t& index) noexcept;
char32_t DecodeUTF8(std::string_view text, std::size_t& index) noexcept;
template<typename TChar>
std::basic_string<TChar> ToUTF16(std::string_view text);
template<typename TChar>
std::string ToUTF8(std::basic_string_view<TChar> text);

//---------------------------------------------------------------------------

/*
  AppendUTF8

  Appends 'character' (a Unicode scalar value) to 'output' as UTF-8
*/
void AppendUTF8(std::string& output, char32_t character)
{
    if (character < 0x80)
    {
        output += static_cast<char>(character);
    }
    else if (character < 0x800)
    {
        output += static_cast<char>(0xC0 | (character >> 6));
        output += static_cast<char>(0x80 | (character & 0x3F));
    }
    else if (character < 0x10000)
    {
        output += static_cast<char>(0xE0 | (character >> 12));
        output += static_cast<char>(0x80 | ((character >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (character & 0x3F));
    }
    else
    {
        output += static_cast<char>(0xF0 | (character >> 18));
        output += static_cast<char>(0x80 | ((character >> 12) & 0x3F));
        output += static_cast<char>(0x80 | ((character >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (character & 0x3F));
    }
}

/*
  AppendUTF16

  Appends 'character' (a Unicode scalar value) to 'output' as UTF-16: one code unit, or a surrogate pair above U+FFFF
*/
template<typename TChar>
void AppendUTF16(std::basic_string<TChar>& output, char32_t character)
{
    if (character < 0x10000)
    {
        output += static_cast<TChar>(character);
        return;
    }

    character -= 0x10000;
    output += static_cast<TChar>(0xD800 + (character >> 10));
    output += static_cast<TChar>(0xDC00 + (character & 0x3FF));
}

/*
  DecodeUTF16

  The character at 'index' in 'text' (UTF-16 code units of type TChar: char16_t, or wchar_t where it has 16 bits),
  moving 'index' past it: a code unit, or a surrogate pair; U+FFFD for an unpaired surrogate, which is skipped
*/
template<typename TChar>
char32_t DecodeUTF16(std::basic_string_view<TChar> text, std::size_t& index) noexcept
{
    const char32_t unit = static_cast<std::uint16_t>(text[index++]);
    if (unit < 0xD800 || unit > 0xDFFF)
        return unit;

    if (unit <= 0xDBFF && index < text.size())
    {
        const char32_t next = static_cast<std::uint16_t>(text[index]);
        if (next >= 0xDC00 && next <= 0xDFFF)
        {
            ++index;
            return 0x10000 + ((unit - 0xD800) << 10) + (next - 0xDC00);
        }
    }

    return ReplacementCharacter;
}

/*
  DecodeUTF8

  The character at 'index' in 'text' (UTF-8), moving 'index' past it. An invalid sequence is U+FFFD (see the Unicode
  standard's table of well-formed byte sequences): its longest start that could begin a valid sequence, at least one
  byte, is skipped, so the byte that broke it starts the next character.
*/
char32_t DecodeUTF8(std::string_view text, std::size_t& index) noexcept
{
    const auto lead = static_cast<unsigned char>(text[index++]);
    if (lead < 0x80)
        return lead;

    std::size_t length = 0;
    char32_t character = 0;
    unsigned char low = 0x80; // The range of the second byte, which some lead bytes narrow
    unsigned char high = 0xBF;

    if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
        character = lead & 0x1F;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
        character = lead & 0x0F;
        low = lead == 0xE0 ? 0xA0 : 0x80; // No overlong forms
        high = lead == 0xED ? 0x9F : 0xBF; // No surrogates
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
        character = lead & 0x07;
        low = lead == 0xF0 ? 0x90 : 0x80; // No overlong forms
        high = lead == 0xF4 ? 0x8F : 0xBF; // Nothing above U+10FFFF
    }
    else
    {
        return ReplacementCharacter;
    }

    for (std::size_t count = 1; count < length; ++count)
    {
        if (index >= text.size())
            return ReplacementCharacter;

        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte < low || byte > high)
            return ReplacementCharacter;

        character = (character << 6) | (byte & 0x3F);
        low = 0x80;
        high = 0xBF;
        ++index;
    }

    return character;
}

/*
  ToUTF16

  'text' (UTF-8) as UTF-16 code units of type TChar (char16_t, or wchar_t where it has 16 bits)
*/
template<typename TChar>
std::basic_string<TChar> ToUTF16(std::string_view text)
{
    std::basic_string<TChar> output;
    output.reserve(text.size());

    for (std::size_t index = 0; index < text.size();)
        AppendUTF16(output, DecodeUTF8(text, index));

    return output;
}

/*
  ToUTF8

  'text' (UTF-16 code units of type TChar) as UTF-8
*/
template<typename TChar>
std::string ToUTF8(std::basic_string_view<TChar> text)
{
    std::string output;
    output.reserve(text.size());

    for (std::size_t index = 0; index < text.size();)
        AppendUTF8(output, DecodeUTF16(text, index));

    return output;
}

} // namespace

//---------------------------------------------------------------------------
std::u16string UTF8ToUTF16(std::string_view text)
{
    return ToUTF16<char16_t>(text);
}

//---------------------------------------------------------------------------
std::wstring UTF8ToWide(std::string_view text)
{
    if constexpr (sizeof(wchar_t) == sizeof(char16_t))
    {
        return ToUTF16<wchar_t>(text);
    }
    else
    {
        std::wstring output;
        output.reserve(text.size());

        for (std::size_t index = 0; index < text.size();)
            output += static_cast<wchar_t>(DecodeUTF8(text, index));

        return output;
    }
}

//---------------------------------------------------------------------------
std::string WideToUTF8(std::wstring_view text)
{
    if constexpr (sizeof(wchar_t) == sizeof(char16_t))
    {
        return ToUTF8(text);
    }
    else
    {
        std::string output;
        output.reserve(text.size());

        for (const wchar_t unit : text)
        {
            const auto character = static_cast<char32_t>(unit);
            const bool isCharacter = character <= 0x10FFFF && (character < 0xD800 || character > 0xDFFF);
            AppendUTF8(output, isCharacter ? character : ReplacementCharacter);
        }

        return output;
    }
}

//---------------------------------------------------------------------------
std::string WideToUTF8(std::u16string_view text)
{
    return ToUTF8(text);
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWWideText
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::wstring TASWWideText::ToWide() const
{
    if (m_UTF16.empty())
        return std::wstring(m_Wide);

    if constexpr (sizeof(wchar_t) == sizeof(char16_t))
    {
        return std::wstring(m_UTF16.begin(), m_UTF16.end());
    }
    else
    {
        std::wstring output;
        output.reserve(m_UTF16.size());

        for (std::size_t index = 0; index < m_UTF16.size();)
            output += static_cast<wchar_t>(DecodeUTF16(m_UTF16, index));

        return output;
    }
}

//---------------------------------------------------------------------------
std::string TASWWideText::ToUTF8() const
{
    return m_UTF16.empty() ? WideToUTF8(m_Wide) : WideToUTF8(m_UTF16);
}

//---------------------------------------------------------------------------

} // namespace ASWLog

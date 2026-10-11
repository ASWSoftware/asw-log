/* **************************************************************************
UT_Helper_JSON.cpp
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
#include "UT_Helper_JSON.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstddef>
#include <cstdint>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// Deeper nesting is rejected, so a malformed line can't exhaust the stack
constexpr int MaxDepth = 32;

//---------------------------------------------------------------------------

// A strict reader that only checks whether the text is well-formed
class TJSONChecker
{
private:
    std::string_view m_Text;
    std::size_t m_Index = 0;
    int m_Depth = 0;

private:
    bool IsAtEnd() const
    {
        return m_Index >= m_Text.size();
    }

    unsigned char Peek() const
    {
        return IsAtEnd() ? 0 : static_cast<unsigned char>(m_Text[m_Index]);
    }

    bool Take(char expected)
    {
        if (IsAtEnd() || m_Text[m_Index] != expected)
            return false;

        ++m_Index;
        return true;
    }

    void SkipWhitespace()
    {
        while (!IsAtEnd() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r'))
            ++m_Index;
    }

    bool ReadArray()
    {
        ++m_Index; // '['
        SkipWhitespace();
        if (Take(']'))
            return true;

        while (true)
        {
            if (!ReadValue())
                return false;

            SkipWhitespace();
            if (Take(']'))
                return true;

            if (!Take(','))
                return false;
        }
    }

    bool ReadDigits()
    {
        const auto start = m_Index;
        while (Peek() >= '0' && Peek() <= '9')
            ++m_Index;

        return m_Index > start;
    }

    bool ReadLiteral(std::string_view literal)
    {
        if (m_Text.substr(m_Index, literal.size()) != literal)
            return false;

        m_Index += literal.size();
        return true;
    }

    bool ReadNumber()
    {
        Take('-');
        if (Take('0'))
        {
            if (Peek() >= '0' && Peek() <= '9')
                return false; // No leading zeros
        }
        else if (!ReadDigits())
        {
            return false;
        }

        if (Take('.') && !ReadDigits())
            return false;

        if (Take('e') || Take('E'))
        {
            if (!Take('+'))
                Take('-');

            if (!ReadDigits())
                return false;
        }

        return true;
    }

    bool ReadObject()
    {
        ++m_Index; // '{'
        SkipWhitespace();
        if (Take('}'))
            return true;

        while (true)
        {
            SkipWhitespace();
            if (Peek() != '"' || !ReadString())
                return false;

            SkipWhitespace();
            if (!Take(':') || !ReadValue())
                return false;

            SkipWhitespace();
            if (Take('}'))
                return true;

            if (!Take(','))
                return false;
        }
    }

    bool ReadString()
    {
        ++m_Index; // '"'

        while (!IsAtEnd())
        {
            const unsigned char character = Peek();
            if (character == '"')
            {
                ++m_Index;
                return true;
            }

            if (character < 0x20)
                return false;

            if (character == '\\')
            {
                ++m_Index;
                const char escaped = static_cast<char>(Peek());
                ++m_Index;

                if (escaped == 'u')
                {
                    for (int digit = 0; digit < 4; ++digit, ++m_Index)
                    {
                        const unsigned char hex = Peek();
                        if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F')))
                            return false;
                    }
                }
                else if (std::string_view("\"\\/bfnrt").find(escaped) == std::string_view::npos)
                {
                    return false;
                }
            }
            else if (character >= 0x80)
            {
                if (!ReadUTF8Character())
                    return false;
            }
            else
            {
                ++m_Index;
            }
        }

        return false; // Unclosed
    }

    // Decodes one multi-byte UTF-8 character and checks it: the right number of continuation bytes, the shortest form,
    // no surrogate and nothing above U+10FFFF
    bool ReadUTF8Character()
    {
        const unsigned char lead = Peek();
        int length = 0;
        std::uint32_t codePoint = 0;

        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            codePoint = lead & 0x1F;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            codePoint = lead & 0x0F;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            codePoint = lead & 0x07;
        }
        else
        {
            return false;
        }

        ++m_Index;

        for (int count = 1; count < length; ++count, ++m_Index)
        {
            if ((Peek() & 0xC0) != 0x80)
                return false;

            codePoint = (codePoint << 6) | (Peek() & 0x3F);
        }

        constexpr std::uint32_t Smallest[] = { 0, 0, 0x80, 0x800, 0x10000 };
        return codePoint >= Smallest[length] && codePoint <= 0x10FFFF && (codePoint < 0xD800 || codePoint > 0xDFFF);
    }

    bool ReadValue()
    {
        SkipWhitespace();
        if (++m_Depth > MaxDepth)
            return false;

        bool isValid = false;
        switch (Peek())
        {
            case '{':
                isValid = ReadObject();
                break;

            case '[':
                isValid = ReadArray();
                break;

            case '"':
                isValid = ReadString();
                break;

            case 't':
                isValid = ReadLiteral("true");
                break;

            case 'f':
                isValid = ReadLiteral("false");
                break;

            case 'n':
                isValid = ReadLiteral("null");
                break;

            default:
                isValid = ReadNumber();
                break;
        }

        --m_Depth;
        return isValid;
    }

public:
    explicit TJSONChecker(std::string_view text)
        : m_Text(text)
    {
    }

    bool IsObjectLine()
    {
        return Peek() == '{' && ReadValue() && IsAtEnd();
    }
};

} // namespace

//---------------------------------------------------------------------------
bool IsJSONObjectLine(std::string_view text)
{
    return TJSONChecker(text).IsObjectLine();
}

} // ASWUnitTests

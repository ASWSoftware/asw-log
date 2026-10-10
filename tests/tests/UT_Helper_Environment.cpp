/* **************************************************************************
UT_Helper_Environment.cpp
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
#include "UT_Helper_Environment.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdlib>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

#if defined(_WIN32)
namespace
{

std::string ToUTF8(const std::wstring& text);
std::wstring ToWide(const std::string& text);

//---------------------------------------------------------------------------

// 'text' (UTF-16) as UTF-8
std::string ToUTF8(const std::wstring& text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);

    return utf8;
}

// 'text' (UTF-8) as UTF-16, converted here rather than with the library's helper the tests check
std::wstring ToWide(const std::string& text)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);

    return wide;
}

} // namespace
#endif

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TScopedEnvironmentVariable
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TScopedEnvironmentVariable::TScopedEnvironmentVariable(std::string name, std::optional<std::string> value)
    : m_Name(std::move(name)),
      m_PreviousValue(Get(m_Name))
{
    Set(m_Name, value);
}
//---------------------------------------------------------------------------
TScopedEnvironmentVariable::~TScopedEnvironmentVariable()
{
    Set(m_Name, m_PreviousValue);
}
//---------------------------------------------------------------------------
std::optional<std::string> TScopedEnvironmentVariable::Get(const std::string& name)
{
#if defined(_WIN32)
    // The Windows API rather than the C runtime, since that's what the logger reads (and MinGW's runtime has no
    // _dupenv_s)
    const auto wideName = ToWide(name);
    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0)
        return std::nullopt;

    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(wideName.c_str(), value.data(), size));

    return ToUTF8(value);
#else
    const char* value = std::getenv(name.c_str());
    if (value == nullptr)
        return std::nullopt;

    return std::string(value);
#endif
}
//---------------------------------------------------------------------------
void TScopedEnvironmentVariable::Set(const std::string& name, const std::optional<std::string>& value)
{
#if defined(_WIN32)
    // nullptr removes the variable
    SetEnvironmentVariableW(ToWide(name).c_str(), value ? ToWide(*value).c_str() : nullptr);
#else
    if (value)
        setenv(name.c_str(), value->c_str(), 1);
    else
        unsetenv(name.c_str());
#endif
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

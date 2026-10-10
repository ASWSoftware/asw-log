/* **************************************************************************
UT_Helper_Environment.h
Author: Anthony S. West - ASW Software

Environment variable helpers shared by the unit tests.

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

#ifndef UT_Helper_EnvironmentH
#define UT_Helper_EnvironmentH
//---------------------------------------------------------------------------
#include <optional>
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TScopedEnvironmentVariable
//
// While alive, sets an environment variable (or removes it, given std::nullopt), then restores its previous value.
// Names and values are UTF-8 (set with the wide Windows API on Windows, which is what the logger reads there).
///////////////////////////////////////////////////////////////////////////
class TScopedEnvironmentVariable
{
private:
    std::string m_Name;
    std::optional<std::string> m_PreviousValue;

private:
    static std::optional<std::string> Get(const std::string& name);
    static void Set(const std::string& name, const std::optional<std::string>& value);

public:
    TScopedEnvironmentVariable(std::string name, std::optional<std::string> value);
    ~TScopedEnvironmentVariable();

    TScopedEnvironmentVariable(const TScopedEnvironmentVariable&) = delete;
    TScopedEnvironmentVariable& operator=(const TScopedEnvironmentVariable&) = delete;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_EnvironmentH

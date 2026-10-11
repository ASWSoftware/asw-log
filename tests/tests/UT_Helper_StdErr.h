/* **************************************************************************
UT_Helper_StdErr.h
Author: Anthony S. West - ASW Software

stderr helpers shared by the unit tests.

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

#ifndef UT_Helper_StdErrH
#define UT_Helper_StdErrH
//---------------------------------------------------------------------------
#include <filesystem>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TStdErrRedirect
//
// While alive, sends what is written to stderr to 'file' instead, by pointing stderr's file descriptor at it. Inactive
// if stderr has no file descriptor (e.g. in a GUI application without a console).
///////////////////////////////////////////////////////////////////////////
class TStdErrRedirect
{
private:
    int m_SavedDescriptor = -1;

public:
    explicit TStdErrRedirect(const std::filesystem::path& file);
    ~TStdErrRedirect();

    TStdErrRedirect(const TStdErrRedirect&) = delete;
    TStdErrRedirect& operator=(const TStdErrRedirect&) = delete;

    bool IsActive() const noexcept;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_StdErrH

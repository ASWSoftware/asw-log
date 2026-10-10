/* **************************************************************************
UT_Helper_StdErr.cpp
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
#include "UT_Helper_StdErr.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdio>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TStdErrRedirect
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TStdErrRedirect::TStdErrRedirect(const std::filesystem::path& file)
{
    std::fflush(stderr);
#if defined(_WIN32)
    std::FILE* target = _wfsopen(file.c_str(), L"wb", _SH_DENYNO);
    if (target == nullptr)
        return;

    m_SavedDescriptor = _dup(_fileno(stderr));
    if (m_SavedDescriptor >= 0 && _dup2(_fileno(target), _fileno(stderr)) != 0)
    {
        _close(m_SavedDescriptor);
        m_SavedDescriptor = -1;
    }
#else
    std::FILE* target = std::fopen(file.c_str(), "wb");
    if (target == nullptr)
        return;

    m_SavedDescriptor = dup(fileno(stderr));
    if (m_SavedDescriptor >= 0 && dup2(fileno(target), fileno(stderr)) < 0)
    {
        close(m_SavedDescriptor);
        m_SavedDescriptor = -1;
    }
#endif
    std::fclose(target);
}
//---------------------------------------------------------------------------
TStdErrRedirect::~TStdErrRedirect()
{
    if (m_SavedDescriptor < 0)
        return;

    std::fflush(stderr);
#if defined(_WIN32)
    _dup2(m_SavedDescriptor, _fileno(stderr));
    _close(m_SavedDescriptor);
#else
    dup2(m_SavedDescriptor, fileno(stderr));
    close(m_SavedDescriptor);
#endif
}
//---------------------------------------------------------------------------
bool TStdErrRedirect::IsActive() const noexcept
{
    return m_SavedDescriptor >= 0;
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

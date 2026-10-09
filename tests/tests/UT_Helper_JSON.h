/* **************************************************************************
UT_Helper_JSON.h
Author: Anthony S. West - ASW Software

JSON helpers shared by the unit tests.

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

#ifndef UT_Helper_JSONH
#define UT_Helper_JSONH
//---------------------------------------------------------------------------
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

// True if 'text' is exactly one JSON object (RFC 8259), with no whitespace around it, whose strings are valid UTF-8
// with no raw control characters: what a JSON Lines line holds without its line ending. Written independently of
// ASWLog's JSON code, so the tests don't check that code against itself.
bool IsJSONObjectLine(std::string_view text);

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_JSONH

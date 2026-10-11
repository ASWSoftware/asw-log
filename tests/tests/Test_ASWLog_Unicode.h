/* **************************************************************************
Test_ASWLog_Unicode.h
Author: Anthony S. West - ASW Software

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

#ifndef Test_ASWLog_UnicodeH
#define Test_ASWLog_UnicodeH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Unicode
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_Unicode : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Formatters_ConvertTheWrappedText();
    void Test_LogFmt_NarrowFormatTakesWideArguments();
    void Test_LogFmt_WideFormatSkipsFilteredEntries();
    void Test_LogFmt_WideFormatWritesUTF8();
    void Test_LogWide_ConvertsOnlyEntriesThatAreUsed();
    void Test_LogWide_EachMethodWritesUTF8();
    void Test_LogWide_WritesThroughARealLogger();
    void Test_RoundTrip_KeepsEveryCharacter();
    void Test_UTF8ToUTF16_ReplacesInvalidSequences();
    void Test_UTF8ToWide_FollowsTheWidthOfWchar();
    void Test_WideText_TakesEachKindOfText();
    void Test_WideToUTF8_ReplacesWhatIsNotACharacter();

public:
    TTest_ASWLog_Unicode();
    ~TTest_ASWLog_Unicode() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_UnicodeH

/* **************************************************************************
Test_ASWLog_Formatter.h
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

#ifndef Test_ASWLog_FormatterH
#define Test_ASWLog_FormatterH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Formatter
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_Formatter : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Format_MatchesFormatLine();
    void Test_FormatLine_AllFieldsInOrder();
    void Test_FormatLine_CategoryOnlyWhenSetAndShown();
    void Test_FormatLine_LargestIdsAndEachLevel();
    void Test_FormatLine_MemoryFields();
    void Test_FormatLine_NoFields();
    void Test_FormatLine_TimestampFollowsZoneAndPrecision();
    void Test_Formatter_ReceivesRecordFromLoggingThread();
    void Test_Formatter_SharedByTwoLoggers();
    void Test_JSONFormatter_DefaultFields();
    void Test_JSONFormatter_EachShowOption();
    void Test_JSONFormatter_EscapesTheTexts();
    void Test_JSONFormatter_FileLoggerWritesJSONLines();
    void Test_JSONFormatter_RawEntries();
    void Test_JSONFormatter_TimeFollowsZoneAndPrecision();
    void Test_PatternFormatter_AffixesOnlyAroundAValue();
    void Test_PatternFormatter_BracesAndPlainText();
    void Test_PatternFormatter_DefaultLayoutMatchesTextFormatter();
    void Test_PatternFormatter_EachPlaceholder();
    void Test_PatternFormatter_FileLoggerWritesItsLines();
    void Test_PatternFormatter_InvalidPatternThrows();
    void Test_PatternFormatter_MemoryFields();
    void Test_PatternFormatter_WidthPadsShortValues();

public:
    TTest_ASWLog_Formatter();
    ~TTest_ASWLog_Formatter() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_FormatterH

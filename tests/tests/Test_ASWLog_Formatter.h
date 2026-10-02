/* **************************************************************************
Test_ASWLog_Formatter.h
Author: Anthony S. West - ASW Software

Copyright 2026 Anthony S. West

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
    void Test_FormatLine_MemoryFields();
    void Test_FormatLine_NoFields();
    void Test_Formatter_ReceivesRecordFromLoggingThread();
    void Test_Formatter_SharedByTwoLoggers();

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

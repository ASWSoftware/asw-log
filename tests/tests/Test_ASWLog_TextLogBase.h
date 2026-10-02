/* **************************************************************************
Test_ASWLog_TextLogBase.h
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

#ifndef Test_ASWLog_TextLogBaseH
#define Test_ASWLog_TextLogBaseH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_TextLogBase
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_TextLogBase : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Finalize_WritesShutdownLineFromDestructor();
    void Test_Formatter_FormatsEveryFileLine();
    void Test_Initialize_WritesStartupLinesThenCallsAfterEntry();
    void Test_Log_AtLevelOffIsNeverWritten();
    void Test_Log_DroppedWhenNotReadyOrNotPrepared();
    void Test_Log_FormatsFiltersAndCallsAfterEntry();
    void Test_Log_MinimumLevelOffAllowsOnlyForcedEntries();
    void Test_Log_ThrowingWriteDoesNotEscape();
    void Test_LogRaw_WritesMessageAsIs();
    void Test_SetEnabled_FalseWritesNothing();

public:
    TTest_ASWLog_TextLogBase();
    ~TTest_ASWLog_TextLogBase() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_TextLogBaseH

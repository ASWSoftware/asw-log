/* **************************************************************************
Test_ASWLog_CategoryLog.h
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

#ifndef Test_ASWLog_CategoryLogH
#define Test_ASWLog_CategoryLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_CategoryLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_CategoryLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Async_KeepsItsOwnCopyOfTheName();
    void Test_Backtrace_KeepsEntriesBelowTheCategoryLevel();
    void Test_Backtrace_KeepsItsOwnCopyOfTheName();
    void Test_FileLog_WritesTheCategoryAfterTheLevel();
    void Test_GetMinimumLevel_FollowsTheWrappedLoggerUntilSet();
    void Test_Lifecycle_LeavesTheWrappedLoggerAlone();
    void Test_MultiLog_CategoryLevelReplacesOnlyTheCompositeLevel();
    void Test_Nesting_JoinsNamesAndInheritsLevels();
    void Test_SetEnabled_SilencesOnlyTheCategory();
    void Test_SetMinimumLevel_OffSilencesAllButForcedEntries();
    void Test_SetMinimumLevel_ReplacesTheWrappedLoggersLevel();
    void Test_ShouldLog_MatchesWhatIsWritten();
    void Test_Write_KeepsACategoryAlreadySet();
    void Test_Write_StampsTheNameAndOwnLevel();

public:
    TTest_ASWLog_CategoryLog();
    ~TTest_ASWLog_CategoryLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_CategoryLogH

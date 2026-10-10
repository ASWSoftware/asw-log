/* **************************************************************************
Test_ASWLog_Fields.h
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

#ifndef Test_ASWLog_FieldsH
#define Test_ASWLog_FieldsH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Fields
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_Fields : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Async_OnLogEntryGetsTheFieldsAfterTheScopeEnds();
    void Test_Backtrace_KeepsTheFieldsAfterTheScopeEnds();
    void Test_Capture_TakesTheScopesToAnotherThread();
    void Test_ForEachLogField_EachKeyOnceInnermostWins();
    void Test_ForEachLogField_RepeatedKeysInTheEntrysOwnFields();
    void Test_Log_CategoryAndMultiLogPassTheFieldsOn();
    void Test_Log_EachOverloadPassesItsFields();
    void Test_Log_ScopeFieldsComeBeforeTheEntrysOwn();
    void Test_Scope_CopiesTemporaryTexts();
    void Test_Scope_IsPerThread();
    void Test_Scope_NestsAndRestoresTheCurrentScope();
    void Test_Value_KindsFromEachType();
    void Test_Value_RejectsCharactersEnumsAndPointers();

public:
    TTest_ASWLog_Fields();
    ~TTest_ASWLog_Fields() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_FieldsH

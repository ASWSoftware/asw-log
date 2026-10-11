/* **************************************************************************
Test_ASWLog_DebuggerLog.h
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

#ifndef Test_ASWLog_DebuggerLogH
#define Test_ASWLog_DebuggerLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_DebuggerLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_DebuggerLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_GetInstance_ReturnsTheSameLogger();
    void Test_ShouldLog_FollowsThePlatform();
    void Test_Windows_SendsEachLineToTheDebugOutput();
    void Test_Windows_WritesTheCrashLineDirectly();

public:
    TTest_ASWLog_DebuggerLog();
    ~TTest_ASWLog_DebuggerLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_DebuggerLogH

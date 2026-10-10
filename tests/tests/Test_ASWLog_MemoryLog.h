/* **************************************************************************
Test_ASWLog_MemoryLog.h
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

#ifndef Test_ASWLog_MemoryLogH
#define Test_ASWLog_MemoryLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_MemoryLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_MemoryLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Async_KeepsTheQueuedLines();
    void Test_Clear_ForgetsTheLinesButNotTheSequence();
    void Test_Close_KeepsTheLines();
    void Test_GetLinesSince_ReturnsNewLinesAndCountsMissedOnes();
    void Test_KeepsLinesWithoutTheirEnding();
    void Test_MaxBytes_CutsALineThatAloneIsTooLong();
    void Test_MaxBytes_DropsTheOldestLines();
    void Test_MaxLines_KeepsTheNewest();
    void Test_Reconfigure_AppliesTheNewLimits();
    void Test_Threads_ReaderGetsEveryLineOnce();

public:
    TTest_ASWLog_MemoryLog();
    ~TTest_ASWLog_MemoryLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_MemoryLogH

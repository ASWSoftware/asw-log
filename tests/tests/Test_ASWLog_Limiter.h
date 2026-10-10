/* **************************************************************************
Test_ASWLog_Limiter.h
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

#ifndef Test_ASWLog_LimiterH
#define Test_ASWLog_LimiterH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Limiter
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_Limiter : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_EveryInterval_AllowsOneCallPerInterval();
    void Test_EveryInterval_HandlesTheLongestInterval();
    void Test_EveryInterval_ZeroAllowsEveryCall();
    void Test_EveryN_AllowsEveryNthCall();
    void Test_Macros_EveryIntervalAddsTheSuppressedField();
    void Test_Macros_EveryNCountsOnlyEntriesThatWouldBeWritten();
    void Test_Macros_KeepTheCallersSourceLocation();
    void Test_Macros_OnceLogsTheFirstEntryThatWouldBeWritten();
    void Test_Once_AllowsOnlyTheFirstCall();
    void Test_Threads_AllowExactlyTheLimitedCalls();

public:
    TTest_ASWLog_Limiter();
    ~TTest_ASWLog_Limiter() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_LimiterH

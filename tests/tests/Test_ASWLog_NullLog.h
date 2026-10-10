/* **************************************************************************
Test_ASWLog_NullLog.h
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

#ifndef Test_ASWLog_NullLogH
#define Test_ASWLog_NullLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_NullLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_NullLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_Fmt_NeverFormats();
    void Test_GetConfig_IsSafeWhileReconfiguring();
    void Test_GetInstance_ReturnsTheSameLogger();
    void Test_Lifecycle_SucceedsAndKeepsTheConfig();
    void Test_ShouldLog_IsAlwaysFalse();
    void Test_Wrapped_WritesNothing();

public:
    TTest_ASWLog_NullLog();
    ~TTest_ASWLog_NullLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_NullLogH

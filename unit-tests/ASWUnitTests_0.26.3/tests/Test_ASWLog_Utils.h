/* **************************************************************************
Test_ASWLog_Utils.h
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

#ifndef Test_ASWLog_UtilsH
#define Test_ASWLog_UtilsH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Utils
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_Utils : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_GenerateLogFileName_ContainsExpectedFields();
    void Test_GetOSInfoString_ContainsEdition();
    void Test_MatchesWildcard_Patterns();
    void Test_Time_ToDateString();
    void Test_Time_ToISO8601String();

public:
    TTest_ASWLog_Utils();
    ~TTest_ASWLog_Utils() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_UtilsH

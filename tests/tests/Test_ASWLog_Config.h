/* **************************************************************************
Test_ASWLog_Config.h
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

#ifndef Test_ASWLog_ConfigH
#define Test_ASWLog_ConfigH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Config
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_Config : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_ApplyEnvironment_AppliesPatternAfterFormat();
    void Test_ApplyEnvironment_AppliesTheCategoryLevels();
    void Test_ApplyEnvironment_IgnoresUnsetAndEmptyVariables();
    void Test_ApplyEnvironment_ReadsTheDefaultPrefix();
    void Test_ApplyEnvironment_ReadsUTF8Values();
    void Test_ApplyEnvironment_ReportsInvalidValues();
    void Test_ApplyEnvironment_SeedsTheLevelAtInitialize();
    void Test_ApplyEnvironment_SetsEachKey();
    void Test_ApplySetting_ReportsAnUnknownKey();
    void Test_ApplySetting_SetsOneSetting();
    void Test_ApplySetting_WritesToStdErrWithoutHandler();
    void Test_Defaults_MatchDocumentedValues();
    void Test_ResolveFolder_CustomFolder();
    void Test_ResolvePath_AbsolutePath();
    void Test_ResolvePath_Defaults();

public:
    TTest_ASWLog_Config();
    ~TTest_ASWLog_Config() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_ConfigH

/* **************************************************************************
Test_ASWLog_CrashHandler.h
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

#ifndef Test_ASWLog_CrashHandlerH
#define Test_ASWLog_CrashHandlerH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_CrashHandler
//
// The crash handlers can't run in the test process (ASWUnitTests catches crashes itself), so the CrashProcess_*
// tests start this test executable again, which then runs a crash scenario before main() (see
// RunCrashHelperIfRequested() in the .cpp) and crashes; the test checks its log files.
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_CrashHandler : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_AppendCrashLine_CutsALongMessageButKeepsTheEnding();
    void Test_AppendCrashLine_UsesTheFixedLayout();
    void Test_CrashProcess_AbortWritesTheCrashLine();
    void Test_CrashProcess_AsyncQueueIsWrittenBeforeTheCrashLine();
    void Test_CrashProcess_FaultWritesTheBacktrace();
    void Test_CrashProcess_FaultWritesTheCrashLine();
    void Test_CrashProcess_LockHeldByTheCrashingThreadWritesTheLineDirectly();
    void Test_CrashProcess_StackOverflowWritesTheCrashLine();
    void Test_CrashProcess_TerminateThenAbortWritesOneCrashLine();
    void Test_CrashProcess_TerminateWritesTheExceptionAndFlushes();
    void Test_HandleCrash_ConsoleLogWritesTheLineToStdErr();
    void Test_HandleCrash_DisabledLoggerIsOnlyFlushed();
    void Test_HandleCrash_FlushesAndWritesTheCrashLine();
    void Test_HandleCrash_SkipsClosedAndDestroyedLoggers();
    void Test_HandleCrash_SyncsTheLineAtSyncToDiskAtLevel();
    void Test_HandleCrash_ThrowingFormatterGetsTheFixedLayoutLine();
    void Test_HandleCrash_WaitsForQueuedEntries();
    void Test_HandleCrash_WriteCrashLineFalseOnlyFlushes();
    void Test_HandleCrash_WritesTheBacktraceBeforeTheCrashLine();
    void Test_HandleCrash_WritesTheLineDirectlyWhenTheLockStaysBusy();
    void Test_InstallCrashHandlers_ChainsAndUninstallRestores();

public:
    TTest_ASWLog_CrashHandler();
    ~TTest_ASWLog_CrashHandler() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_CrashHandlerH

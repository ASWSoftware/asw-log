/* **************************************************************************
main.cpp
Author: Anthony S. West - ASW Software

Example for ASWLog.

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

//---------------------------------------------------------------------------
#include <iostream>
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

// Independent standalone function example
void SampleFunction(ASWLog::IASWLog& logger)
{
    logger.LogInfo("Inside a standard standalone function.");

    // Testing C++20 inline type-safe string formatting layout
    logger.LogDebugFmt("Dynamic template formatting inside function: status={}, value={:.4f}", "ACTIVE", 3.14159);
}

// Class example
class MyTestClass
{
public:
    void Execute(ASWLog::IASWLog& logger)
    {
        // Capture [void MyTestClass::Execute(ASWLog::IASWLog&)] automatically inside the log via 'source_location'.
        logger.LogWarnFmt("Triggering class member action inside class {}!", "MyTestClass");
    }
};

//---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // Check if user requested the logger version flag
    if (argc > 1 && std::string_view(argv[1]) == "--logversion")
    {
        std::cout << ASWLog::TASWFileLog::GetInstance().GetFullVersionStr() << std::endl;
        return 0;
    }

    std::cout << "Starting execution for ASWLog...\n";

    // Generate unique file name
    std::string generatedName = ASWLog::GenerateLogFileName("ExampleLog.txt");
    std::cout << "Generated Log Filename: " << generatedName << "\n";

    // Configure global singleton instance options
    ASWLog::TASWLogConfig globalConfig;
    globalConfig.MinimumLevel  = ASWLog::Level::Trace; // Trap all logging thresholds
    globalConfig.LogFilePath   = generatedName;
    globalConfig.BannerMessage = "--- WELCOME TO ASWLogExample - GLOBAL CONFIG ---";

    // Toggle properties on
    globalConfig.LogSourceLine = true;
    globalConfig.LogMethodName = true;
//    globalConfig.LogModuleName = true;
//    globalConfig.ModuleName    = "CORE_ENGINE";

    // Get singleton instance - the logger supports singleton and non-singleton instances
    auto& globalLogger = ASWLog::TASWFileLog::GetInstance();
    if (!globalLogger.Initialize(globalConfig))
    {
        std::cerr << "Error: Failed to initialize global singleton logger!\n";
        return 1;
    }

    std::cout << "Using singleton logger: " << globalLogger.GetFullVersionStr() << "\n";

    // Example of diverse log layers across different contextual zones
    globalLogger.LogTrace("Testing Trace log line layout parameters.");

    // Pass by abstract interface reference to verify polymorphism compatibility
    SampleFunction(globalLogger);

    MyTestClass tester;
    tester.Execute(globalLogger);

    // New C++ 'format' is supported
    globalLogger.LogCriticalFmt("CRITICAL EVENT BOUNDARY RECOVERY CODE: {:X}", 0xDEADBEEF);

    // Example of independent standalone/local logger instance
    std::cout << "Testing independent local stack instance execution...\n";
    ASWLog::TASWLogConfig localConfig;
    localConfig.MinimumLevel   = ASWLog::Level::Warn; // Skips trace/debug/info
    localConfig.LogFilePath    = "local_standalone_errors.txt";
    localConfig.LogUTCDateTime = true;
    localConfig.LogLevelStr    = true;
    localConfig.LogProcessId   = false;
    localConfig.LogThreadId    = false;

    ASWLog::TASWFileLog localLogger;
    if (localLogger.Initialize(localConfig))
    {
        // This will be cleanly skipped because the atomic min level is WARN
        localLogger.LogInfo("This Info trace should be skipped by filtering rules.");

        // This will be caught
        localLogger.LogError("Test local logger error message.");

        // Teardown with localized close message
        localLogger.Finalize("Local logger contextual shutdown successful.");
    }

    // Step E: Finalize Global Logger explicitly with final timestamp injection
    globalLogger.Finalize("Application terminated naturally via main exit block.");
    std::cout << "Execution completed.\n";

#if !defined(NDEBUG)
    std::cout << "Press enter to continue..." << std::endl;
    std::cin.get();
#endif

    return 0;
}

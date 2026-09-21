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
#include "ASWLog_ConsoleLog.h"
#include "ASWLog_FileLog.h"
#include "ASWLog_MultiLog.h"
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
    std::string generatedName = ASWLog::GenerateLogFileName("ASWLogExample", "ExampleLog.txt");
    std::cout << "Generated Log Filename: " << generatedName << "\n";

    // Configure global singleton instance options
    ASWLog::TASWLogConfig globalConfig;
    globalConfig.InitialMinimumLevel = ASWLog::Level::Trace; // Trap all logging thresholds
    globalConfig.LogFilePath         = generatedName;
    globalConfig.BannerMessage_Init = "--- WELCOME TO ASWLogExample - GLOBAL CONFIG ---";
    globalConfig.BannerMessage_Shutdown = "GLOBAL LOGGER - I'm outta here";

    // Toggle properties on
    globalConfig.LogSourceLine = true;
    globalConfig.LogMethodName = true;
    globalConfig.LogAppMem_WorkingSet = true;
    globalConfig.LogAppMem_PeakWorkingSet = true;

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

    std::cout << "Cleaning up old global logs...\n";
    globalLogger.LogInfo("Cleaning up old global logs...");
    auto logDir = globalConfig.ResolveLogFileDir();
    std::size_t nLogsDeleted = ASWLog::TASWFileLog::DeleteOldLogs(logDir, "*ExampleLog.txt", std::chrono::hours(1));
    globalLogger.LogInfoFmt("Deleted {} old global logs...", nLogsDeleted);
    std::cout << "Deleted " << nLogsDeleted << " old global logs...\n";

    // Polymorphism compatibility
    SampleFunction(globalLogger);

    MyTestClass tester;
    tester.Execute(globalLogger);

    // New C++ 'format' is supported
    globalLogger.LogCriticalFmt("CRITICAL EVENT BOUNDARY RECOVERY CODE: {:X}", 0xDEADBEEF);

    // Example of independent standalone/local logger instance
    std::cout << "Testing independent local stack instance execution...\n";
    ASWLog::TASWLogConfig localConfig;
    localConfig.InitialMinimumLevel = ASWLog::Level::Warn; // Skips trace/debug/info
    localConfig.BannerMessage_Init = "Local logger - howdy";
    localConfig.BannerMessage_Shutdown = "Local logger - cya";
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
    }
    else
    {
        std::cout << "Local logger failed to initialize!\n";
    }

    // Example of console logging with level-based color and stream routing
    std::cout << "Testing console logger with color-coded output...\n";
    ASWLog::TASWLogConfig consoleConfig;
    consoleConfig.InitialMinimumLevel = ASWLog::Level::Trace;
    consoleConfig.LogUTCDateTime = false; // Keep console lines short and readable
    consoleConfig.LogProcessId = false;
    consoleConfig.LogThreadId = false;
    consoleConfig.WriteShutdownLog = false;

    ASWLog::TASWConsoleLog consoleLogger;
    // consoleLogger.SetUseColor(false); // Uncomment on a terminal without ANSI support
    if (consoleLogger.Initialize(consoleConfig))
    {
        consoleLogger.LogInfo("Console logger initialized - Info and below print to stdout.");
        consoleLogger.LogWarn("Warn and above print to stderr and are color-coded when supported.");
        consoleLogger.LogError("Example error message, shown in red.");
    }
    else
    {
        std::cout << "Console logger failed to initialize!\n";
    }

    // Example of fanning a single call out to multiple sinks at once
    std::cout << "Testing multi-sink logger (file + console fan-out)...\n";
    ASWLog::TASWMultiLog multiLogger;
    multiLogger.AddLogger(globalLogger);  // Reuses the already-initialized file singleton
    multiLogger.AddLogger(consoleLogger); // Reuses the already-initialized console logger

    // One call reaches every registered sink; each sink still applies its own level and formatting
    multiLogger.LogInfo("This single call is written to both the log file and the console.");

    // The composite's own level is an optional pre-filter, independent of each sink's own level
    multiLogger.SetMinimumLevel(ASWLog::Level::Error);
    multiLogger.LogWarn("This Warn is suppressed by the composite gate before reaching either sink.");
    multiLogger.LogError("This Error clears the composite gate and reaches both sinks.");

    globalLogger.LogInfo("Application terminated naturally via main exit block.");
    std::cout << "Execution completed.\n";

#if !defined(NDEBUG) && defined(__BORLANDC__)
    std::cout << "Press enter to continue..." << std::endl;
    std::cin.get();
#endif

    return 0;
}

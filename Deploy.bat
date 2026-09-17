@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "TARGET_DIR=%~1"

if "%TARGET_DIR%"=="" (
    echo ERROR: A staging/deploy folder is required.
    echo Usage: Deploy.bat "C:\path\to\staging"
    exit /b 1
)

echo Deploying ASWLog release files to:
echo   %TARGET_DIR%

if not exist "%TARGET_DIR%\." (
    echo Creating target folder...
    mkdir "%TARGET_DIR%"
    if errorlevel 1 (
        echo ERROR: Could not create target folder.
        exit /b 1
    )
)

if not exist "%TARGET_DIR%\ASWLog\." (
    echo Creating ASWLog folder...
    mkdir "%TARGET_DIR%\ASWLog"
    if errorlevel 1 (
        echo ERROR: Could not create "%TARGET_DIR%\ASWLog".
        exit /b 1
    )
)

if not exist "%TARGET_DIR%\example\." (
    echo Creating example folder...
    mkdir "%TARGET_DIR%\example"
    if errorlevel 1 (
        echo ERROR: Could not create "%TARGET_DIR%\example".
        exit /b 1
    )
)

if not exist "%TARGET_DIR%\example\cmake\." (
    mkdir "%TARGET_DIR%\example\cmake"
    if errorlevel 1 exit /b 1
)

if not exist "%TARGET_DIR%\example\rad370\." (
    mkdir "%TARGET_DIR%\example\rad370"
    if errorlevel 1 exit /b 1
)

if not exist "%TARGET_DIR%\example\src\." (
    mkdir "%TARGET_DIR%\example\src"
    if errorlevel 1 exit /b 1
)

call :CopyFile "LICENSE" "%TARGET_DIR%\LICENSE"
if errorlevel 1 exit /b 1
call :CopyFile "README.md" "%TARGET_DIR%\README.md"
if errorlevel 1 exit /b 1

call :CopyFile "ASWLog\ASWLog_Base.cpp" "%TARGET_DIR%\ASWLog\ASWLog_Base.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Base.h" "%TARGET_DIR%\ASWLog\ASWLog_Base.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Config.cpp" "%TARGET_DIR%\ASWLog\ASWLog_Config.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Config.h" "%TARGET_DIR%\ASWLog\ASWLog_Config.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_FileLog.cpp" "%TARGET_DIR%\ASWLog\ASWLog_FileLog.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_FileLog.h" "%TARGET_DIR%\ASWLog\ASWLog_FileLog.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Interface.cpp" "%TARGET_DIR%\ASWLog\ASWLog_Interface.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Interface.h" "%TARGET_DIR%\ASWLog\ASWLog_Interface.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Types.cpp" "%TARGET_DIR%\ASWLog\ASWLog_Types.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Types.h" "%TARGET_DIR%\ASWLog\ASWLog_Types.h"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Utils.cpp" "%TARGET_DIR%\ASWLog\ASWLog_Utils.cpp"
if errorlevel 1 exit /b 1
call :CopyFile "ASWLog\ASWLog_Utils.h" "%TARGET_DIR%\ASWLog\ASWLog_Utils.h"
if errorlevel 1 exit /b 1

call :CopyFile "example\cmake\CMakeLists.txt" "%TARGET_DIR%\example\cmake\CMakeLists.txt"
if errorlevel 1 exit /b 1
call :CopyFile "example\cmake\README.md" "%TARGET_DIR%\example\cmake\README.md"
if errorlevel 1 exit /b 1

call :CopyFile "example\rad370\ASWLogExample.cbproj" "%TARGET_DIR%\example\rad370\ASWLogExample.cbproj"
if errorlevel 1 exit /b 1
call :CopyFile "example\rad370\ASWLogExamplePCH1.h" "%TARGET_DIR%\example\rad370\ASWLogExamplePCH1.h"
if errorlevel 1 exit /b 1
call :CopyFile "example\rad370\Build_Win64x_Debug.bat" "%TARGET_DIR%\example\rad370\Build_Win64x_Debug.bat"
if errorlevel 1 exit /b 1
call :CopyFile "example\rad370\Build_Win64x_Release.bat" "%TARGET_DIR%\example\rad370\Build_Win64x_Release.bat"
if errorlevel 1 exit /b 1

call :CopyFile "example\src\main.cpp" "%TARGET_DIR%\example\src\main.cpp"
if errorlevel 1 exit /b 1

echo Deployment completed successfully.
exit /b 0

:CopyFile
set "SOURCE_FILE=%~1"
set "DESTINATION_FILE=%~2"
echo Copying %SOURCE_FILE%...
if not exist "%SCRIPT_DIR%%SOURCE_FILE%" (
    echo ERROR: Source file not found: "%SCRIPT_DIR%%SOURCE_FILE%"
    exit /b 1
)
if not exist "%DESTINATION_FILE%\.." (
    mkdir "%DESTINATION_FILE%\.."
)
copy /Y "%SCRIPT_DIR%%SOURCE_FILE%" "%DESTINATION_FILE%" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy "%SOURCE_FILE%".
    exit /b 1
)
exit /b 0
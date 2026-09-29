@echo off

REM Builds the ASWLog unit tests with RAD Studio. Requires the Rad370 environment variable.

if not exist "%~dp0..\..\third_party\asw-unit-tests\src\ASWUnitTests_Registry.h" (
    echo ERROR: The ASWUnitTests submodule is missing from third_party\asw-unit-tests.
    echo From the repository root, run: git submodule update --init
    exit /b 1
)

call "%Rad370%\bin\rsvars.bat"

pushd "%~dp0"
call cmd /c msbuild /t:Build /p:Config=Release;Platform=Win64x ASWLogTests.cbproj
set "BUILD_RESULT=%ERRORLEVEL%"
popd

exit /b %BUILD_RESULT%

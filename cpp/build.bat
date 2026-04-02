@echo off
setlocal

set "ROOT_DIR=C:\ProtoEditor\cpp"
set "TMP_DIR=%ROOT_DIR%\.tmp"
set "BUILD_DIR=%ROOT_DIR%\build"
set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

set "TEMP=%TMP_DIR%"
set "TMP=%TMP_DIR%"

if not exist "%TMP_DIR%" mkdir "%TMP_DIR%"

call "%VCVARS_BAT%"
if errorlevel 1 exit /b %errorlevel%

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

"%CMAKE_EXE%" -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo.
    echo Visual Studio solution olusturulamadi.
    echo Devam etmek icin herhangi bir tusa basin...
    pause > nul
    exit /b %errorlevel%
)

echo.
echo Visual Studio solution hazir:
echo %BUILD_DIR%\ProtoEditor.sln
echo Devam etmek icin herhangi bir tusa basin...
pause > nul
exit /b 0

@echo off
setlocal

set "TEMP=C:\ProtoEditor\cpp\.tmp"
set "TMP=C:\ProtoEditor\cpp\.tmp"
if not exist "C:\ProtoEditor\cpp\.tmp" mkdir "C:\ProtoEditor\cpp\.tmp"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%

if exist "C:\ProtoEditor\cpp\build" rmdir /s /q "C:\ProtoEditor\cpp\build"

"C:\Program Files\CMake\bin\cmake.exe" -S "C:\ProtoEditor\cpp" -B "C:\ProtoEditor\cpp\build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\cmake.exe" --build "C:\ProtoEditor\cpp\build" --config Release
exit /b %errorlevel%

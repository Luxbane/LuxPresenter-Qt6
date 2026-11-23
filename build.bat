@echo off
rem Configure, build and test LuxPresenter (MSVC 2022 + Ninja + Qt 6.12.0).
rem Output: E:\LuxPresenter-QT6\Portable\LuxPresenter.exe
rem Usage: build.bat [Release|Debug]   (default: Release)
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set CMAKE=C:\Users\sandl\mingw64\bin\cmake.exe
set NINJA=C:\Users\sandl\mingw64\bin\ninja.exe
set QT=C:/Qt/6.12.0/msvc2022_64
set "PATH=%QT%\bin;%PATH%"

"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=%QT% -DCMAKE_CXX_COMPILER=cl || exit /b 1
"%CMAKE%" --build "%~dp0build" || exit /b 1
"C:\Users\sandl\mingw64\bin\ctest.exe" --test-dir "%~dp0build" --output-on-failure || exit /b 1
exit /b 0

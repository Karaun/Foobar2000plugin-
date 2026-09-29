@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0.."

echo ============================================================
echo Foobar Pro Suite v2.6 - BUILD ONLY: Audio Settings
echo ============================================================

if not exist "sdk\foobar2000\SDK\foobar2000.h" (
  call "%~dp0Prepare_SDK_2023-09-23.bat"
  if errorlevel 1 exit /b 1
)

set "VS2019="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" set "VS2019=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
if not defined VS2019 if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VS2019=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
if not defined VS2019 if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VS2019=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise"
if not defined VS2019 if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VS2019=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
if not defined VS2019 (
  echo [ERROR] Visual Studio 2019 C++ tools not found.
  pause
  exit /b 11
)
call "%VS2019%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 13

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Retarget_SDK_VS2019.ps1"
if errorlevel 1 exit /b 16
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Patch_SDK_NoPCH.ps1"
if errorlevel 1 exit /b 17

set "CL_MPCount=1"
set "UseMultiToolTask=false"
set "PreferredToolArchitecture=x64"

msbuild "FoobarProSuite.sln" /m:1 /t:foo_easy_audio_settings /p:Configuration=Release /p:Platform=x64 /p:BuildInParallel=false /p:UseMultiToolTask=false /p:CL_MPCount=1 /p:PreferredToolArchitecture=x64 /v:minimal
if errorlevel 1 (
  echo [ERROR] Build failed.
  pause
  exit /b 20
)

if not exist "Release_x64\foo_easy_audio_settings.dll" (
  echo [ERROR] Expected DLL missing: Release_x64\foo_easy_audio_settings.dll
  pause
  exit /b 21
)

echo [SUCCESS] Release_x64\foo_easy_audio_settings.dll
pause
exit /b 0

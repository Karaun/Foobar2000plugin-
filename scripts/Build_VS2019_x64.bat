@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0.."

echo ============================================================
echo Foobar Pro Suite v2.9.2 - BUILD ALL - VS2019 x64
echo ============================================================
echo.

if not exist "sdk\foobar2000\SDK\foobar2000.h" (
  echo [INFO] foobar2000 SDK not found. Preparing SDK...
  call "%~dp0Prepare_SDK_2023-09-23.bat"
  if errorlevel 1 (
    echo [ERROR] SDK preparation failed.
    pause
    exit /b 1
  )
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

echo [INFO] Retargeting SDK to VS2019 / v142 / C++17...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Retarget_SDK_VS2019.ps1"
if errorlevel 1 (
  echo [ERROR] SDK retarget failed.
  pause
  exit /b 16
)

echo [INFO] Applying low-memory SDK patch...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Patch_SDK_NoPCH.ps1"
if errorlevel 1 (
  echo [ERROR] SDK low-memory patch failed.
  pause
  exit /b 17
)

set "CL_MPCount=1"
set "UseMultiToolTask=false"
set "PreferredToolArchitecture=x64"

if not exist "Release_x64" mkdir "Release_x64"
del /q "Release_x64\foo_easy_audio_settings.dll" 2>nul
del /q "Release_x64\foo_pro_nowplaying.dll" 2>nul
del /q "Release_x64\foo_pro_lyrics.dll" 2>nul
del /q "Release_x64\foo_pro_album_grid.dll" 2>nul
del /q "Release_x64\foo_pro_transport.dll" 2>nul

echo.
echo [INFO] Building FoobarProSuite.sln...
msbuild "FoobarProSuite.sln" ^
 /m:1 ^
 /t:Build ^
 /p:Configuration=Release ^
 /p:Platform=x64 ^
 /p:BuildInParallel=false ^
 /p:UseMultiToolTask=false ^
 /p:CL_MPCount=1 ^
 /p:PreferredToolArchitecture=x64 ^
 /v:minimal

if errorlevel 1 (
  echo.
  echo [ERROR] Pro Suite build failed.
  echo Please send the FIRST error Cxxxx or LNKxxxx block.
  pause
  exit /b 20
)

echo.
echo [VERIFY] Checking all expected DLLs...
set "MISSING=0"
if exist "Release_x64\foo_easy_audio_settings.dll" (echo [OK] foo_easy_audio_settings.dll) else (echo [MISSING] foo_easy_audio_settings.dll & set "MISSING=1")
if exist "Release_x64\foo_pro_nowplaying.dll" (echo [OK] foo_pro_nowplaying.dll) else (echo [MISSING] foo_pro_nowplaying.dll & set "MISSING=1")
if exist "Release_x64\foo_pro_lyrics.dll" (echo [OK] foo_pro_lyrics.dll) else (echo [MISSING] foo_pro_lyrics.dll & set "MISSING=1")
if exist "Release_x64\foo_pro_album_grid.dll" (echo [OK] foo_pro_album_grid.dll) else (echo [MISSING] foo_pro_album_grid.dll & set "MISSING=1")
if exist "Release_x64\foo_pro_transport.dll" (echo [OK] foo_pro_transport.dll) else (echo [MISSING] foo_pro_transport.dll & set "MISSING=1")

if "!MISSING!"=="1" (
  echo.
  echo [ERROR] One or more DLLs are missing.
  pause
  exit /b 21
)

echo.
echo ============================================================
echo [SUCCESS] ALL 5 DLLs generated
echo ============================================================
echo   %CD%\Release_x64\foo_easy_audio_settings.dll
echo   %CD%\Release_x64\foo_pro_nowplaying.dll
echo   %CD%\Release_x64\foo_pro_lyrics.dll
echo   %CD%\Release_x64\foo_pro_album_grid.dll
echo   %CD%\Release_x64\foo_pro_transport.dll
echo.
echo Next: run Install_ProSuite_to_foobar2000.bat
pause
exit /b 0

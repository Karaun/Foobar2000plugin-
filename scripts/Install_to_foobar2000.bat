@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0.."

set "DLL=%CD%\Release_x64\foo_easy_audio_settings.dll"
if not exist "%DLL%" (
    echo [ERROR] DLL not found:
    echo %DLL%
    echo Run Build_VS2019_x64.bat first.
    pause
    exit /b 1
)

set "TARGET="
if exist "D:\HCTT\foobar2000\profile" (
    set "TARGET=D:\HCTT\foobar2000\profile\user-components-x64\foo_easy_audio_settings"
) else (
    set "TARGET=%APPDATA%\foobar2000-v2\user-components-x64\foo_easy_audio_settings"
)

if not exist "%TARGET%" mkdir "%TARGET%"

copy /Y "%DLL%" "%TARGET%\foo_easy_audio_settings.dll" >nul
if errorlevel 1 (
    echo [ERROR] Copy failed. Close foobar2000 and try again.
    pause
    exit /b 2
)

echo [OK] Installed to:
echo %TARGET%
echo.
echo Restart foobar2000.
echo Then open View - Audio Settings, or add the Audio Settings command to the toolbar.
pause

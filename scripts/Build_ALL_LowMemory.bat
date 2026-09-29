@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0.."
echo ============================================================
echo Foobar Pro Suite - Low Memory ALL-DLL Build
echo ============================================================
set "CL_MPCount=1"
set "UseMultiToolTask=false"
call "%~dp0Build_VS2019_x64.bat"
exit /b %errorlevel%

@echo off
:: 【强制锁定UE自带.NET 8.0，彻底屏蔽系统.NET 9.0】
set "DOTNET_ROOT=%~dp0Engine\Binaries\ThirdParty\DotNet\Windows"
set "DOTNET_MULTILEVEL_LOOKUP=0"
set "PATH=%DOTNET_ROOT%;%PATH%"
:: 清除系统级DOTNET环境变量
set "DOTNET_CLI_TELEMETRY_OPTOUT=1"
set "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"

rem Copyright Epic Games, Inc. All Rights Reserved.
if not exist "%~dp0Engine\Build\BatchFiles\GenerateProjectFiles.bat" goto Error_BatchFileInWrongLocation
call "%~dp0Engine\Build\BatchFiles\GenerateProjectFiles.bat" %*
exit /B %ERRORLEVEL%

:Error_BatchFileInWrongLocation
echo GenerateProjectFiles ERROR: The batch file does not appear to be located in the root UE directory.  This script must be run from within that directory.
pause
exit /B 1

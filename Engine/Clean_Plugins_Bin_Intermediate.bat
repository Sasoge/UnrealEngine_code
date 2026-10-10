@echo off
chcp 65001 >nul
echo ==============================================
echo  UE引擎插件一键清理脚本
echo  清理：所有插件的 Intermediate / Binaries / Saved
echo  安全可删，不会影响源码
echo ==============================================
echo.

set "ENGINE_ROOT=%~dp0"

echo 正在清理插件 Intermediate...
for /d /r "%ENGINE_ROOT%Plugins" %%d in (Intermediate) do (
    if exist "%%d" (
        echo 删除：%%d
        rd /s /q "%%d" 2>nul
    )
)

echo.
echo 正在清理插件 Binaries...
for /d /r "%ENGINE_ROOT%Plugins" %%d in (Binaries) do (
    if exist "%%d" (
        echo 删除：%%d
        rd /s /q "%%d" 2>nul
    )
)

echo.
echo 正在清理插件 Saved...
for /d /r "%ENGINE_ROOT%Plugins" %%d in (Saved) do (
    if exist "%%d" (
        echo 删除：%%d
        rd /s /q "%%d" 2>nul
    )
)

echo.
echo ==============================================
echo  清理完成！通常可释放 15GB~40GB 空间
echo ==============================================
echo.
pause
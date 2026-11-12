@echo off
chcp 65001 >nul
setlocal EnableExtensions

REM ============================================================
REM  SpectrumTag - 恢复"第一次打开"状态（测试用）
REM
REM  用法: reset_first_run.bat [/y] [/keep-recordings] [/run]
REM    /y                不询问，直接删除（供脚本调用，结束后不停顿）
REM    /keep-recordings  保留 Recordings 里的 .sptrec，已有 DAW 工程仍能打开
REM    /run              重置完成后直接启动独立版
REM
REM  说明: 只删除 SpectrumTag 自己的状态文件，不碰音乐、图片、导出目录，
REM        也不删除数据目录本身。VST3 的工程状态保存在 DAW 里，需新建实例。
REM ============================================================

set "CONFIRM=1"
set "KEEP_RECORDINGS=0"
set "LAUNCH=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="/y"                set "CONFIRM=0"
if /i "%~1"=="/keep-recordings"  set "KEEP_RECORDINGS=1"
if /i "%~1"=="/run"              set "LAUNCH=1"
shift
goto parse_args
:args_done

echo ============================================================
echo  SpectrumTag 恢复首次打开状态
echo ============================================================

REM ---------- 1) 定位数据目录 ----------
REM  与 shared/Presentation.h 一致:
REM    SPECTRUMTAG_PREFS_DIR 优先; 有 D 盘用 D:\Codex\PluginData\SpectrumTag;
REM    否则 %APPDATA%\SpectrumTag。Recordings / Standalone / Images /
REM    RenderCache 都在这个目录下。
set "DATA_DIR="
if defined SPECTRUMTAG_PREFS_DIR set "DATA_DIR=%SPECTRUMTAG_PREFS_DIR%"
if not defined DATA_DIR if exist "D:\" set "DATA_DIR=D:\Codex\PluginData\SpectrumTag"
if not defined DATA_DIR set "DATA_DIR=%APPDATA%\SpectrumTag"

if not defined DATA_DIR goto unsafe
if "%DATA_DIR:~-1%"=="\" set "DATA_DIR=%DATA_DIR:~0,-1%"
REM 拒绝盘符根目录和过短的路径，避免误删
if "%DATA_DIR:~3%"=="" goto unsafe

set "TELEMETRY_FILE=%APPDATA%\iisaacbeats\Telemetry\spectrumtag.xml"
set "TEMP_DIR=%APPDATA%\SpectrumTag\Temporary"

echo [INFO] 数据目录: "%DATA_DIR%"
if defined SPECTRUMTAG_PREFS_DIR echo [INFO] 来源: 环境变量 SPECTRUMTAG_PREFS_DIR

if not exist "%DATA_DIR%" echo [INFO] 数据目录不存在，当前已经是首次打开状态。

REM ---------- 2) 关闭正在运行的独立版 ----------
tasklist /fi "imagename eq SpectrumTag.exe" 2>nul | find /i "SpectrumTag.exe" >nul
if not errorlevel 1 (
  echo [INFO] 检测到独立版正在运行，先关闭它 ...
  taskkill /im SpectrumTag.exe /f >nul 2>nul
  ping -n 3 127.0.0.1 >nul
  tasklist /fi "imagename eq SpectrumTag.exe" 2>nul | find /i "SpectrumTag.exe" >nul
  if not errorlevel 1 (
    echo [ERROR] 无法关闭 SpectrumTag.exe，请手动退出后重试。
    goto fail
  )
)

REM ---------- 3) 统计并列出将要删除的内容 ----------
set "PROBE_FILE=%TEMP%\spectrumtag-reset-size-%RANDOM%%RANDOM%.txt"
powershell -NoProfile -ExecutionPolicy Bypass -Command "[IO.File]::WriteAllText('%PROBE_FILE%', [string][math]::Round([double](Get-ChildItem -Recurse -Force -File -LiteralPath '%DATA_DIR%' -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum/1MB,1))" >nul 2>nul
set "DATA_MB=0"
if exist "%PROBE_FILE%" set /p DATA_MB=<"%PROBE_FILE%"
if exist "%PROBE_FILE%" del "%PROBE_FILE%" >nul 2>nul

echo.
if exist "%DATA_DIR%" (
  echo [INFO] 当前内容，约 %DATA_MB% MB:
  dir /b "%DATA_DIR%"
) else (
  echo [INFO] 数据目录为空。
)
echo.
echo   将删除: preferences.xml / Standalone / Images / RenderCache
if "%KEEP_RECORDINGS%"=="1" echo   将保留: Recordings 目录里的 .sptrec
if not "%KEEP_RECORDINGS%"=="1" echo   将删除: Recordings 里的 .sptrec，已有 DAW 工程会因此打不开音频
if exist "%TELEMETRY_FILE%" echo   将删除: "%TELEMETRY_FILE%"
echo.

if not "%CONFIRM%"=="1" goto do_reset
choice /c YN /n /m "确认恢复首次打开状态? 按 Y 继续, N 取消: "
if errorlevel 2 goto cancelled

:do_reset
echo [INFO] 开始清理 ...
if exist "%DATA_DIR%\preferences.xml" del /q "%DATA_DIR%\preferences.xml" >nul 2>nul
if exist "%DATA_DIR%\Standalone" rmdir /s /q "%DATA_DIR%\Standalone" >nul 2>nul
if exist "%DATA_DIR%\Images" rmdir /s /q "%DATA_DIR%\Images" >nul 2>nul
if exist "%DATA_DIR%\RenderCache" rmdir /s /q "%DATA_DIR%\RenderCache" >nul 2>nul
if exist "%TELEMETRY_FILE%" del /q "%TELEMETRY_FILE%" >nul 2>nul
if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%" >nul 2>nul
if "%KEEP_RECORDINGS%"=="1" goto report
if exist "%DATA_DIR%\Recordings" rmdir /s /q "%DATA_DIR%\Recordings" >nul 2>nul

:report
echo.
echo [OK] 已恢复首次打开状态。
if not exist "%DATA_DIR%" goto report_done
echo [INFO] 数据目录现在的内容: "%DATA_DIR%"
dir /b "%DATA_DIR%" 2>nul
:report_done
echo [INFO] 下次启动会重新创建 preferences.xml 与 Standalone\last-session.xml。
echo [INFO] VST3 的工程状态存在 DAW 工程里，插件请新建实例测试。

if "%LAUNCH%"=="1" goto launch
goto finished

:launch
set "EXE="
call :detect_exe
if defined EXE echo [INFO] 启动 "%EXE%"
if defined EXE start "" "%EXE%"
if not defined EXE echo [WARN] 未找到独立版 SpectrumTag.exe，请先构建 Release 产物。
goto finished

:cancelled
echo.
echo [INFO] 已取消，没有删除任何文件。
if "%CONFIRM%"=="1" pause >nul
endlocal
exit /b 1

:unsafe
echo [ERROR] 数据目录不可用或指向盘符根目录，已中止: "%DATA_DIR%"
goto fail

:fail
if "%CONFIRM%"=="1" pause >nul
endlocal
exit /b 1

:finished
if "%CONFIRM%"=="1" pause >nul
endlocal
exit /b 0

:detect_exe
if exist "%~dp0cmake-build-release-visual-studio\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe" set "EXE=%~dp0cmake-build-release-visual-studio\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
if not defined EXE if exist "%~dp0build\msvc\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe" set "EXE=%~dp0build\msvc\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
if not defined EXE if exist "%~dp0cmake-build-release\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe" set "EXE=%~dp0cmake-build-release\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
exit /b 0

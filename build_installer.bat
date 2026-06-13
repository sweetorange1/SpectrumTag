@echo off
setlocal

REM ============================================================
REM  SpectrumTag - Windows Release Installer Builder
REM  Version : 1.2.0
REM  Output  : dist\SpectrumTag_Setup_1.2.0_x64.exe
REM ============================================================

set "APP_NAME=SpectrumTag"
set "APP_VERSION=1.2.0"
set "SCRIPT_DIR=%~dp0"
set "ISS_FILE=%SCRIPT_DIR%SpectrumTag_installer.iss"
set "VST3_DIR=%SCRIPT_DIR%cmake-build-release\SpectrumTag_artefacts\Release\VST3"
set "DIST_DIR=%SCRIPT_DIR%dist"
set "OUTPUT_EXE=%DIST_DIR%\%APP_NAME%_Setup_%APP_VERSION%_x64.exe"

echo ============================================================
echo  %APP_NAME% Installer Builder  v%APP_VERSION%  (Release)
echo ============================================================

if not exist "%ISS_FILE%" (
  echo [ERROR] 未找到安装脚本: "%ISS_FILE%"
  exit /b 1
)

if not exist "%VST3_DIR%" (
  echo [ERROR] 未找到 VST3 构建产物目录: "%VST3_DIR%"
  echo [HINT] 请先以 Release 模式构建 SpectrumTag 后再打包。
  echo        例如: cmake --build cmake-build-release --config Release --target SpectrumTag
  exit /b 1
)

if not exist "%DIST_DIR%" (
  mkdir "%DIST_DIR%"
)

REM 检查并创建 presents 目录（避免 Inno Setup 编译失败）
if not exist "%SCRIPT_DIR%presents" (
  echo [WARN] 未找到 presents 目录，将创建空目录（安装包将不包含预设文件）。
  mkdir "%SCRIPT_DIR%presents"
) else (
  echo [INFO] 找到 presents 目录
)

set "ISCC_PATH=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_PATH%" set "ISCC_PATH=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_PATH%" set "ISCC_PATH=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"

if not exist "%ISCC_PATH%" (
  echo [ERROR] 未找到 ISCC.exe，请先安装 Inno Setup 6。
  echo 你可以运行: winget install --id JRSoftware.InnoSetup -e
  exit /b 1
)

echo [INFO] 编译器: "%ISCC_PATH%"
echo [INFO] 安装脚本: "%ISS_FILE%"
echo [INFO] VST3 产物: "%VST3_DIR%"

REM 检查 presents 目录是否有文件
set "ISCC_EXTRA_FLAGS="
if exist "%SCRIPT_DIR%presents\*" (
  echo [INFO] 找到 presents 目录，将包含预设文件
  set "ISCC_EXTRA_FLAGS=/DPRESETS_EXIST=1"
) else (
  echo [WARN] presents 目录为空，将不包含预设文件
)

echo [INFO] 开始打包 ...
echo ------------------------------------------------------------

"%ISCC_PATH%" %ISCC_EXTRA_FLAGS% "%ISS_FILE%"

if errorlevel 1 (
  echo ------------------------------------------------------------
  echo [ERROR] 打包失败，请查看上方日志。
  exit /b 1
)

echo ------------------------------------------------------------
if exist "%OUTPUT_EXE%" (
  echo [OK] 打包完成
  echo      输出: "%OUTPUT_EXE%"
) else (
  echo [OK] 打包完成
  echo      输出目录: "%DIST_DIR%"
)
endlocal

echo.
echo 按任意键退出...
pause >nul
exit /b 0


@echo off
setlocal EnableExtensions

REM ============================================================
REM  SpectrumTag - Windows Release Installer Builder
REM  Version : 取自 CMakeLists.txt 的 project(SpectrumTag VERSION x.y.z)
REM            （本脚本和 .iss 都不再写死版本号）
REM  Output  : dist\SpectrumTag_Setup_x.y.z_x64.exe
REM ============================================================

set "APP_NAME=SpectrumTag"
set "SCRIPT_DIR=%~dp0"
set "ISS_FILE=%SCRIPT_DIR%SpectrumTag_installer.iss"
set "DIST_DIR=%SCRIPT_DIR%dist"
set "CMAKE_FILE=%SCRIPT_DIR%CMakeLists.txt"
set "PROBE_FILE=%TEMP%\spectrumtag-version-%RANDOM%%RANDOM%.txt"

if not exist "%ISS_FILE%" (
  echo [ERROR] 未找到安装脚本: "%ISS_FILE%"
  exit /b 1
)
if not exist "%CMAKE_FILE%" (
  echo [ERROR] 未找到 "%CMAKE_FILE%"，无法确定产品版本。
  exit /b 1
)

REM ---------- 0) 从 CMakeLists.txt 读取唯一版本源 ----------
set "APP_VERSION="
powershell -NoProfile -ExecutionPolicy Bypass -Command "[IO.File]::WriteAllText('%PROBE_FILE%', [regex]::Match([IO.File]::ReadAllText('%CMAKE_FILE%'), 'project\(SpectrumTag VERSION ([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value)" >nul 2>nul
if exist "%PROBE_FILE%" set /p APP_VERSION=<"%PROBE_FILE%"
if exist "%PROBE_FILE%" del "%PROBE_FILE%" >nul 2>nul

if not defined APP_VERSION (
  echo [ERROR] 无法从 CMakeLists.txt 解析版本号，期望形如 "project SpectrumTag VERSION x.y.z"
  exit /b 1
)

set "OUTPUT_EXE=%DIST_DIR%\%APP_NAME%_Setup_%APP_VERSION%_x64.exe"

echo ============================================================
echo  %APP_NAME% Installer Builder  v%APP_VERSION%  (Release)
echo ============================================================

REM ---------- 1) 自动探测 VST3 顶层目录（包含 SpectrumTag.vst3 bundle 的父目录）----------
set "VST3_DIR="

if exist "%SCRIPT_DIR%build\msvc\SpectrumTag_artefacts\Release\VST3\SpectrumTag.vst3\Contents\x86_64-win\SpectrumTag.vst3" (
  set "VST3_DIR=%SCRIPT_DIR%build\msvc\SpectrumTag_artefacts\Release\VST3"
)

if not defined VST3_DIR if exist "%SCRIPT_DIR%cmake-build-release-visual-studio\SpectrumTag_artefacts\Release\VST3\SpectrumTag.vst3" (
  set "VST3_DIR=%SCRIPT_DIR%cmake-build-release-visual-studio\SpectrumTag_artefacts\Release\VST3"
)

if not defined VST3_DIR if exist "%SCRIPT_DIR%cmake-build-release\SpectrumTag_artefacts\Release\VST3\SpectrumTag.vst3" (
  set "VST3_DIR=%SCRIPT_DIR%cmake-build-release\SpectrumTag_artefacts\Release\VST3"
)

if not defined VST3_DIR if exist "%LOCALAPPDATA%\Programs\Common\VST3\SpectrumTag.vst3" (
  set "VST3_DIR=%LOCALAPPDATA%\Programs\Common\VST3"
)

if not defined VST3_DIR (
  echo [ERROR] 未找到 VST3 构建产物 SpectrumTag.vst3。
  echo [HINT] 请先以 Release 模式构建 SpectrumTag 后再打包，例如:
  echo        cmake -B cmake-build-release-visual-studio -DCMAKE_BUILD_TYPE=Release
  echo        cmake --build cmake-build-release-visual-studio --config Release
  exit /b 1
)

REM ---------- 2) 自动探测 Standalone 独立程序 exe ----------
set "STANDALONE_EXE="

if exist "%SCRIPT_DIR%build\msvc\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe" (
  set "STANDALONE_EXE=%SCRIPT_DIR%build\msvc\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
)

if not defined STANDALONE_EXE if exist "%SCRIPT_DIR%cmake-build-release-visual-studio\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe" (
  set "STANDALONE_EXE=%SCRIPT_DIR%cmake-build-release-visual-studio\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
)

if not defined STANDALONE_EXE if exist "%SCRIPT_DIR%cmake-build-release\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe" (
  set "STANDALONE_EXE=%SCRIPT_DIR%cmake-build-release\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
)

if not defined STANDALONE_EXE (
  echo [ERROR] 未找到 Standalone 产物 SpectrumTag.exe。Standalone 是软件主体，安装包必须包含它。
  echo [HINT] 请先以 Release 模式构建 SpectrumTagStandalone（或 SpectrumTag_All）后再打包。
  exit /b 1
) else (
  echo [INFO] Standalone 产物: "%STANDALONE_EXE%"
)

REM ---------- 3) 校验产物版本，避免旧程序被打进新安装包 ----------
set "VST3_BINARY=%VST3_DIR%\SpectrumTag.vst3\Contents\x86_64-win\SpectrumTag.vst3"
set "EXE_VERSION=未知"
set "VST3_VERSION=未知"

powershell -NoProfile -ExecutionPolicy Bypass -Command "[IO.File]::WriteAllText('%PROBE_FILE%', (Get-Item -LiteralPath '%STANDALONE_EXE%').VersionInfo.ProductVersion)" >nul 2>nul
if exist "%PROBE_FILE%" set /p EXE_VERSION=<"%PROBE_FILE%"
if exist "%PROBE_FILE%" del "%PROBE_FILE%" >nul 2>nul

if exist "%VST3_BINARY%" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "[IO.File]::WriteAllText('%PROBE_FILE%', (Get-Item -LiteralPath '%VST3_BINARY%').VersionInfo.ProductVersion)" >nul 2>nul
  if exist "%PROBE_FILE%" set /p VST3_VERSION=<"%PROBE_FILE%"
  if exist "%PROBE_FILE%" del "%PROBE_FILE%" >nul 2>nul
) else (
  echo [WARN] 未找到 VST3 二进制 "%VST3_BINARY%"，跳过其版本校验。
)

echo [INFO] 源码版本: %APP_VERSION%
echo [INFO] Standalone 产物版本: %EXE_VERSION%
echo [INFO] VST3 产物版本: %VST3_VERSION%

if not "%EXE_VERSION%"=="%APP_VERSION%" (
  echo [ERROR] Standalone 产物版本 %EXE_VERSION% 与源码版本 %APP_VERSION% 不一致。
  echo [HINT] 旧产物会被打进新安装包，请先重新构建 Release 产物再打包。
  exit /b 1
)
if not "%VST3_VERSION%"=="%APP_VERSION%" (
  if not "%VST3_VERSION%"=="未知" (
    echo [ERROR] VST3 产物版本 %VST3_VERSION% 与源码版本 %APP_VERSION% 不一致。
    echo [HINT] 请先重新构建 Release 产物再打包。
    exit /b 1
  )
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
set "PRESETS_FLAG="
if exist "%SCRIPT_DIR%presents\*" (
  echo [INFO] 找到 presents 目录，将包含预设文件
  set "PRESETS_FLAG=/DPRESETS_EXIST=1"
) else (
  echo [WARN] presents 目录为空，将不包含预设文件
)

echo [INFO] 开始打包 v%APP_VERSION% ...
echo ------------------------------------------------------------

REM 版本号以 /DMyAppVersion 传给 .iss，安装包文件名、卸载项名称和版本资源都用它。
"%ISCC_PATH%" /DMyAppVersion=%APP_VERSION% /DVST3_DIR="%VST3_DIR%" /DSTANDALONE_EXE="%STANDALONE_EXE%" %PRESETS_FLAG% "%ISS_FILE%"

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

if /i not "%~1"=="/nopause" pause >nul
exit /b 0

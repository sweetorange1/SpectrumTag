@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ISS_FILE=%SCRIPT_DIR%SpectrumTag_installer.iss"
set "VST3_DIR=%SCRIPT_DIR%cmake-build-release\SpectrumTag_artefacts\Release\VST3"

if not exist "%ISS_FILE%" (
  echo [ERROR] 未找到安装脚本: "%ISS_FILE%"
  exit /b 1
)

if not exist "%VST3_DIR%" (
  echo [ERROR] 未找到 VST3 构建产物目录: "%VST3_DIR%"
  echo [HINT] 请先构建 Release 版本后再打包。
  exit /b 1
)

set "ISCC_PATH=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_PATH%" set "ISCC_PATH=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_PATH%" set "ISCC_PATH=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"

if not exist "%ISCC_PATH%" (
  echo [ERROR] 未找到 ISCC.exe，请先安装 Inno Setup 6。
  echo 你可以运行: winget install --id JRSoftware.InnoSetup -e
  exit /b 1
)

echo [INFO] 使用编译器: "%ISCC_PATH%"
echo [INFO] 开始打包: "%ISS_FILE%"
"%ISCC_PATH%" "%ISS_FILE%"

if errorlevel 1 (
  echo [ERROR] 打包失败，请查看上方日志。
  exit /b 1
)

echo [OK] 打包完成，输出目录通常为: "%SCRIPT_DIR%dist"
endlocal
exit /b 0


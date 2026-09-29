#define MyAppName "SpectrumTag"
#define MyAppVersion "1.4.1"
#define MyAppPublisher "iisaacbeats.cn"
#define MyAppURL "https://iisaacbeats.cn"
#define MyAppCopyright "Copyright (C) 2026 iisaacbeats.cn"
#define MyPluginBundle "SpectrumTag.vst3"
#define MyAppExe "SpectrumTag.exe"

; Standalone（独立可执行程序）exe 的完整路径 —— 软件主体，安装包必须包含。
; build_installer.bat 探测到产物时会用 /DSTANDALONE_EXE=<完整路径> 传入。
;#define STANDALONE_EXE "cmake-build-release-visual-studio\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"
#ifndef STANDALONE_EXE
  #error "STANDALONE_EXE 未定义：请先构建 Standalone 产物并通过 /DSTANDALONE_EXE=<完整路径> 传入"
#endif

; VST3 顶层目录（即包含 SpectrumTag.vst3 bundle 的父目录）。
; 默认指向 Release 构建目录；build_installer.bat 会用 -DVST3_DIR 覆盖为实际探测到的路径。
#ifndef VST3_DIR
  #define VST3_DIR "cmake-build-release\SpectrumTag_artefacts\Release\VST3"
#endif

[Setup]
AppId={{0E3BF70B-5D5C-4F0F-B6E4-50F8C4B55C01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
AppCopyright={#MyAppCopyright}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Standalone Application and VST3 Plug-in Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DirExistsWarning=no
OutputDir=dist
OutputBaseFilename={#MyAppName}_Setup_{#MyAppVersion}_x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
SetupLogging=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DisableDirPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

; 软件主体是 Standalone 独立程序（必装）；VST3 插件作为补充，可选安装。
; 目录页选择的是独立程序的安装位置；VST3 固定安装到系统标准目录，不提供自定义。
[Components]
Name: "standalone"; Description: "{#MyAppName} standalone application"; Types: full compact custom; Flags: fixed
Name: "plugin"; Description: "{#MyAppName} VST3 plug-in"; Types: full compact custom

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for the standalone app"; GroupDescription: "Additional icons:"; Components: standalone

[Files]
Source: {#STANDALONE_EXE}; DestDir: "{app}"; DestName: "{#MyAppExe}"; Components: standalone; Flags: ignoreversion
Source: "{#VST3_DIR}\{#MyPluginBundle}\*"; DestDir: "{commoncf}\VST3\iisaacbeats.cn\{#MyPluginBundle}"; Components: plugin; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Components: standalone
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Components: standalone; Tasks: desktopicon

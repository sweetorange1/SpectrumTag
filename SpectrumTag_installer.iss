#define MyAppName "SpectrumTag"
#define MyAppVersion "1.3.0"
#define MyAppPublisher "iisaacbeats.cn"
#define MyAppURL "https://iisaacbeats.cn"
#define MyAppCopyright "Copyright (C) 2026 iisaacbeats.cn"
#define MyPluginBundle "SpectrumTag.vst3"
#define MyAppExe "SpectrumTag.exe"

; Standalone（独立可执行程序）exe 的完整路径。
; build_installer.bat 探测到产物时会用 -DSTANDALONE_EXE=<完整路径> 传入；
; 未定义时安装包只包含 VST3 插件。
;#define STANDALONE_EXE "cmake-build-release-visual-studio\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe"

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
VersionInfoDescription={#MyAppName} VST3 Audio Plug-in Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
DefaultDirName={commoncf}\VST3\iisaacbeats.cn
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

; 有 Standalone 产物时：拆成"插件 / 独立程序"两个可选组件，
; 独立程序安装到 Program Files 下（不能塞进 VST3 目录），并给出开始菜单快捷方式。
#ifdef STANDALONE_EXE
[Components]
Name: "plugin"; Description: "{#MyAppName} VST3 plug-in"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "{#MyAppName} standalone application"; Types: full compact custom

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for the standalone app"; GroupDescription: "Additional icons:"; Flags: unchecked; Components: standalone

[Files]
Source: "{#VST3_DIR}\{#MyPluginBundle}\*"; DestDir: "{app}\{#MyPluginBundle}"; Components: plugin; Flags: ignoreversion recursesubdirs createallsubdirs
Source: {#STANDALONE_EXE}; DestDir: "{autopf}\{#MyAppPublisher}\{#MyAppName}"; DestName: "{#MyAppExe}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{autopf}\{#MyAppPublisher}\{#MyAppName}\{#MyAppExe}"; Components: standalone
Name: "{autodesktop}\{#MyAppName}"; Filename: "{autopf}\{#MyAppPublisher}\{#MyAppName}\{#MyAppExe}"; Components: standalone; Tasks: desktopicon
#else
[Files]
Source: "{#VST3_DIR}\{#MyPluginBundle}\*"; DestDir: "{app}\{#MyPluginBundle}"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Code]
var
  InstallDirWarningShown: Boolean;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  DefaultVst3Path: string;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    DefaultVst3Path := ExpandConstant('{commoncf}\VST3\iisaacbeats.cn');

    if (CompareText(RemoveBackslashUnlessRoot(WizardDirValue), RemoveBackslashUnlessRoot(DefaultVst3Path)) <> 0) and (not InstallDirWarningShown) then
    begin
      MsgBox('你选择了非默认VST3目录。安装完成后，你可能需要在宿主软件(DAW)中手动添加该目录并重新扫描插件，才能正常识别并使用本插件。', mbInformation, MB_OK);
      InstallDirWarningShown := True;
    end;
  end;
end;
#define MyAppName "SpectrumTag"
#define MyAppVersion "1.4.3"
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
; 注意：本 AppId 仍与旧版 pupon 相同（历史遗留）。pupon 已改用独立 AppId，
; 本安装包继续保持原 AppId，避免现有 SpectrumTag 用户升级链路断开。
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
; 目录页选择的是独立程序的安装位置；勾选 VST3 后会出现独立的 VST3 目录页，
; 默认安装到系统标准目录（{commoncf}\VST3\iisaacbeats.cn），也可自行指定。
[Components]
Name: "standalone"; Description: "{#MyAppName} standalone application"; Types: full compact custom; Flags: fixed
Name: "plugin"; Description: "{#MyAppName} VST3 plug-in"; Types: full compact custom

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for the standalone app"; GroupDescription: "Additional icons:"; Components: standalone

[InstallDelete]
; VST3：安装前删除旧 bundle，避免升级时残留旧文件
Type: filesandordirs; Name: "{code:GetVst3Dir}\{#MyPluginBundle}"; Components: plugin

[Files]
Source: {#STANDALONE_EXE}; DestDir: "{app}"; DestName: "{#MyAppExe}"; Components: standalone; Flags: ignoreversion
Source: "{#VST3_DIR}\{#MyPluginBundle}\*"; DestDir: "{code:GetVst3Dir}\{#MyPluginBundle}"; Components: plugin; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Components: standalone
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Components: standalone; Tasks: desktopicon

[Code]
var
  Vst3DirPage:         TInputDirWizardPage;
  Vst3DirWarningShown: Boolean;

function DefaultVst3Dir: string;
begin
  Result := ExpandConstant('{commoncf}\VST3\iisaacbeats.cn');
end;

// 供 [Files] / [InstallDelete] 使用的常量函数：code:GetVst3Dir
function GetVst3Dir(Param: string): string;
begin
  if (Vst3DirPage <> nil) and (Vst3DirPage.Values[0] <> '') then
    Result := Vst3DirPage.Values[0]
  else
    Result := DefaultVst3Dir;
end;

procedure InitializeWizard;
begin
  // VST3 目录页插在“选择组件”之后：只有勾选 plugin 组件时才会出现。
  Vst3DirPage := CreateInputDirPage(
    wpSelectComponents,
    'Select VST3 Install Location',
    'Where should the VST3 plug-in be installed?',
    'The VST3 plug-in will be installed into the following folder.' + #13#10 +
    'To continue, click Next. If you would like to select a different folder, click Browse.' + #13#10#13#10 +
    'Most DAWs automatically scan the default VST3 folder:' + #13#10 +
    '    ' + DefaultVst3Dir,
    False,   // AppendDir = False —— 不在用户选择的路径后追加 AppName
    ''       // NewFolderName —— 为空，不自动创建子目录
  );
  Vst3DirPage.Add('');
  Vst3DirPage.Values[0] := DefaultVst3Dir;
end;

// 未勾选 VST3 时跳过该页。
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (Vst3DirPage <> nil) and (PageID = Vst3DirPage.ID) then
    Result := not WizardIsComponentSelected('plugin');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ChosenPath: string;
begin
  Result := True;

  if (Vst3DirPage <> nil) and (CurPageID = Vst3DirPage.ID) then
  begin
    ChosenPath := Vst3DirPage.Values[0];

    if (CompareText(
          RemoveBackslashUnlessRoot(ChosenPath),
          RemoveBackslashUnlessRoot(DefaultVst3Dir)
        ) <> 0) and (not Vst3DirWarningShown) then
    begin
      MsgBox(
        'You have selected a non-default VST3 folder:' + #13#10 +
        ChosenPath + #13#10#13#10 +
        'After installation, you may need to manually add this folder to your DAW''s VST3 plug-in paths and rescan, otherwise the plug-in might not be detected.',
        mbInformation, MB_OK);
      Vst3DirWarningShown := True;
    end;
  end;
end;

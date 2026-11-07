#ifndef Version
  #define Version "1.4.20"
#endif
#ifndef Payload
  #error Define Payload as the staged Windows component directory
#endif
#ifndef Output
  #error Define Output as the installer output directory
#endif
[Setup]
; Preserve the upstream SpectrumTag installer identity for upgrades.
AppId={{0E3BF70B-5D5C-4F0F-B6E4-50F8C4B55C01}
AppName=SpectrumTag
AppVersion={#Version}
AppPublisher=iisaacbeats.cn / SpectrumTag contributors
AppPublisherURL=https://github.com/Ziqing-Gu/Spectrum-Tag-Private
DefaultDirName={autopf}\iisaacbeats.cn\SpectrumTag
DefaultGroupName=SpectrumTag
DisableProgramGroupPage=yes
LicenseFile={#Payload}\Docs\LICENSE.txt
OutputDir={#Output}
OutputBaseFilename=SpectrumTag-{#Version}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
UninstallDisplayIcon={app}\SpectrumTag.exe
CloseApplications=yes
RestartApplications=no
WizardStyle=modern
VersionInfoVersion={#Version}

[Types]
Name: "full"; Description: "All components"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "realtime"; Description: "SpectrumTag realtime VST3"; Types: full
Name: "ara"; Description: "SpectrumTag ARA VST3 extension"; Types: full
Name: "standalone"; Description: "SpectrumTag standalone audio/video editor"; Types: full

[Files]
Source: "{#Payload}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: NeedsVCRuntime
Source: "{#Payload}\SpectrumTag.vst3\*"; DestDir: "{commoncf64}\VST3\iisaacbeats.cn\SpectrumTag.vst3"; Components: realtime; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\SpectrumTag ARA.vst3\*"; DestDir: "{commoncf64}\VST3\iisaacbeats.cn\SpectrumTag ARA.vst3"; Components: ara; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\SpectrumTag.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#Payload}\Docs\*"; DestDir: "{app}\Docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\SpectrumTag"; Filename: "{app}\SpectrumTag.exe"; Components: standalone
Name: "{group}\Installation Guide"; Filename: "{app}\Docs\INSTALL-EN.txt"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Check: NeedsVCRuntime; Flags: waituntilterminated
Filename: "{app}\Docs\INSTALL-EN.txt"; Description: "Read installation and host setup notes"; Flags: postinstall shellexec skipifsilent unchecked

[Messages]
WelcomeLabel2=This installs SpectrumTag [name/ver] on your computer.%n%nSave your projects and fully quit all DAWs and SpectrumTag before continuing. The ARA component requires an ARA-capable host and its ARA integration workflow.

[Code]
function NeedsVCRuntime: Boolean;
var
  Installed, Major, Minor, Build: Cardinal;
  Key: String;
begin
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  Result := True;
  if RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) and (Installed = 1)
    and RegQueryDWordValue(HKLM64, Key, 'Major', Major)
    and RegQueryDWordValue(HKLM64, Key, 'Minor', Minor)
    and RegQueryDWordValue(HKLM64, Key, 'Bld', Build) then
    Result := (Major < 14) or ((Major = 14) and ((Minor < 44) or ((Minor = 44) and (Build < 35211))));
end;

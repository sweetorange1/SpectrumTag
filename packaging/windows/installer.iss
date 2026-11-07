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
AppId={{AC8A55A4-B01F-4BB0-B928-C36C92F3B108}
AppName=SpectrumTag
AppVersion={#Version}
AppPublisher=iisaacbeats.cn / SpectrumTag contributors
AppPublisherURL=https://github.com/Ziqing-Gu/SpectrumTag
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
Source: "{#Payload}\SpectrumTag.vst3\*"; DestDir: "{commoncf64}\VST3\iisaacbeats.cn\SpectrumTag.vst3"; Components: realtime; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\SpectrumTag ARA.vst3\*"; DestDir: "{commoncf64}\VST3\iisaacbeats.cn\SpectrumTag ARA.vst3"; Components: ara; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\SpectrumTag.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#Payload}\Docs\*"; DestDir: "{app}\Docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\SpectrumTag"; Filename: "{app}\SpectrumTag.exe"; Components: standalone
Name: "{group}\Installation Guide"; Filename: "{app}\Docs\INSTALL-EN.txt"

[Run]
Filename: "{app}\Docs\INSTALL-EN.txt"; Description: "Read installation and host setup notes"; Flags: postinstall shellexec skipifsilent unchecked

[Messages]
WelcomeLabel2=This installs SpectrumTag [name/ver] on your computer.%n%nSave your projects and fully quit all DAWs and SpectrumTag before continuing. ARA is an audio-event extension, not an ordinary insert effect.

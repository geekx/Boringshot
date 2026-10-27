; Boringshot installer. Built by CI:
;   iscc /DAppVersion=0.1.0 /DSourceDir=<install dir> /DOutputDir=<dir> packaging\boringshot.iss
; Per-user install, no admin, no VC++ Redist (static CRT).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\out"
#endif

[Setup]
AppId={{6B2F1E0C-8D4A-4C7E-9B1F-B0E1A6C35D21}
AppName=Boringshot
AppVersion={#AppVersion}
AppPublisher=geekx
DefaultDirName={localappdata}\Programs\Boringshot
DefaultGroupName=Boringshot
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Boringshot-{#AppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\boringshot.exe
CloseApplications=yes

[Tasks]
Name: "autostart"; Description: "开机自动启动 / Start with Windows"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\Boringshot"; Filename: "{app}\boringshot.exe"
Name: "{group}\Uninstall Boringshot"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "Boringshot"; ValueData: """{app}\boringshot.exe"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\boringshot.exe"; Description: "启动 Boringshot"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM boringshot.exe /F"; Flags: runhidden; RunOnceId: "KillBoringshot"

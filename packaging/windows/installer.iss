; Invoked by package.ps1 after deploying Qt and the app-local MSVC runtime.
#ifndef StageDir
  #error StageDir is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif

[Setup]
AppId={{ABFB989A-1FB7-46D0-B917-F670DF82CB69}
AppName=Arcade Wheel
AppVersion={#AppVersion}
AppPublisher=Arcade Wheel
AppPublisherURL=https://github.com/qa-p1/Arcade-wheel
DefaultDirName={localappdata}\Programs\Arcade Wheel
DefaultGroupName=Arcade Wheel
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=ArcadeWheel-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#StageDir}\LICENSE
UninstallDisplayIcon={app}\arcade-wheel.exe
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Arcade Wheel"; Filename: "{app}\arcade-wheel.exe"
Name: "{autodesktop}\Arcade Wheel"; Filename: "{app}\arcade-wheel.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\arcade-wheel.exe"; Description: "Open Arcade Wheel"; Flags: nowait postinstall skipifsilent

[Registry]
; Remove only our optional login entry on uninstall; preserve user configuration.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "ArcadeWheel"; Flags: uninsdeletevalue

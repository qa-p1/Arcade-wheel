; Wheel-specific identity and package output; common per-user integration is
; pinned in arcade.iss and arcade-integration.ps1 from Arcade Link.
#define ARCADE_ID "arcade.wheel"
#define ARCADE_NAME "Arcade Wheel"
#define ARCADE_VERSION AppVersion
#define ARCADE_EXE "arcade-wheel.exe"
#define ARCADE_CLI "arcade-wheel"
#define ARCADE_INSTALLER_ID "{{ABFB989A-1FB7-46D0-B917-F670DF82CB69}"
#define ARCADE_HOMEPAGE "https://github.com/qa-p1/Arcade-catalog"
#define ARCADE_START_AT_LOGIN_DEFAULT 0
#define ARCADE_CUSTOM_EVENTS
; Invoked by package.ps1 after Qt and the app-local MSVC runtime are deployed.
; arcade.iss ends inside [Code], so no ";" comments may follow the include
; before the next section header.
#include "arcade.iss"

#ifndef StageDir
  #error StageDir is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
OutputDir={#OutputDir}
OutputBaseFilename=ArcadeWheel-{#ARCADE_VERSION}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#StageDir}\LICENSE
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{app}\{#ARCADE_EXE}"; Description: "Open Arcade Wheel"; Flags: nowait postinstall skipifsilent

[Code]
function WheelArcadeRoot: String;
begin
  Result := GetEnv('ARCADE_HOME');
  if Result = '' then Result := ExpandConstant('{localappdata}\Arcade');
end;

procedure QuitRunningWheel;
var
  Exe: String;
  ExitCode: Integer;
begin
  Exe := ExpandConstant('{app}\{#ARCADE_EXE}');
  if FileExists(Exe) then
    Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    ArcadeIntegration(True);
    // Startup shortcuts replace the historical app-owned Run value.
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'ArcadeWheel');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Receipt: String;
  HadReceipt: Boolean;
begin
  if CurUninstallStep = usUninstall then begin
    Receipt := WheelArcadeRoot + '\installs\arcade.wheel.json';
    HadReceipt := FileExists(Receipt);
    // Exit 10 leaves a successor's receipt in place. Extra Wheel files may
    // be removed only when the shared helper proves this copy owned it.
    // [UninstallRun]/[UninstallDelete] Check functions run during setup,
    // before ownership is known, so this cleanup lives here instead.
    ArcadeIntegration(False);
    if HadReceipt and not FileExists(Receipt) then begin
      QuitRunningWheel;
      DeleteFile(WheelArcadeRoot + '\apps\arcade.wheel.json');
      RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'ArcadeWheel');
    end;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if ArcadeIntegrationFailed then Result := 1 else Result := 0;
end;

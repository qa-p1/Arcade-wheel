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
#include "arcade.iss"

; Invoked by package.ps1 after Qt and the app-local MSVC runtime are deployed.
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

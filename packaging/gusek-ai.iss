; Inno Setup script for GUSEK with local AI assistant
;
; Built by 'gusek.cmd installer' (see gusek.ps1)
; Requires Inno Setup 6 / 7 (ISCC.exe)

#ifndef AppVersion
  #define AppVersion "0.2.26-ai"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.2.26.0"
#endif
#ifndef StageDir
  #error Pass /DStageDir=<folder holding staged GUSEK files>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{5E4C7A33-89DF-4B4B-9689-5FDFBF23E3A1}
AppName=GUSEK AI
AppVersion={#AppVersion}
AppVerName=GUSEK AI {#AppVersion}
AppPublisher=GUSEK
AppComments=GUSEK with a local offline AI assistant for linear and integer programming coursework
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=GUSEK AI
PrivilegesRequired=lowest
DefaultDirName={autopf}\GUSEK AI
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#OutputDir}
OutputBaseFilename=gusek-ai-{#AppVersion}-setup
UninstallDisplayIcon={app}\gusek.exe
UninstallDisplayName=GUSEK AI {#AppVersion}
LicenseFile={#StageDir}\README
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "\ai\defaults\GusekAI.ini,\ai\defaults\system_prompt.txt"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\ai\defaults\GusekAI.ini"; DestDir: "{app}\ai\defaults"; Flags: onlyifdoesntexist
Source: "{#StageDir}\ai\defaults\system_prompt.txt"; DestDir: "{app}\ai\defaults"; Flags: onlyifdoesntexist

[Dirs]
Name: "{localappdata}\GusekAI\models"
Name: "{localappdata}\GusekAI\context"

[UninstallDelete]
Type: files; Name: "{localappdata}\GusekAI\models\*.gguf"
Type: files; Name: "{localappdata}\GusekAI\models\*.gguf.part"

[Icons]
Name: "{autoprograms}\GUSEK AI"; Filename: "{app}\gusek.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\GUSEK AI"; Filename: "{app}\gusek.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\gusek.exe"; WorkingDir: "{app}"; Description: "Launch GUSEK now"; Flags: postinstall nowait skipifsilent

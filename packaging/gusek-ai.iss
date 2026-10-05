; Inno Setup script for GUSEK with local AI assistant
;
; Built by 'gusek.cmd installer' (see gusek.ps1)
; Requires Inno Setup 6 / 7 (ISCC.exe)

#ifndef AppVersion
  #define AppVersion "0.2.26-ai"
#endif
#ifndef NumericVersion
  #error Pass /DNumericVersion=<four-part Windows version>
#endif
#ifndef AppIdValue
  #define AppIdValue "{{5E4C7A33-89DF-4B4B-9689-5FDFBF23E3A1}"
#endif
#ifndef AppName
  #define AppName "GUSEK AI"
#endif
#ifndef AiDataDir
  #define AiDataDir "{localappdata}\GusekAI"
#endif
#ifndef StageDir
  #error Pass /DStageDir=<folder holding staged GUSEK files>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={#AppIdValue}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=GUSEK
AppComments=GUSEK with a local offline AI assistant for linear and integer programming coursework
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=GUSEK AI
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#OutputDir}
OutputBaseFilename=gusek-ai-{#AppVersion}-setup
UninstallDisplayIcon={app}\gusek.exe
UninstallDisplayName={#AppName} {#AppVersion}
LicenseFile={#StageDir}\README
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "GusekAI.ini,system_prompt.txt,*.gguf,*.part,*.log"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\ai\defaults\GusekAI.ini"; DestDir: "{app}\ai\defaults"; Flags: onlyifdoesntexist
Source: "{#StageDir}\ai\defaults\system_prompt.txt"; DestDir: "{app}\ai\defaults"; Flags: onlyifdoesntexist

[Dirs]
Name: "{#AiDataDir}\models"
Name: "{#AiDataDir}\context"

[UninstallDelete]
; Only the pinned default assets belong to this distribution.
Type: files; Name: "{#AiDataDir}\models\Qwen3.5-4B-Q4_K_M.gguf"
Type: files; Name: "{#AiDataDir}\models\Qwen3.5-4B-Q4_K_M.gguf.part"
Type: files; Name: "{#AiDataDir}\models\Qwen3.5-4B-mmproj-F16.gguf"
Type: files; Name: "{#AiDataDir}\models\Qwen3.5-4B-mmproj-F16.gguf.part"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\gusek.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\gusek.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\gusek.exe"; WorkingDir: "{app}"; Description: "Launch GUSEK now"; Flags: postinstall nowait skipifsilent

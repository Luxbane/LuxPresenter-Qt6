; LuxPresenter-Qt6 Inno Setup Script
#define MyAppName "LuxPresenter"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "Luxbane"
#define MyAppURL "https://github.com/Luxbane/LuxPresenter"
#define MyAppExeName "LuxPresenter.exe"
#define SourceDir "..\Portable"

[Setup]
AppId={{D1A3961E-745B-4C6A-914F-94B5E8BC2901}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\Release
OutputBaseFilename=LuxPresenter_Setup
SetupIconFile=..\resources\app.ico
Compression=lzma2/fast
SolidCompression=no
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
Name: "{userdocs}\{#MyAppName}Qt6\Database"

[Files]
Source: "{#SourceDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\app.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\Data\settings.json"; DestDir: "{app}\Data"; Flags: onlyifdoesntexist
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,display_log.txt,*.tmp,*.log,Data\settings.json,Data\PresentationCache\*,Data\WebView2Data\*,Data\Thumbnails\*,Database\songs.json,*copilot*,edge_feedback\*"

[Icons]
Name: "{autoprograms}\{#MyAppName}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"
Name: "{autoprograms}\{#MyAppName}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autoprograms}\{#MyAppName}\Songs Database Folder"; Filename: "{userdocs}\{#MyAppName}Qt6\Database"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

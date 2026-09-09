#define MyAppName "Mezozoy"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "Luby Studio"
#define MyAppExeName "Mezozoy.exe"

[Setup]
AppId={{C121F128-FA6A-46A2-A0DB-1A385EACE1F6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=1.0.1.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Установщик сценарного редактора Mezozoy
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\Mezozoy.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=..\output
OutputBaseFilename=Mezozoy-{#MyAppVersion}-Setup
LicenseFile=..\LICENSE

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Дополнительные ярлыки:"; Flags: unchecked

[Files]
Source: "..\dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\examples\Horizon.mzoy"; DestDir: "{app}\examples"; Flags: ignoreversion

[Icons]
Name: "{group}\Mezozoy"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Удалить Mezozoy"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Mezozoy"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.mzoy"; ValueType: string; ValueName: ""; ValueData: "Mezozoy.Project"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Mezozoy.Project"; ValueType: string; ValueName: ""; ValueData: "Проект Mezozoy"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Mezozoy.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCU; Subkey: "Software\Classes\Mezozoy.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Запустить Mezozoy"; Flags: nowait postinstall skipifsilent

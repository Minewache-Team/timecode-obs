; Inno Setup Script for obs-ltc-timecode
; OBS Studio LTC Timecode Generator Plugin
;
; Usage (from project root):
;   Build for distribution (CI - uses release/ staging dir):
;     iscc /DMyAppVersion=0.1.0 /DConfiguration=Release installer\obs-ltc-timecode.iss
;
;   Build from local build output (developer):
;     iscc /DMyAppVersion=0.1.0 /DUseLocalBuild=1 installer\obs-ltc-timecode.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

#ifndef Configuration
  #define Configuration "Release"
#endif

#define MyAppName "OBS LTC Timecode Generator"
#define MyAppPublisher "Ferdmusic"
#define MyAppURL "https://github.com/Minewache-Team/timecode-obs"
#define PluginName "obs-ltc-timecode"

; Source paths: CI uses release/ staging, local builds use build_x64/ directly
#ifdef UseLocalBuild
  #define DllSource "..\build_x64\" + Configuration + "\" + PluginName + ".dll"
  #define LibltcDllSource "..\build_x64\" + Configuration + "\libltc.dll"
  #define DataSource "..\data\*"
#else
  #define DllSource "..\release\" + Configuration + "\" + PluginName + "\bin\64bit\" + PluginName + ".dll"
  #define LibltcDllSource "..\release\" + Configuration + "\" + PluginName + "\bin\64bit\libltc.dll"
  #define DataSource "..\release\" + Configuration + "\" + PluginName + "\data\*"
#endif

; MW OBS KIT template path
#define TemplateSource "..\[MW] OBS KIT"

[Setup]
AppId={{A7F3B2E1-9C4D-4E8F-B6A5-1D2E3F4A5B6C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableProgramGroupPage=yes
OutputDir=..\build_x64
OutputBaseFilename={#PluginName}-{#MyAppVersion}-windows-x64-setup
SetupIconFile=compiler:SetupClassicIcon.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
DisableDirPage=yes
UsePreviousAppDir=yes
CloseApplications=force
CloseApplicationsFilter=obs64.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
; Plugin DLL
Source: "{#DllSource}"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

; libltc shared library (LGPLv3 — dynamically linked for LGPL compliance)
Source: "{#LibltcDllSource}"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

; libltc license (LGPLv3 — required by LGPL Section 4a/4b)
Source: "..\deps\libltc\COPYING"; DestDir: "{app}\licenses\libltc"; DestName: "COPYING.LGPLv3"; Flags: ignoreversion

; Data files (locale etc.)
Source: "{#DataSource}"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

; MW OBS KIT Template - Scene Collection (only if not already customized by user)
; skipifsourcedoesntexist: template files are optional (not present in CI)
Source: "{#TemplateSource}\Minewache-New.json"; DestDir: "{userappdata}\obs-studio\basic\scenes"; Flags: onlyifdoesntexist skipifsourcedoesntexist

; MW OBS KIT Template - Profile (only if not already customized by user)
Source: "{#TemplateSource}\Minewache-New\*"; DestDir: "{userappdata}\obs-studio\basic\profiles\Minewache-New"; Flags: onlyifdoesntexist recursesubdirs createallsubdirs skipifsourcedoesntexist

[Dirs]
Name: "{userappdata}\obs-studio\basic\scenes"
Name: "{userappdata}\obs-studio\basic\profiles\Minewache-New"

[Code]
function InitializeSetup(): Boolean;
var
  ObsExePath: String;
begin
  Result := True;
  ObsExePath := ExpandConstant('{pf}\obs-studio\bin\64bit\obs64.exe');
  if not FileExists(ObsExePath) then
  begin
    if MsgBox('OBS Studio was not found in the default location.' + #13#10 +
              'The plugin requires OBS Studio (64-bit) to work.' + #13#10#13#10 +
              'Continue installation anyway?',
              mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    MsgBox('Installation complete!' + #13#10#13#10 +
           'To use the plugin:' + #13#10 +
           '  1. Start OBS Studio' + #13#10 +
           '  2. Scene Collection -> "Minewache-New"' + #13#10 +
           '  3. Profile -> "Minewache-New"' + #13#10 +
           '  4. LTC Timecode is pre-configured on Track 3' + #13#10#13#10 +
           'If OBS was running during installation, please restart it.',
           mbInformation, MB_OK);
  end;
end;

[Messages]
SetupWindowTitle=Setup - {#MyAppName} v{#MyAppVersion}
WelcomeLabel1=Welcome to the {#MyAppName} Setup
WelcomeLabel2=This will install the LTC Timecode Generator plugin for OBS Studio.%n%nThe plugin generates NTP-synchronized SMPTE LTC timecode audio for frame-accurate multi-camera synchronization.%n%nVersion: {#MyAppVersion}

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

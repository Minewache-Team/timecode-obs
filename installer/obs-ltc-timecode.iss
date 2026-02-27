; Inno Setup Script for obs-ltc-timecode
; OBS Studio LTC Timecode Generator Plugin
;
; Usage (from project root):
;   iscc /DMyAppVersion=0.1.0 /DConfiguration=RelWithDebInfo installer\obs-ltc-timecode.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

#ifndef Configuration
  #define Configuration "RelWithDebInfo"
#endif

#define MyAppName "OBS LTC Timecode Generator"
#define MyAppPublisher "Ferdmusic"
#define MyAppURL "https://github.com/Minewache-Team/timecode-obs"
#define PluginName "obs-ltc-timecode"

[Setup]
AppId={{A7F3B2E1-9C4D-4E8F-B6A5-1D2E3F4A5B6C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableProgramGroupPage=yes
OutputBaseFilename={#PluginName}-{#MyAppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
DisableDirPage=yes
UsePreviousAppDir=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Plugin DLL
Source: "..\release\{#Configuration}\{#PluginName}\bin\64bit\{#PluginName}.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

; Data files (locale etc.)
Source: "..\release\{#Configuration}\{#PluginName}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

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

[Messages]
SetupWindowTitle=Setup - {#MyAppName} v{#MyAppVersion}
WelcomeLabel1=Welcome to the {#MyAppName} Setup
WelcomeLabel2=This will install the LTC Timecode Generator plugin for OBS Studio.%n%nThe plugin generates SMPTE LTC timecode audio synchronized via NTP.%n%nVersion: {#MyAppVersion}

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

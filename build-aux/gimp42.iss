; gimp42.iss - Inno Setup script for the Windows installer.
;
; SPDX-License-Identifier: GPL-2.0-or-later
;
;     iscc /DAppVersion=1.0.4 /DSourceDir=dist build-aux\gimp42.iss
;
; SourceDir is the tree meson install and bundle-windows.sh made
; (bin\gimp42.exe, lib\gimp42 with the plug-ins, share\gimp42 with the
; brushes, patterns, palettes and gradients); the installer copies it
; whole and puts the GIMP on the Start menu.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

[Setup]
AppId={{3F6D2B61-9E0A-4C57-A1D2-6B42E0C5F142}
AppName=GIMP42
AppVersion={#AppVersion}
AppVerName=GIMP42 {#AppVersion}
AppPublisher=The gimp42 project
AppPublisherURL=https://github.com/office-42/gimp42
AppSupportURL=https://github.com/office-42/gimp42
DefaultDirName={autopf}\GIMP42
DefaultGroupName=GIMP42
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\COPYING
OutputDir=.
OutputBaseFilename=gimp42-{#AppVersion}-setup
UninstallDisplayIcon={app}\bin\gimp42.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assocxcf"; Description: "Open GIMP images (.xcf) with GIMP42"; GroupDescription: "File associations:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\GIMP42"; Filename: "{app}\bin\gimp42.exe"
Name: "{autodesktop}\GIMP42"; Filename: "{app}\bin\gimp42.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.xcf\OpenWithProgids"; ValueType: string; ValueName: "GIMP42.xcf"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocxcf
Root: HKA; Subkey: "Software\Classes\GIMP42.xcf"; ValueType: string; ValueName: ""; ValueData: "GIMP Image"; Flags: uninsdeletekey; Tasks: assocxcf
Root: HKA; Subkey: "Software\Classes\GIMP42.xcf\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\gimp42.exe,0"; Tasks: assocxcf
Root: HKA; Subkey: "Software\Classes\GIMP42.xcf\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\gimp42.exe"" ""%1"""; Tasks: assocxcf
Root: HKA; Subkey: "Software\Classes\Applications\gimp42.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\gimp42.exe"" ""%1"""; Flags: uninsdeletekey

[Run]
Filename: "{app}\bin\gimp42.exe"; Description: "{cm:LaunchProgram,GIMP42}"; Flags: nowait postinstall skipifsilent

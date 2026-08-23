; Inno Setup script for the Windows installer.
;
; Two components, because plenty of people want the plugin without the
; standalone. Both land in the folders a host actually scans, which are under
; Program Files, so the installer asks for administrator rights rather than
; failing quietly halfway through.

#ifndef Version
  #define Version "0.0.0"
#endif

#ifndef Artefacts
  #define Artefacts "..\..\build\Overtonium_artefacts\Release"
#endif

[Setup]
AppId={{8E5C1B94-3D7A-4A2E-9C61-0B2D5F7A4E13}
AppName=Overtonium
AppVersion={#Version}
AppVerName=Overtonium {#Version}
AppPublisher=Dehli Musikk
AppPublisherURL=https://www.dehlimusikk.no/
AppSupportURL=https://github.com/benjamindehli/overtonium
DefaultDirName={autopf}\Overtonium
DefaultGroupName=Overtonium
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputBaseFilename=Overtonium-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayName=Overtonium {#Version}

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom; \
  Flags: checkablealone
Name: "app"; Description: "Standalone application"; Types: full custom

[Files]
; The VST3 is a bundle on Windows too, a folder ending in .vst3, so it goes in
; whole rather than as a single file.
Source: "{#Artefacts}\VST3\Overtonium.vst3\*"; \
  DestDir: "{commoncf64}\VST3\Overtonium.vst3"; \
  Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#Artefacts}\Standalone\Overtonium.exe"; \
  DestDir: "{app}"; Components: app; Flags: ignoreversion

Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Overtonium"; Filename: "{app}\Overtonium.exe"; Components: app
Name: "{autodesktop}\Overtonium"; Filename: "{app}\Overtonium.exe"; \
  Components: app; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; \
  GroupDescription: "Shortcuts:"; Components: app; Flags: unchecked

[UninstallDelete]
; The VST3 bundle is copied in as a tree, so its folder has to go on the way
; out rather than being left behind empty.
Type: filesandordirs; Name: "{commoncf64}\VST3\Overtonium.vst3"

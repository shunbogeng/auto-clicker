; Inno Setup 安装包脚本（可选）。先跑 build_windows.ps1 生成 dist\AutoClicker 目录，
; 再用 ISCC 编译：iscc /DStageDir=dist\AutoClicker /Odist AutoClicker.iss
#define AppName "连点器 AutoClicker"
#ifndef StageDir
  #define StageDir "..\..\dist\AutoClicker"
#endif

[Setup]
AppId={{A9B3F1C7-2E4D-4B36-9A17-AUTOCLICKER}}
AppName={#AppName}
AppVersion=2.0.0
DefaultDirName={autopf}\AutoClicker
DefaultGroupName=AutoClicker
OutputBaseFilename=AutoClicker-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\AutoClicker.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\AutoClicker.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\AutoClicker.exe"; Description: "现在启动"; Flags: nowait postinstall skipifsilent

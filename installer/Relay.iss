; ============================================================
;  FileTransfer Relay Server - Inno Setup Installer Script
;  商业软件标准安装向导:路径选择/桌面快捷方式/注册表/卸载
; ============================================================

#define MyAppName      "FileTransfer 中继端"
#define MyAppExeName   "FileTransferRelay.exe"
#define MyAppPublisher "FileTransfer"
#define MyAppVersion   "1.0.0"
#define MyAppId        "FileTransferRelay_8F3A2C1D-5E6B-4C2D-9A1E-7F8B9C0D1E2F"

; ------------------------- Setup ----------------------------
[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=@INSTALLER_OUTPUT_DIR@
OutputBaseFilename=@INSTALLER_OUTPUT_BASENAME@
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
; 允许普通用户安装(无 UAC 也可以装到 appdata)
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ShowLanguageDialog=no
UninstallDisplayIcon={app}\{#MyAppExeName}
ChangesAssociations=no
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} 安装程序
VersionInfoProductName={#MyAppName}
; 安装程序自身的图标
SetupIconFile=@INSTALLER_SETUP_ICON@

; ------------------------ Languages -------------------------
[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

; ------------------------- Tasks ----------------------------
[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加图标:"; Flags: unchecked
Name: "autostart";   Description: "开机自动启动中继服务"; GroupDescription: "启动选项:"; Flags: unchecked

; ------------------------- Files ----------------------------
[Files]
Source: "{#SOURCEDIR}\{#MyAppExeName}";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#SOURCEDIR}\MSVCP140.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SOURCEDIR}\VCRUNTIME140.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#SOURCEDIR}\VCRUNTIME140_1.dll"; DestDir: "{app}"; Flags: ignoreversion

; ------------------------- Icons ----------------------------
[Icons]
Name: "{group}\{#MyAppName}";          Filename: "{app}\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}";     Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";    Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
; 开机自启(通过 Run 注册表项而不是启动文件夹,更标准)

; ------------------------ Registry --------------------------
[Registry]
; 安装路径记录
Root: HKCU; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletevalue createvalueifdoesntexist
; 开机自动启动(如果勾选了)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue createvalueifdoesntexist; Tasks: autostart

; ------------------------ Run / Post ------------------------
[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

; 卸载时删除程序自身没有创建的剩余文件/空目录
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

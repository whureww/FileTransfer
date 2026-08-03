; ============================================================
;  Silex Client - Inno Setup Installer Script
;  商业软件标准安装向导:路径选择/桌面快捷方式/注册表/卸载
; ============================================================

#define MyAppName      "Silex 客户端"
#define MyAppExeName   "Silex.exe"
#define MyAppPublisher "Silex"
#define MyAppVersion   "0.0.9"
#define MyAppId        "SilexClient_8F3A2C1D-5E6B-4C2D-9A1E-7F8B9C0D1E2F"

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
; Unicode 中文
ShowLanguageDialog=no
UninstallDisplayIcon={app}\{#MyAppExeName}
ChangesAssociations=no
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} 安装程序
VersionInfoProductName={#MyAppName}
; 安装程序自身的图标 (左上角、开始菜单、快捷方式)
SetupIconFile=@INSTALLER_SETUP_ICON@

; ------------------------ Languages -------------------------
[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

; ------------------------- Tasks ----------------------------
; 「创建桌面快捷方式」复选框
[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加图标:"; Flags: unchecked

; ------------------------- Files ----------------------------
; Source 路径在编译时通过 ISCC /D 传入:
;   /D "SOURCEDIR=D:\...\build"
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

; ------------------------ Registry --------------------------
[Registry]
; 写入安装路径到 HKCU(普通用户也能读写,不需要管理员权限)
Root: HKCU; Subkey: "Software\{#MyAppPublisher}\{#MyAppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletevalue createvalueifdoesntexist

; ------------------------ Run / Post ------------------------
[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

; 卸载时删除程序自身没有创建的剩余文件/空目录
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

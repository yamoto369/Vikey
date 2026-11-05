; Vikey for Windows installer (Inno Setup 6). Per-user, no admin prompt.
;   iscc /DVersion=1.0.14 /DArch=x64 /DSrc=..\build\App Vikey.iss
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef Src
  #define Src "..\build\App"
#endif

[Setup]
AppId={{7E2B5C1A-9D3F-4C7B-8E61-VIKEY0000001}
AppName=Vikey
AppVersion={#Version}
AppVerName=Vikey {#Version}
AppPublisher=Typing Technology Company Limited
AppPublisherURL=https://vikey.app
AppSupportURL=https://vikey.app/faq
VersionInfoVersion={#Version}
DefaultDirName={localappdata}\Programs\Vikey
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=.\out
OutputBaseFilename=Vikey-{#Version}-{#Arch}-Setup
SetupIconFile=..\App\Vikey.ico
UninstallDisplayIcon={app}\Vikey.exe
UninstallDisplayName=Vikey
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=Vikey.exe
RestartApplications=no
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
; the few screens people see, in Vietnamese like the app
WelcomeLabel1=Cài đặt Vikey
WelcomeLabel2=Vikey {#Version} sẽ được cài cho tài khoản của bạn, không cần quyền quản trị.%n%nBấm Next để tiếp tục.
FinishedHeadingLabel=Đã cài xong Vikey
FinishedLabel=Vikey nằm ở khay hệ thống (góc dưới bên phải). Bấm Alt+Z để chuyển Anh/Việt.
ConfirmUninstall=Gỡ Vikey khỏi máy?

[Files]
Source: "{#Src}\Vikey.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Vikey"; Filename: "{app}\Vikey.exe"

[Registry]
; the app's own "Mở Vikey khi đăng nhập" entry goes away with the app
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "Vikey"; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\Vikey.exe"; Description: "Mở Vikey"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM Vikey.exe /F"; Flags: runhidden; RunOnceId: "KillVikey"


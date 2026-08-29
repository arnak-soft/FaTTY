; Inno Setup 6 — установщик FaTTY (обёртка над Portable из build.bat).
; Версию и имя папки передаёт build.bat через /D...

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#ifndef MyVersionInfo
  #define MyVersionInfo "0.0.0.0"
#endif
#ifndef PortableDirName
  #define PortableDirName "FaTTY 0.0.0-dev Portable"
#endif

#define MyAppName "FaTTY"
#define MyAppExeName "FaTTY.exe"

[Setup]
AppId={{8C4E9A2F-7B1D-4F6A-9E3C-2D5B8A0F1C47}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyVersionInfo}
VersionInfoProductVersion={#MyVersionInfo}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=FaTTY {#MyAppVersion} Setup
SetupIconFile=assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; Сами закрываем FaTTY в [Code] (PrepareToInstall): иначе Inno показывает
; «Закройте приложение», а UIPI не даёт elevated Setup послать WM_CLOSE.
CloseApplications=no
RestartApplications=no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.CloseFaTTYBusy=A command or file transfer is still running. It will be interrupted.
russian.CloseFaTTYBusy=Выполняется команда или передача файла. Она будет прервана.
english.CloseFaTTYModal=An editor window is open. Unsaved changes will be lost.
russian.CloseFaTTYModal=Открыто окно редактирования. Несохранённые изменения будут потеряны.
english.CloseFaTTYConfirm=Close FaTTY and continue setup?
russian.CloseFaTTYConfirm=Закрыть FaTTY и продолжить установку?
english.CloseFaTTYOld=FaTTY is running and will be closed. A running command will be interrupted, and unsaved edits will be lost.%n%nContinue?
russian.CloseFaTTYOld=FaTTY запущен и будет закрыт. Текущая команда прервётся, несохранённые правки в открытых окнах пропадут.%n%nПродолжить?
english.CloseFaTTYRetry=FaTTY is still running.%n%nWait again?
russian.CloseFaTTYRetry=FaTTY всё ещё запущен.%n%nПодождать ещё?
english.CloseFaTTYFailed=Setup cannot continue while FaTTY is running.
russian.CloseFaTTYFailed=Установка не может продолжаться, пока запущен FaTTY.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "dist\{#PortableDirName}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  SYNCHRONIZE = $00100000;
  EVENT_MODIFY_STATE = $0002;
  WAIT_OBJECT_0 = 0;
  MUTEX_NAME = 'Local\FaTTY.SingleInstance';
  CLOSE_EVENT = 'Local\FaTTY.CloseForInstall';
  BUSY_EVENT = 'Local\FaTTY.BusyWork';
  MODAL_EVENT = 'Local\FaTTY.OpenDialog';

function OpenEvent(dwDesiredAccess: LongWord; bInheritHandle: LongBool; lpName: String): THandle;
  external 'OpenEventW@kernel32.dll stdcall';
function SetEvent(hEvent: THandle): LongBool;
  external 'SetEvent@kernel32.dll stdcall';
function CloseHandle(hObject: THandle): LongBool;
  external 'CloseHandle@kernel32.dll stdcall';
function WaitForSingleObject(hHandle: THandle; dwMilliseconds: LongWord): LongWord;
  external 'WaitForSingleObject@kernel32.dll stdcall';

function NamedEventExists(const Name: String): Boolean;
var
  h: THandle;
begin
  h := OpenEvent(SYNCHRONIZE, False, Name);
  Result := h <> 0;
  if h <> 0 then
    CloseHandle(h);
end;

function NamedEventSignaled(const Name: String): Boolean;
var
  h: THandle;
begin
  Result := False;
  h := OpenEvent(SYNCHRONIZE, False, Name);
  if h <> 0 then begin
    Result := WaitForSingleObject(h, 0) = WAIT_OBJECT_0;
    CloseHandle(h);
  end;
end;

function SignalNamedEvent(const Name: String): Boolean;
var
  h: THandle;
begin
  Result := False;
  h := OpenEvent(SYNCHRONIZE or EVENT_MODIFY_STATE, False, Name);
  if h <> 0 then begin
    Result := SetEvent(h);
    CloseHandle(h);
  end;
end;

function WaitUntilFaTTYClosed(TimeoutMs: Integer): Boolean;
var
  waited: Integer;
begin
  waited := 0;
  while CheckForMutexes(MUTEX_NAME) do begin
    if waited >= TimeoutMs then begin
      Result := False;
      Exit;
    end;
    Sleep(200);
    waited := waited + 200;
  end;
  Result := True;
end;

function ForceKillFaTTY: Boolean;
var
  rc: Integer;
begin
  Result := Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM FaTTY.exe', '', SW_HIDE,
                 ewWaitUntilTerminated, rc);
end;

function ConfirmAndCloseFaTTY: String;
var
  s: String;
  busy, modal, hasIpc: Boolean;
begin
  Result := '';
  if not CheckForMutexes(MUTEX_NAME) then
    Exit;

  hasIpc := NamedEventExists(CLOSE_EVENT);
  busy := NamedEventSignaled(BUSY_EVENT);
  modal := NamedEventSignaled(MODAL_EVENT);

  if not WizardSilent then begin
    if not hasIpc then begin
      if MsgBox(ExpandConstant('{cm:CloseFaTTYOld}'), mbConfirmation, MB_YESNO) <> IDYES then begin
        Result := ExpandConstant('{cm:CloseFaTTYFailed}');
        Exit;
      end;
    end else if busy or modal then begin
      s := '';
      if busy then
        s := ExpandConstant('{cm:CloseFaTTYBusy}');
      if modal then begin
        if s <> '' then
          s := s + #13#10#13#10;
        s := s + ExpandConstant('{cm:CloseFaTTYModal}');
      end;
      s := s + #13#10#13#10 + ExpandConstant('{cm:CloseFaTTYConfirm}');
      if MsgBox(s, mbConfirmation, MB_YESNO) <> IDYES then begin
        Result := ExpandConstant('{cm:CloseFaTTYFailed}');
        Exit;
      end;
    end;
  end;

  if hasIpc then
    SignalNamedEvent(CLOSE_EVENT)
  else
    ForceKillFaTTY;

  if WaitUntilFaTTYClosed(30000) then
    Exit;

  if (not WizardSilent) and
     (MsgBox(ExpandConstant('{cm:CloseFaTTYRetry}'), mbConfirmation, MB_RETRYCANCEL) = IDRETRY) then begin
    if hasIpc then
      SignalNamedEvent(CLOSE_EVENT);
    ForceKillFaTTY;
    if WaitUntilFaTTYClosed(15000) then
      Exit;
  end else if WizardSilent then begin
    ForceKillFaTTY;
    if WaitUntilFaTTYClosed(10000) then
      Exit;
  end;
  Result := ExpandConstant('{cm:CloseFaTTYFailed}');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  Result := ConfirmAndCloseFaTTY;
end;

function InitializeUninstall(): Boolean;
var
  err: String;
begin
  err := ConfirmAndCloseFaTTY;
  if err <> '' then begin
    MsgBox(err, mbError, MB_OK);
    Result := False;
  end else
    Result := True;
end;

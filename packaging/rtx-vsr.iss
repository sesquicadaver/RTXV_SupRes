; RTX VSR — один інсталятор Inno Setup для Windows 11 x64.
; Підпис релізу: розкоментуйте SignTool і підпишіть setup.exe. Цей файл підпис не імітує.
; Порти брандмауера не відкриваються. Правило, якщо знадобиться, лише для rtx-vsr-proxy.exe
; і конкретного режиму прийому.
; VC++ Runtime і драйвер NVIDIA цим деінсталятором не знімаються.
; Профілі лежать у каталозі користувача і в {app} не входять.

#ifndef Dist
  #define Dist "dist\windows"
#endif

#define AppName "RTX VSR"
#define AppExeName "rtx-vsr-gui.exe"

[Setup]
AppId={{8F3A1C2E-6B47-4D91-9E5A-2C7B4F8D1A60}
AppName={#AppName}
AppVersion=0.1.0
AppPublisher=RTXV SupRes
DefaultDirName={autopf}\RTX VSR
DefaultGroupName={#AppName}
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0.22000
PrivilegesRequired=admin
OutputDir=output
OutputBaseFilename=rtx-vsr-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
LicenseFile=NOTICES.txt
CloseApplications=no
RestartApplications=no
UsePreviousAppDir=yes
SetupMutex=RTX_VSR_SETUP
UninstallDisplayIcon={app}\{#AppExeName}
; SignTool=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $f

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "vc_redist.x64.exe,nvidia-driver.exe,rtx-vsr-precheck.exe,components.json"
Source: "{#Dist}\rtx-vsr-precheck.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\rtx-vsr-precheck.exe"; DestDir: "{tmp}"; Flags: dontcopy
Source: "{#Dist}\components.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\components.json"; DestDir: "{tmp}"; Flags: dontcopy
Source: "{#Dist}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: dontcopy skipifsourcedoesntexist
Source: "{#Dist}\nvidia-driver.exe"; DestDir: "{tmp}"; Flags: dontcopy skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Ярлик на робочому столі"; GroupDescription: "Ярлики:"; Flags: unchecked

[Code]

function DecodeJson(const Text: String): String;
var
  Index, CodePoint: Integer;
  Hex: String;
begin
  Result := '';
  Index := 1;
  while Index <= Length(Text) do
  begin
    if (Text[Index] = '\') and (Index < Length(Text)) then
    begin
      if (Text[Index + 1] = 'u') and (Index + 5 <= Length(Text)) then
      begin
        Hex := Copy(Text, Index + 2, 4);
        CodePoint := StrToInt('$' + Hex);
        Result := Result + Chr(CodePoint);
        Index := Index + 6;
      end
      else if Text[Index + 1] = 'n' then
      begin
        Result := Result + #10;
        Index := Index + 2;
      end
      else
      begin
        Result := Result + Text[Index + 1];
        Index := Index + 2;
      end;
    end
    else
    begin
      Result := Result + Text[Index];
      Index := Index + 1;
    end;
  end;
end;

function JsonString(const Text, Key: String): String;
var
  Marker, StartAt, Index: Integer;
begin
  Result := '';
  Marker := Pos('"' + Key + '":"', Text);
  if Marker = 0 then Exit;
  StartAt := Marker + Length(Key) + 4;
  Index := StartAt;
  while Index <= Length(Text) do
  begin
    if (Text[Index] = '\') and (Index < Length(Text)) and (Text[Index + 1] = 'u') then
      Index := Index + 6
    else if Text[Index] = '\' then
      Index := Index + 2
    else if Text[Index] = '"' then
      Break
    else
      Index := Index + 1;
  end;
  Result := DecodeJson(Copy(Text, StartAt, Index - StartAt));
end;

function TryExtract(const Name: String): Boolean;
begin
  try
    ExtractTemporaryFile(Name);
    Result := True;
  except
    Result := False;
  end;
end;

function SignatureValid(const Path: String): Boolean;
var
  Code: Integer;
begin
  Result := False;
  if not FileExists(Path) then Exit;
  if Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      '-NoProfile -Command "if ((Get-AuthenticodeSignature -FilePath ''' + Path + ''').Status -eq ''Valid'') { exit 0 } else { exit 1 }"',
      '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Result := Code = 0;
end;

function CaptureHash(const Path: String): String;
var
  Code: Integer;
  Output: String;
begin
  Result := '';
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      '-NoProfile -Command "(Get-FileHash -Algorithm SHA256 -Path ''' + Path + ''').Hash.ToLowerInvariant() | Set-Content -NoNewline ''' + ExpandConstant('{tmp}\hash-only.txt') + '''"',
      '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Exit;
  if (Code = 0) and LoadStringFromFile(ExpandConstant('{tmp}\hash-only.txt'), Output) then
    Result := Lowercase(Trim(Output));
end;

function CopyTree(const FromDir, ToDir: String): Boolean;
var
  Code: Integer;
begin
  ForceDirectories(ToDir);
  Exec(ExpandConstant('{cmd}'), '/C robocopy "' + FromDir + '" "' + ToDir + '" /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1',
    '', SW_HIDE, ewWaitUntilTerminated, Code);
  Result := Code < 8;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
  Report, Message, RuntimeChoice, DriverChoice, VcName, VcHash, DriverName, DriverUrl, DriverHash, Saved: String;
  VcPath, DriverPath, Download: String;
begin
  Result := '';
  NeedsRestart := False;
  if not TryExtract('rtx-vsr-precheck.exe') or not TryExtract('components.json') then
  begin
    Result := 'у пакеті немає модуля перевірки';
    Exit;
  end;
  Report := '';
  if not Exec(ExpandConstant('{tmp}\rtx-vsr-precheck.exe'),
      '--manifest "' + ExpandConstant('{tmp}\components.json') + '" --out "' + ExpandConstant('{tmp}\precheck.json') + '"',
      '', SW_HIDE, ewWaitUntilTerminated, Code) then
  begin
    Result := 'модуль перевірки не запустився';
    Exit;
  end;
  if not LoadStringFromFile(ExpandConstant('{tmp}\precheck.json'), Report) then
  begin
    Result := 'немає результату перевірки';
    Exit;
  end;
  Message := JsonString(Report, 'message');
  if Pos('"proceed":true', Report) = 0 then
  begin
    Result := Message;
    Exit;
  end;
  RuntimeChoice := JsonString(Report, 'runtime');
  DriverChoice := JsonString(Report, 'driver');
  if RuntimeChoice = 'install' then
  begin
    VcName := JsonString(Report, 'vc_file');
    VcHash := JsonString(Report, 'vc_sha256');
    if not TryExtract(VcName) then
    begin
      Result := 'у пакеті немає офіційного VC++ Runtime';
      Exit;
    end;
    VcPath := ExpandConstant('{tmp}\') + VcName;
    Saved := CaptureHash(VcPath);
    if (Saved = '') or (Saved <> Lowercase(VcHash)) or (not SignatureValid(VcPath)) then
    begin
      Result := 'VC++ Runtime не збігся з підписом або контрольною сумою';
      Exit;
    end;
    if not Exec(VcPath, '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    begin
      Result := 'VC++ Runtime не запустився';
      Exit;
    end;
    if (Code <> 0) and (Code <> 1638) and (Code <> 3010) then
    begin
      Result := 'VC++ Runtime завершився з помилкою';
      Exit;
    end;
    if Code = 3010 then
    begin
      NeedsRestart := True;
      Exit;
    end;
  end;
  if DriverChoice = 'install' then
  begin
    DriverName := JsonString(Report, 'driver_file');
    DriverUrl := JsonString(Report, 'driver_url');
    DriverHash := JsonString(Report, 'driver_sha256');
    DriverPath := '';
    if DriverName <> '' then
    begin
      if not TryExtract(DriverName) then
      begin
        Result := 'у пакеті немає офіційного драйвера NVIDIA';
        Exit;
      end;
      DriverPath := ExpandConstant('{tmp}\') + DriverName;
    end
    else if (Length(DriverUrl) > 8) and (Copy(DriverUrl, 1, 8) = 'https://') then
    begin
      DriverPath := ExpandConstant('{tmp}\nvidia-driver.exe');
      Download := '-NoProfile -Command "Invoke-WebRequest -Uri ''' + DriverUrl + ''' -OutFile ''' + DriverPath + '''"';
      if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Download, '', SW_SHOW, ewWaitUntilTerminated, Code) or (Code <> 0) then
      begin
        Result := 'офіційний драйвер не завантажено';
        Exit;
      end;
    end
    else
    begin
      Result := 'немає локального пакета чи офіційної адреси драйвера';
      Exit;
    end;
    Saved := CaptureHash(DriverPath);
    if (Saved = '') or (Saved <> Lowercase(DriverHash)) or (not SignatureValid(DriverPath)) then
    begin
      Result := 'драйвер NVIDIA не збігся з підписом або контрольною сумою';
      Exit;
    end;
    if not Exec(DriverPath, '/s /noreboot', '', SW_SHOW, ewWaitUntilTerminated, Code) then
    begin
      Result := 'інсталятор драйвера NVIDIA не запустився';
      Exit;
    end;
    if (Code <> 0) and (Code <> 3010) then
    begin
      Result := 'інсталятор драйвера NVIDIA завершився з помилкою';
      Exit;
    end;
    if Code = 3010 then
      NeedsRestart := True;
  end;
  if not NeedsRestart then
  begin
    if DirExists(ExpandConstant('{app}')) then
    begin
      if not CopyTree(ExpandConstant('{app}'), ExpandConstant('{commonappdata}\RTX VSR\previous')) then
        Result := 'не вдалося зберегти попередню версію';
    end;
  end;
end;

procedure RestorePrevious;
var
  Previous: String;
  Code: Integer;
begin
  Previous := ExpandConstant('{commonappdata}\RTX VSR\previous');
  if DirExists(Previous) then
  begin
    CopyTree(Previous, ExpandConstant('{app}'));
    MsgBox('Перевірку встановлення не пройдено. Повернуто попередні файли програми. Профілі не змінювались.', mbError, MB_OK);
  end
  else if FileExists(ExpandConstant('{uninstallexe}')) then
  begin
    Exec(ExpandConstant('{uninstallexe}'), '/VERYSILENT /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code);
    MsgBox('Перевірку встановлення не пройдено. Файли програми прибрано. VC++ Runtime і драйвер NVIDIA не чіпались.', mbError, MB_OK);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  Clip: String;
begin
  if CurStep <> ssPostInstall then Exit;
  if not Exec(ExpandConstant('{app}\rtx-vsr-proxy.exe'), 'doctor --json', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
  begin
    RestorePrevious;
    Exit;
  end;
  Clip := ExpandConstant('{app}\install-check.h264');
  if not Exec(ExpandConstant('{app}\rtx-vsr-proxy.exe'), 'install-check --clip "' + Clip + '"', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    RestorePrevious;
end;

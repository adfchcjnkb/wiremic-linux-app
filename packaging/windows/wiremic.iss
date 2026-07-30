; WireMic installer (Inno Setup)
;
; Bundles VB-CABLE and installs it silently, which VB-Audio permits provided
; the end user can identify it as their product and is in a position to
; donate. The attribution page below exists to satisfy that condition -- do
; not remove it.

#define AppName        "WireMic"
#define AppVersion     GetEnv('WIREMIC_VERSION')
#if AppVersion == ""
  #define AppVersion   "1.0.0"
#endif
#define AppPublisher   "Arvinkheradmand"
#define AppExeName     "wiremic.exe"
#define AppUrl         "https://github.com/adfchcjnkb/wiremic-linux-app"

[Setup]
AppId={{9C4B2F7A-3E51-4A6D-9B2C-7F1D8A5E3C40}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}
VersionInfoCompany={#AppPublisher}
VersionInfoProductName={#AppName}
VersionInfoVersion={#AppVersion}

; Installing a driver needs administrator rights.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableDirPage=no
DisableProgramGroupPage=no
AllowNoIcons=yes

OutputDir=..\..\dist
OutputBaseFilename=WireMic-{#AppVersion}-Setup
SetupIconFile=..\icons\wiremic.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

; Windows 10 1809 and later.
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "installcable"; Description: "Install the VB-CABLE virtual audio device (required for the virtual microphone)"; GroupDescription: "Virtual microphone:"; Check: not CableAlreadyInstalled

[Files]
Source: "..\..\dist\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\icons\wiremic.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "vbcable\*"; DestDir: "{tmp}\vbcable"; Flags: deleteafterinstall recursesubdirs createallsubdirs; Tasks: installcable

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\wiremic.ico"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\wiremic.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  CablePage: TOutputMsgMemoWizardPage;

procedure InitializeWizard;
begin
  { VB-Audio's licence requires the user to be told what VB-CABLE is, where it
    comes from, and that it is donationware. }
  CablePage := CreateOutputMsgMemoPage(
    wpLicense,
    'Third-party component',
    'WireMic installs VB-CABLE',
    'The virtual microphone is provided by VB-CABLE, a separate product by VB-Audio Software.',
    'VB-CABLE Virtual Audio Device' + #13#10 +
    'Copyright VB-Audio Software' + #13#10 +
    'Origin: https://vb-cable.com' + #13#10 + #13#10 +
    'VB-CABLE is DONATIONWARE. It may be used free of charge, and VB-Audio ' +
    'invites every user who finds it useful to donate a licence fee on their ' +
    'website. WireMic bundles it with VB-Audio''s permission; WireMic is not ' +
    'affiliated with VB-Audio.' + #13#10 + #13#10 +
    'After installation, WireMic feeds audio into "CABLE Input", and any ' +
    'application can select "CABLE Output" as its microphone. It appears in ' +
    'the Windows Sound control panel (mmsys.cpl) like any other device, and ' +
    'WireMic can set it as the default input for you with one button.');
end;

function CableAlreadyInstalled(): Boolean;
var
  Names: TArrayOfString;
  I: Integer;
  Display: String;
begin
  Result := False;
  if RegGetSubkeyNames(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', Names) then
  begin
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      if RegQueryStringValue(HKLM,
           'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Names[I],
           'DisplayName', Display) then
      begin
        if Pos('VB-CABLE', Display) > 0 then
        begin
          Result := True;
          Exit;
        end;
      end;
    end;
  end;
end;

procedure InstallCable();
var
  Installer: String;
  ResultCode: Integer;
begin
  { Re-running the driver installer over a working install can leave the
    endpoint in a half-configured state, so never touch an existing one. }
  if CableAlreadyInstalled() then
  begin
    Log('VB-CABLE already present; leaving it alone.');
    Exit;
  end;

  { VB-CABLE ships separate 32/64-bit setup binaries; -i installs, -h runs it
    without its own UI so our progress page stays in charge. }
  Installer := ExpandConstant('{tmp}\vbcable\VBCABLE_Setup_x64.exe');
  if not FileExists(Installer) then
    Installer := ExpandConstant('{tmp}\vbcable\VBCABLE_Setup.exe');

  if not FileExists(Installer) then
  begin
    MsgBox('The VB-CABLE installer was not bundled with this build.' + #13#10 +
           'You can install it later from https://vb-cable.com and WireMic ' +
           'will pick it up automatically.', mbInformation, MB_OK);
    Exit;
  end;

  WizardForm.StatusLabel.Caption := 'Installing the VB-CABLE virtual audio device...';
  if not Exec(Installer, '-i -h', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    MsgBox('The VB-CABLE installer could not be started.' + #13#10 + #13#10 +
           'WireMic is installed and will work as soon as VB-CABLE is present.' +
           ' You can install it yourself from https://vb-cable.com -- WireMic ' +
           'detects it automatically, no reconfiguration needed.',
           mbError, MB_OK);
    Exit;
  end;

  { VB-CABLE returns a non-zero code when it needs a reboot to finish binding
    the endpoint. That is not a failure, but the user has to know. }
  if ResultCode <> 0 then
  begin
    MsgBox('VB-CABLE was installed but Windows needs a restart before the ' +
           'virtual microphone appears.' + #13#10 + #13#10 +
           'Restart when convenient, then open WireMic.',
           mbInformation, MB_OK);
  end;
end;

{ Windows Firewall blocks unsolicited inbound datagrams for any program that
  has no rule, and on a network Windows has classified as Public it blocks them
  without ever prompting. Discovery is unsolicited inbound UDP by definition, so
  without these rules the phone broadcasts into a void: it can hear the PC but
  the PC never hears the phone, and neither side can open the control channel.
  Setup runs elevated, which is the one moment we can add the rules silently. }
procedure RemoveFirewallRules();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\netsh.exe'),
       'advfirewall firewall delete rule name="WireMic (UDP-In)"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\netsh.exe'),
       'advfirewall firewall delete rule name="WireMic (TCP-In)"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure AddFirewallRules();
var
  Target: String;
  ResultCode: Integer;
  UdpOk, TcpOk: Boolean;
begin
  Target := ExpandConstant('{app}\{#AppExeName}');

  { Reinstalling must not stack duplicate rules, and a rule left over from an
    install at a different path would not match this executable. }
  RemoveFirewallRules();

  UdpOk := Exec(ExpandConstant('{sys}\netsh.exe'),
    'advfirewall firewall add rule name="WireMic (UDP-In)" dir=in action=allow'
    + ' program="' + Target + '" protocol=UDP profile=any enable=yes',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);

  TcpOk := Exec(ExpandConstant('{sys}\netsh.exe'),
    'advfirewall firewall add rule name="WireMic (TCP-In)" dir=in action=allow'
    + ' program="' + Target + '" protocol=TCP profile=any enable=yes',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);

  if not (UdpOk and TcpOk) then
  begin
    MsgBox('WireMic could not add its Windows Firewall rules automatically.'
           + #13#10 + #13#10 +
           'Without them your phone will not be able to find this computer. '
           + 'Open WireMic and use "Repair network permissions" on the '
           + 'Settings page, or allow ' + Target + ' through the firewall for '
           + 'private and public networks.', mbError, MB_OK);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    AddFirewallRules();
    if WizardIsTaskSelected('installcable') and not CableAlreadyInstalled() then
      InstallCable();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { Leaving firewall holes behind for an executable that no longer exists is
    untidy at best. VB-CABLE is left installed on purpose: the user may have
    had it before WireMic, and other applications may now depend on it. }
  if CurUninstallStep = usUninstall then
    RemoveFirewallRules();
end;

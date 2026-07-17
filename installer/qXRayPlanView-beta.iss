#define AppVersion "0.1.0-beta.1"
#define TargetName "CloudCompare-2.14-beta-c7d5bb7-Windows-x64"
#define PluginDll "QXRAYPLANVIEW_PLUGIN.dll"
#ifndef ArtifactRoot
#define ArtifactRoot "..\..\..\..\..\..\..\release-artifacts"
#endif

[Setup]
AppId={{7E0E0707-B77B-4B6C-8E67-8B3E2C1E209B}
AppName=qXRayPlanView Plugin
AppVersion={#AppVersion}
AppPublisher=marceamendola
AppPublisherURL=https://github.com/marceamendola/cloudcompare-xray-planview
AppSupportURL=https://github.com/marceamendola/cloudcompare-xray-planview/issues
AppUpdatesURL=https://github.com/marceamendola/cloudcompare-xray-planview/releases
DefaultDirName={autopf}\CloudCompare
DisableProgramGroupPage=yes
DisableReadyMemo=no
OutputDir={#ArtifactRoot}
OutputBaseFilename=qXRayPlanView-{#TargetName}-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
SetupLogging=yes
UninstallDisplayName=qXRayPlanView Plugin ({#TargetName})

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#ArtifactRoot}\qXRayPlanView-{#TargetName}\{#PluginDll}"; DestDir: "{app}\plugins"; Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{app}\plugins\{#PluginDll}"

[Code]
function CloudCompareExePath(): String;
begin
  Result := ExpandConstant('{app}\CloudCompare.exe');
end;

function PluginPath(): String;
begin
  Result := ExpandConstant('{app}\plugins\{#PluginDll}');
end;

function BackupPath(): String;
begin
  Result := ExpandConstant('{app}\plugins\{#PluginDll}.backup');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    if not FileExists(CloudCompareExePath()) then
    begin
      MsgBox('Selecciona la carpeta donde esta CloudCompare.exe.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';

  if not DirExists(ExpandConstant('{app}\plugins')) then
  begin
    if not CreateDir(ExpandConstant('{app}\plugins')) then
    begin
      Result := 'No se pudo crear la carpeta plugins dentro de CloudCompare.';
      Exit;
    end;
  end;

  if FileExists(PluginPath()) then
  begin
    if FileExists(BackupPath()) then
      DeleteFile(BackupPath());

    if not RenameFile(PluginPath(), BackupPath()) then
    begin
      Result := 'No se pudo reemplazar el plugin. Cierra CloudCompare y vuelve a intentar.';
      Exit;
    end;
  end;
end;

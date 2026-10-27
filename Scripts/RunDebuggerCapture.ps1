param([string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7')
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$Script=Join-Path $PSScriptRoot 'CaptureDebugger.py'
$ArgsList=@("`"$ProjectRoot\NiagaraHerd.uproject`"",'-windowed','-unattended','-nosound',"-ExecCmds=`"py $($Script.Replace('\','/'))`"","-abslog=`"$ProjectRoot\Saved\Logs\Niagara-Debugger-Final.log`"")
$Run=Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $ArgsList -WindowStyle Hidden -Wait -PassThru
if($Run.ExitCode -ne 0) { throw "Debugger capture failed: $($Run.ExitCode)" }
foreach($Name in @('Niagara-Debugger-GPU.png','Niagara-Debugger-CPU.png')) { Join-Path "$ProjectRoot\Saved\Screenshots" $Name }

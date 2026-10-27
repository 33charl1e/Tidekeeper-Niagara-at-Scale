param([string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7')
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$ProjectRoot\NiagaraHerd.uproject" -run=BuildHerdAssets -unattended -nosplash -nosound -AllowCommandletRendering -d3d12 "-abslog=$ProjectRoot\Saved\Logs\BuildHerdAssets.log"
if($LASTEXITCODE -ne 0) { throw "Niagara asset generation failed: $LASTEXITCODE" }

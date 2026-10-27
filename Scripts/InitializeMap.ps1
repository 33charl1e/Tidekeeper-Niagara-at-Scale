param([string]$EngineRoot = 'D:\Program Files\Epic Games\UE_5.7')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$ProjectRoot\NiagaraHerd.uproject" -run=pythonscript "-script=$PSScriptRoot\initialize_map.py" -unattended -nosplash -NullRHI "-abslog=$ProjectRoot\Saved\Logs\InitializeMap.log"
if ($LASTEXITCODE -ne 0) { throw "Map initialization failed: $LASTEXITCODE" }

param([string]$EngineRoot = 'D:\Program Files\Epic Games\UE_5.7')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" NiagaraHerdEditor Win64 Development "$ProjectRoot\NiagaraHerd.uproject" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=4
if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $LASTEXITCODE" }

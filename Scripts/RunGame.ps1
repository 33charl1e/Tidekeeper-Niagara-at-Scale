param([string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7')
$ProjectRoot=Split-Path -Parent $PSScriptRoot
Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$ProjectRoot\NiagaraHerd.uproject`"",'-game','-windowed','-ResX=1920','-ResY=1080','-nosplash')

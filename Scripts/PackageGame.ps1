param([string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7',[switch]$SkipCook)
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$CookArgument=if($SkipCook){'-skipcook'}else{'-cook'}
& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$ProjectRoot\NiagaraHerd.uproject" -noP4 -platform=Win64 -clientconfig=Development -build $CookArgument -stage -pak -iostore -compressed -archive "-archivedirectory=$ProjectRoot\Packaged" -unattended -utf8output
if($LASTEXITCODE -ne 0) { throw "Packaging failed: $LASTEXITCODE" }

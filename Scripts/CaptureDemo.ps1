param([string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7')
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$Name='Demo-'+(Get-Date -Format yyyyMMdd-HHmmss)
$ArgsList=@("`"$ProjectRoot\NiagaraHerd.uproject`"",'-game','-windowed','-ResX=1920','-ResY=1080','-unattended','-nosplash','-nosound',"-HerdCapture=$Name",'-HerdTestExit=180',"-abslog=`"$ProjectRoot\Saved\Logs\Capture-$Name.log`"")
$Run=Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $ArgsList -WindowStyle Hidden -Wait -PassThru
if($Run.ExitCode -ne 0) { throw "Capture failed: $($Run.ExitCode)" }
$Frames=Join-Path $ProjectRoot "Saved\Capture\$Name"
$Count=@(Get-ChildItem -LiteralPath $Frames -Filter 'Frame*.png').Count
if($Count -ne 2250) { throw "Expected 2250 frames; got $Count. Check capture log." }
$Encoder=Join-Path $ProjectRoot 'Saved\EncodeCapture.exe'
if(!(Test-Path -LiteralPath $Encoder) -or (Get-Item -LiteralPath "$PSScriptRoot\EncodeCapture.cpp").LastWriteTimeUtc -gt (Get-Item -LiteralPath $Encoder).LastWriteTimeUtc)
{
    & "$PSScriptRoot\BuildEncoder.cmd"
    if($LASTEXITCODE -ne 0) { throw "Encoder build failed: $LASTEXITCODE" }
}
& "$ProjectRoot\Saved\EncodeCapture.exe" $Frames "$ProjectRoot\Docs\Tidekeeper-Demo.mp4" 30
if($LASTEXITCODE -ne 0) { throw "Video encoding failed: $LASTEXITCODE" }
Write-Output "$ProjectRoot\Docs\Tidekeeper-Demo.mp4"

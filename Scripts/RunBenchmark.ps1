param([string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7',[switch]$UseEditor)
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$ProfileDir=Join-Path $ProjectRoot 'Saved\Profiling'
New-Item -ItemType Directory -Path $ProfileDir -Force | Out-Null
$Stamp=Get-Date -Format yyyyMMdd-HHmmss
$Trace=Join-Path $ProfileDir "HerdSweep-$Stamp.utrace"
$Game=Join-Path $ProjectRoot 'Packaged\Windows\NiagaraHerd\Binaries\Win64\NiagaraHerd.exe'
$CsvDir=Join-Path $ProjectRoot 'Packaged\Windows\NiagaraHerd\Saved\Benchmarks'
$Log=Join-Path $ProjectRoot "Saved\Logs\Benchmark-$Stamp.log"
$Arguments=@('-windowed','-ResX=1920','-ResY=1080','-unattended','-nosplash','-nosound','-statnamedevents','-HerdTestExit=200','-ExecCmds="herd.Benchmark 1,stat Niagara,stat GPU"','-trace=cpu,gpu,frame,bookmark,region',"-tracefile=`"$Trace`"","-abslog=`"$Log`"")
if($UseEditor -or !(Test-Path -LiteralPath $Game))
{
    $Game="$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe"
    $CsvDir=Join-Path $ProjectRoot 'Saved\Benchmarks'
    $Arguments=@("`"$ProjectRoot\NiagaraHerd.uproject`"",'-game')+$Arguments
}
$Run=Start-Process -FilePath $Game -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
if($Run.ExitCode -ne 0) { throw "Benchmark process failed: $($Run.ExitCode)" }
if(!(Select-String -LiteralPath $Log -Pattern 'HERD_BENCHMARK_COMPLETE' -Quiet)) { throw "Benchmark did not complete; inspect $Log" }
Write-Output "Trace: $Trace"
Get-ChildItem -LiteralPath $CsvDir -Filter '*.csv' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName

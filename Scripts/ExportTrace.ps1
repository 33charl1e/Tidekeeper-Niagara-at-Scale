param(
    [Parameter(Mandatory=$true)][string]$TraceFile,
    [string]$EngineRoot='D:\Program Files\Epic Games\UE_5.7'
)
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$ProfileDir=Join-Path $ProjectRoot 'Saved\Profiling\Export'
New-Item -ItemType Directory -Path $ProfileDir -Force | Out-Null
$ForwardDir=$ProfileDir.Replace('\','/')
$Response=Join-Path $ProfileDir 'export.rsp'
@(
    "TimingInsights.ExportTimers $ForwardDir/Timers.csv",
    "TimingInsights.ExportTimerStatistics $ForwardDir/All-{region}.csv -region=CPU_*,GPU_*",
    "TimingInsights.ExportTimerStatistics $ForwardDir/GPU-{region}.csv -region=CPU_*,GPU_* -threads=GPU*",
    "TimingInsights.ExportTimerStatistics $ForwardDir/GT-{region}.csv -region=CPU_*,GPU_* -threads=GameThread",
    "TimingInsights.ExportTimerStatistics $ForwardDir/RT-{region}.csv -region=CPU_*,GPU_* -threads=RenderThread*"
) | Set-Content -LiteralPath $Response -Encoding utf8
$ExportArgs=@("-OpenTraceFile=`"$TraceFile`"",'-NoUI','-AutoQuit',"-ExecOnAnalysisCompleteCmd=@=`"$Response`"","-abslog=`"$ProjectRoot\Saved\Logs\InsightsExport.log`"")
$Process=Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealInsights.exe" -ArgumentList $ExportArgs -WindowStyle Hidden -Wait -PassThru
if($Process.ExitCode -ne 0) { throw "Insights export failed: $($Process.ExitCode)" }
Write-Output $ProfileDir

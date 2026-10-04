param(
    [string]$Port = 'COM3',
    [ValidateSet('TestDebug', 'TestRelease')][string]$Preset = 'TestDebug',
    [int]$TimeoutSeconds = 30,
    [string]$Python = 'python',
    [switch]$CaptureOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Cmake {
    param([string[]]$Arguments, [string]$LogPath)
    # Windows PowerShell treats ordinary native stderr as an error under Stop.
    $ErrorActionPreference = 'Continue'
    & cmake @Arguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE" }
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$previousPath = $env:PATH
$exitStatus = 2
Push-Location -LiteralPath $projectRoot
try {
    $toolRoot = 'C:\ST\STM32CubeCLT_1.21.0'
    foreach ($relative in @('CMake\bin', 'Ninja\bin', 'GNU-tools-for-STM32\bin')) {
        $toolDirectory = Join-Path $toolRoot $relative
        if (Test-Path -LiteralPath $toolDirectory) { $env:PATH = $toolDirectory + ';' + $env:PATH }
    }
    & $Python -c 'import serial'
    if ($LASTEXITCODE -ne 0) { throw 'Install the serial dependency: python -m pip install -r tests/requirements.txt' }
    $outputDirectory = Join-Path $projectRoot 'build\test-results'
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    if (-not $CaptureOnly) {
        Invoke-Cmake -Arguments @('--preset', $Preset) -LogPath (Join-Path $outputDirectory "configure-$Preset-$stamp.log")
        Invoke-Cmake -Arguments @('--build', '--preset', $Preset) -LogPath (Join-Path $outputDirectory "build-$Preset-$stamp.log")
        # The generated Commander script selects the ELF belonging to this preset.
        Invoke-Cmake -Arguments @('--build', '--preset', $Preset, '--target', 'flash_jlink') -LogPath (Join-Path $outputDirectory "flash-$Preset-$stamp.log")
    }
    & $Python scripts/rtos_test_runner.py --port $Port --timeout $TimeoutSeconds --firmware "build/$Preset/Rtos_Learn.elf"
    $exitStatus = $LASTEXITCODE
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    $env:PATH = $previousPath
    Pop-Location
}
exit $exitStatus

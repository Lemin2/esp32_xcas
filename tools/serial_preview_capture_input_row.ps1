param(
    [string]$Port = "COM13",
    [int]$Baud = 460800,
    [string]$FormulasFile = "tools/tmp_preview_stress_cases.txt",
    [string]$OutDir = "captures/ui_preview_stress_large_matrix_sqrt_nested_v2",
    [int]$SubmitWaitMs = 6000,
    [int]$ShotTimeoutMs = 30000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $FormulasFile)) {
    throw "Formulas file not found: $FormulasFile"
}
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}
$formulas = Get-Content -Path $FormulasFile -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    Add-Type -AssemblyName System.IO.Ports
}

function Read-ShotLines([System.IO.Ports.SerialPort]$serial, [int]$timeoutMs) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $inShot = $false
    $lines = New-Object System.Collections.Generic.List[string]

    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        try {
            $line = $serial.ReadLine()
            if ($null -eq $line) { continue }
            $trim = $line.TrimEnd("`r", "`n")
            if ($trim.Contains("SHOT_BEGIN")) {
                $inShot = $true
                $lines.Add($trim)
                continue
            }
            if ($inShot) {
                $lines.Add($trim)
                if ($trim.Contains("SHOT_END")) {
                    return ,$lines
                }
            }
        } catch [System.TimeoutException] {
            continue
        }
    }

    return @()
}

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.NewLine = "`n"
$sp.ReadTimeout = 220
$sp.WriteTimeout = 1200
$sp.Encoding = [System.Text.Encoding]::UTF8
$sp.DtrEnable = $false
$sp.RtsEnable = $false

try {
    $sp.Open()
    Start-Sleep -Milliseconds 300
    $sp.WriteLine("ML HELP")
    Start-Sleep -Milliseconds 120

    $index = 0
    foreach ($formula in $formulas) {
        $index++
        $txtPath = Join-Path $OutDir ("{0:D2}_preview.txt" -f $index)
        $pngPath = Join-Path $OutDir ("{0:D2}_preview.png" -f $index)

        $sp.WriteLine("ML SUBMIT $formula")
        Start-Sleep -Milliseconds $SubmitWaitMs

        # Select newest input line and open fullscreen preview.
        $sp.WriteLine("ML KEY UP")
        Start-Sleep -Milliseconds 120
        $sp.WriteLine("ML KEY SPACE")
        Start-Sleep -Milliseconds 220

        $sp.WriteLine("ML SHOT")
        $shotLines = Read-ShotLines -serial $sp -timeoutMs $ShotTimeoutMs
        if (@($shotLines).Count -eq 0) {
            "[WARN] No complete preview SHOT for: $formula" | Tee-Object -FilePath $txtPath
            continue
        }

        $shotLines | Set-Content -Path $txtPath -Encoding UTF8
        & powershell -ExecutionPolicy Bypass -File "decode_shot.ps1" -In $txtPath -Out $pngPath | Out-Null
        Write-Output ("[OK] {0} -> {1}" -f $formula, $pngPath)

        # Close preview and continue.
        $sp.WriteLine("ML KEY SPACE")
        Start-Sleep -Milliseconds 120
    }
}
finally {
    if ($sp.IsOpen) {
        $sp.Close()
    }
}

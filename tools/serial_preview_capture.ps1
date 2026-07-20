param(
    [string]$Port = "COM7",
    [int]$Baud = 460800,
    [string]$OutDir = "captures/ui_preview_fullscreen_debug",
    [string]$FormulasFile = "",
    [int]$SubmitWaitMs = 1800,
    [int]$ShotTimeoutMs = 25000,
    [switch]$DtrEnable,
    [switch]$RtsEnable,
    [int]$InitialWaitMs = 0,
    [switch]$UiPreview
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

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

function Write-Command([System.IO.Ports.SerialPort]$serial, [string]$command) {
    $serial.Write($command + "`r")
}

$formulas = @()
if (-not [string]::IsNullOrWhiteSpace($FormulasFile)) {
    if (-not (Test-Path $FormulasFile)) {
        throw "Formulas file not found: $FormulasFile"
    }
    $formulas = Get-Content -Path $FormulasFile -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
} else {
    $formulas = @(
        "sum(k^2,k,1,n)",
        "[[1/(x+1),sqrt(2)],[sum(k,k,1,n),x^2]]"
    )
}

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.NewLine = "`n"
$sp.ReadTimeout = 250
$sp.WriteTimeout = 1200
$sp.Encoding = [System.Text.Encoding]::UTF8
$sp.DtrEnable = [bool]$DtrEnable
$sp.RtsEnable = [bool]$RtsEnable

try {
    $sp.Open()
    if ($InitialWaitMs -gt 0) {
        Start-Sleep -Milliseconds $InitialWaitMs
    }
    Start-Sleep -Milliseconds 300
    Write-Command -serial $sp -command "ML HELP"
    Start-Sleep -Milliseconds 100

    $index = 0
    foreach ($formula in $formulas) {
        $index++
        $txtPath = Join-Path $OutDir ("{0:D2}_preview.txt" -f $index)
        $pngPath = Join-Path $OutDir ("{0:D2}_preview.png" -f $index)

        if ($UiPreview) {
            Write-Command -serial $sp -command "ML SUBMIT $formula"
            Start-Sleep -Milliseconds $SubmitWaitMs
            Write-Command -serial $sp -command "ML PREVIEW_SHOT"
        } else {
            Write-Command -serial $sp -command "ML PREVIEW_SHOT $formula"
        }

        $shotLines = Read-ShotLines -serial $sp -timeoutMs $ShotTimeoutMs
        if (@($shotLines).Count -eq 0) {
            "[WARN] No complete preview SHOT for: $formula" | Tee-Object -FilePath $txtPath
            continue
        }

        $shotLines | Set-Content -Path $txtPath -Encoding UTF8
        & powershell -ExecutionPolicy Bypass -File "decode_shot.ps1" -In $txtPath -Out $pngPath | Out-Null
        Write-Output ("[OK] {0} -> {1}" -f $formula, $pngPath)
    }
}
finally {
    if ($sp.IsOpen) {
        $sp.Close()
    }
}

param(
    [string]$Port = "COM7",
    [int]$Baud = 460800,
    [string]$FormulasFile = "tools/mathlayout_formulas.txt",
    [string]$OutDir = "captures",
    [int]$RunWaitMs = 120,
    [int]$ReadLoopMs = 20000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $FormulasFile)) {
    throw "Formulas file not found: $FormulasFile"
}

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$formulas = Get-Content -Path $FormulasFile | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
if (@($formulas).Count -eq 0) {
    throw "No formulas found in $FormulasFile"
}

# PowerShell 5.1 often has SerialPort already loaded; PowerShell 7 may need explicit load.
if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    try {
        Add-Type -AssemblyName System.IO.Ports
    }
    catch {
        $portsDll = Join-Path $PSHOME "System.IO.Ports.dll"
        if (Test-Path $portsDll) {
            Add-Type -Path $portsDll
        }
    }
}

if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    throw "System.IO.Ports.SerialPort is unavailable in this PowerShell runtime. Use Windows PowerShell 5.1 or install the runtime that provides System.IO.Ports."
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.NewLine = "`n"
$sp.ReadTimeout = 250
$sp.WriteTimeout = 1000
$sp.DtrEnable = $false
$sp.RtsEnable = $false

function Read-ShotLines([System.IO.Ports.SerialPort]$serial, [int]$timeoutMs) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $inShot = $false
    $lines = New-Object System.Collections.Generic.List[string]
    $sawEnd = $false

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
                    $sawEnd = $true
                    return ,$lines
                }
            }
        } catch [System.TimeoutException] {
            continue
        } catch [System.IO.IOException] {
            continue
        }
    }

    if (-not $sawEnd) {
        return @()
    }
    return ,$lines
}

function Sanitize-Name([string]$input) {
    $s = $input -replace '[^a-zA-Z0-9_\-]+', '_'
    if ([string]::IsNullOrWhiteSpace($s)) { return "expr" }
    if ($s.Length -gt 36) { return $s.Substring(0, 36) }
    return $s
}

Push-Location (Resolve-Path ".")
try {
    $sp.Open()
    Start-Sleep -Milliseconds 500

    $index = 0
    foreach ($formula in $formulas) {
        $index++
        $name = Sanitize-Name $formula
        $prefix = "{0:D2}_{1}" -f $index, $name
        $txtPath = Join-Path $OutDir ("$prefix.txt")
        $pngPath = Join-Path $OutDir ("$prefix.png")

        $cmd = "ML RUN $formula"
        try {
            $sp.WriteLine($cmd)
        }
        catch [System.IO.IOException] {
            "[WARN] Serial write failed for: $formula" | Tee-Object -FilePath $txtPath
            continue
        }
        Start-Sleep -Milliseconds $RunWaitMs

        $shotLines = Read-ShotLines -serial $sp -timeoutMs $ReadLoopMs
        if (@($shotLines).Count -eq 0) {
            "[WARN] No complete SHOT frame for: $formula" | Tee-Object -FilePath $txtPath
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
    Pop-Location
}

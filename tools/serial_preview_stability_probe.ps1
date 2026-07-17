param(
    [string]$Port = "COM7",
    [int]$Baud = 460800,
    [string]$FormulasFile = "tools/tmp_preview_stress_cases.txt",
    [string]$OutDir = "captures/preview_stability_probe",
    [int]$SubmitWaitMs = 700,
    [int]$StepWindowMs = 2500
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
$ts = Get-Date -Format 'yyyyMMdd_HHmmss'
$logPath = Join-Path $OutDir ("preview_stability_" + $ts + ".log")

if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    Add-Type -AssemblyName System.IO.Ports
}

function Read-Window([System.IO.Ports.SerialPort]$serial, [int]$windowMs, [string]$tag, [string]$logPath) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $windowMs) {
        try {
            $line = $serial.ReadLine()
            if ($null -eq $line) { continue }
            $t = $line.TrimEnd("`r", "`n")
            $msg = "[{0}] {1}" -f $tag, $t
            Add-Content -Path $logPath -Value $msg -Encoding UTF8
            if ($t -match 'task_wdt|Guru Meditation|Backtrace|abort|panic|rst:|ELF file SHA256|Rebooting|ML_UI|ML_ACK') {
                Write-Output $msg
            }
        }
        catch [System.TimeoutException] {
            continue
        }
        catch {
            $em = "[{0}] SERIAL_READ_ERR: {1}" -f $tag, $_.Exception.Message
            Add-Content -Path $logPath -Value $em -Encoding UTF8
            Write-Output $em
            break
        }
    }
}

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.NewLine = "`n"
$sp.ReadTimeout = 180
$sp.WriteTimeout = 1500
$sp.Encoding = [System.Text.Encoding]::UTF8
$sp.DtrEnable = $false
$sp.RtsEnable = $false

try {
    $sp.Open()
    Add-Content -Path $logPath -Value ("=== START " + (Get-Date).ToString('s')) -Encoding UTF8

    $sp.WriteLine("ML HELP")
    Read-Window -serial $sp -windowMs 1200 -tag "BOOT" -logPath $logPath

    $idx = 0
    foreach ($formula in $formulas) {
        $idx++
        Add-Content -Path $logPath -Value ("=== CASE " + $idx + " : " + $formula) -Encoding UTF8

        try {
            $sp.WriteLine("ML SUBMIT $formula")
        }
        catch {
            $em = "[CASE $idx] SERIAL_WRITE_ERR SUBMIT: " + $_.Exception.Message
            Add-Content -Path $logPath -Value $em -Encoding UTF8
            Write-Output $em
            break
        }

        Read-Window -serial $sp -windowMs $SubmitWaitMs -tag ("C" + $idx + "-SUBMIT") -logPath $logPath

        foreach ($cmd in @("ML KEY UP", "ML KEY SPACE", "ML SHOT", "ML KEY SPACE")) {
            try {
                $sp.WriteLine($cmd)
                Add-Content -Path $logPath -Value ("[CASE $idx] SEND " + $cmd) -Encoding UTF8
            }
            catch {
                $em = "[CASE $idx] SERIAL_WRITE_ERR " + $cmd + ": " + $_.Exception.Message
                Add-Content -Path $logPath -Value $em -Encoding UTF8
                Write-Output $em
                break
            }

            Read-Window -serial $sp -windowMs $StepWindowMs -tag ("C" + $idx + "-" + $cmd.Replace(' ','_')) -logPath $logPath
        }
    }

    Add-Content -Path $logPath -Value ("=== END " + (Get-Date).ToString('s')) -Encoding UTF8
    Write-Output ("LOG_FILE: " + $logPath)
}
finally {
    if ($sp.IsOpen) {
        $sp.Close()
    }
}

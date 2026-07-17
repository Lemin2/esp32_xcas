param(
    [string]$Port = "COM7",
    [int]$Baud = 460800,
    [string]$Formula = "[[1/(x+1),sqrt(2)],[sum(k,k,1,n),x^2]]",
    [int]$ReadyWaitMs = 10000,
    [int]$AfterSubmitMs = 6000,
    [int]$AfterShotMs = 30000,
    [string]$OutDir = "captures/serial_stability"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$ts = Get-Date -Format 'yyyyMMdd_HHmmss'
$logPath = Join-Path $OutDir ("matrix_fallback_probe_" + $ts + ".log")

if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    Add-Type -AssemblyName System.IO.Ports
}

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.NewLine = "`n"
$sp.ReadTimeout = 120
$sp.WriteTimeout = 1200
$sp.Encoding = [System.Text.Encoding]::UTF8
$sp.DtrEnable = $false
$sp.RtsEnable = $false

function Read-Window([System.IO.Ports.SerialPort]$serial, [int]$windowMs) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $windowMs) {
        try {
            $line = $serial.ReadLine()
            if ($null -eq $line) { continue }
            $t = $line.TrimEnd("`r", "`n")
            Add-Content -Path $logPath -Value $t -Encoding UTF8
            if ($t -match 'ML_UI|SHOT_BEGIN|SHOT_END|SHOT_ERR|task_wdt|Guru Meditation|Backtrace|abort|panic') {
                Write-Output $t
            }
        }
        catch [System.TimeoutException] {
            continue
        }
        catch [System.IO.IOException] {
            continue
        }
    }
}

try {
    $sp.Open()
    Add-Content -Path $logPath -Value ("=== START " + (Get-Date).ToString('s')) -Encoding UTF8

    $sp.WriteLine('ML HELP')
    $ready = $false
    $swReady = [System.Diagnostics.Stopwatch]::StartNew()
    while ($swReady.ElapsedMilliseconds -lt $ReadyWaitMs -and -not $ready) {
        try {
            $line = $sp.ReadLine()
            if ($line) {
                $t = $line.TrimEnd("`r", "`n")
                Add-Content -Path $logPath -Value $t -Encoding UTF8
                if ($t -match 'serial automation ready|ML_HELP') {
                    $ready = $true
                    Write-Output "READY"
                }
                if ($t -match 'task_wdt|Guru Meditation|Backtrace|abort|panic') {
                    Write-Output $t
                }
            }
        }
        catch [System.TimeoutException] {
            try { $sp.WriteLine('ML HELP') } catch {}
        }
    }

    $sp.WriteLine("ML SUBMIT $Formula")
    Read-Window -serial $sp -windowMs $AfterSubmitMs

    $sp.WriteLine('ML SHOT')
    Read-Window -serial $sp -windowMs $AfterShotMs

    Add-Content -Path $logPath -Value ("=== END " + (Get-Date).ToString('s')) -Encoding UTF8
    Write-Output ("LOG_FILE: " + $logPath)
}
finally {
    if ($sp.IsOpen) {
        $sp.Close()
    }
}

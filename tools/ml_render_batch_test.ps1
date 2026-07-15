param(
    [string]$Port   = "COM7",
    [int]$Baud      = 460800,
    [string]$CasesFile = "tools/ml_render_cases.txt"
)

# Load System.IO.Ports
if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    try { Add-Type -AssemblyName System.IO.Ports } catch {
        $dll = Join-Path $PSHOME "System.IO.Ports.dll"
        if (Test-Path $dll) { Add-Type -Path $dll }
    }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.NewLine      = "`n"
$sp.ReadTimeout  = 300
$sp.WriteTimeout = 1000
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false
$sp.Open()
Start-Sleep -Milliseconds 300

$cases = Get-Content $CasesFile | Where-Object { $_ -match '\S' -and $_ -notmatch '^\s*#' }
$pass = 0; $fail = 0

foreach ($expr in $cases) {
    $expr = $expr.Trim()
    try {
        $sp.WriteLine("ML RENDER $expr")
    } catch { Write-Warning "Write failed for: $expr"; ++$fail; continue }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $lines = @(); $started = $false; $done = $false
    while ($sw.ElapsedMilliseconds -lt 6000 -and -not $done) {
        try {
            $line = $sp.ReadLine().TrimEnd("`r","`n")
            if ($line -eq "ML_RENDER_BEGIN")         { $started = $true }
            elseif ($started -and $line -match "^ML_LINE:(.*)")    { $lines += $Matches[1] }
            elseif ($started -and $line -match "^ML_RENDER_END")   { $done = $true }
        } catch [System.TimeoutException] {}
          catch [System.IO.IOException]   {}
    }

    if ($done -and $lines.Count -gt 0) {
        Write-Output "[PASS] $expr  ($($lines.Count) lines)"
        ++$pass
    } else {
        Write-Warning "[FAIL] $expr  (received $($lines.Count) lines, done=$done)"
        ++$fail
    }
    Start-Sleep -Milliseconds 120
}

$sp.Close()
Write-Output ""
Write-Output "Result: $pass passed, $fail failed out of $($pass+$fail) cases"
if ($fail -gt 0) { exit 1 }

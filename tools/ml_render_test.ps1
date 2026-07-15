param(
    [string]$Port = "COM7",
    [int]$Baud = 460800,
    [string]$Expr = "1/2",
    [int]$TimeoutMs = 5000
)

# Load System.IO.Ports
if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    try { Add-Type -AssemblyName System.IO.Ports } catch {
        $dll = Join-Path $PSHOME "System.IO.Ports.dll"
        if (Test-Path $dll) { Add-Type -Path $dll }
    }
}
if (-not ("System.IO.Ports.SerialPort" -as [type])) {
    throw "System.IO.Ports not available"
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.NewLine      = "`n"
$sp.ReadTimeout  = 300
$sp.WriteTimeout = 1000
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false

$sp.Open()
Start-Sleep -Milliseconds 200

try {
    $sp.WriteLine("ML RENDER $Expr")

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $lines = @()
    $started = $false
    $baseline = -1
    $height   = -1

    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        try {
            $raw = $sp.ReadLine()
            $line = $raw.TrimEnd("`r", "`n")

            if ($line -eq "ML_RENDER_BEGIN") {
                $started = $true
                continue
            }
            if ($started -and $line -match "^ML_LINE:(.*)") {
                $lines += $Matches[1]
                continue
            }
            if ($started -and $line -match "^ML_RENDER_END baseline=(\d+) height=(\d+)") {
                $baseline = [int]$Matches[1]
                $height   = [int]$Matches[2]
                break
            }
        }
        catch [System.TimeoutException] { continue }
        catch [System.IO.IOException]   { continue }
    }

    if (-not $started -or $lines.Count -eq 0) {
        Write-Error "No ML_RENDER output received for: $Expr"
        exit 1
    }

    Write-Output "=== ML RENDER: $Expr ==="
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $marker = if ($i -eq $baseline) { " <-- baseline" } else { "" }
        Write-Output ("|$($lines[$i])|$marker")
    }
    Write-Output "=== height=$height baseline=$baseline ==="
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
}

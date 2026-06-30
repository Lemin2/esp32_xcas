param(
    [string]$In = "C:\Users\Lenovo\source\repos\esp32_giac\shot_capture.txt",
    [string]$Out = "C:\Users\Lenovo\source\repos\esp32_giac\shot.png"
)

Add-Type -AssemblyName System.Drawing

$lines = Get-Content -Path $In
$b64 = New-Object System.Text.StringBuilder
foreach ($l in $lines) {
    $idx = $l.IndexOf('SHOT:')
    if ($idx -ge 0) {
        $data = $l.Substring($idx + 5).Trim()
        [void]$b64.Append($data)
    }
}
$s = $b64.ToString()
Write-Output ("b64 chars=" + $s.Length)
$bytes = [System.Convert]::FromBase64String($s)
Write-Output ("decoded bytes=" + $bytes.Length)

$w = 240; $h = 135
$expected = $w * $h * 2
if ($bytes.Length -lt $expected) {
    Write-Output ("WARNING: short by " + ($expected - $bytes.Length) + " bytes")
}

$bmp = New-Object System.Drawing.Bitmap($w, $h)
for ($y = 0; $y -lt $h; $y++) {
    for ($x = 0; $x -lt $w; $x++) {
        $off = ($y * $w + $x) * 2
        if ($off + 1 -ge $bytes.Length) { continue }
        $v = [int]$bytes[$off] -bor ([int]$bytes[$off + 1] -shl 8)
        $r = (($v -shr 11) -band 0x1F) -shl 3
        $g = (($v -shr 5) -band 0x3F) -shl 2
        $b = ($v -band 0x1F) -shl 3
        $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($r, $g, $b))
    }
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("saved " + $Out)

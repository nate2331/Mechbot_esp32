param([string]$Port = 'COM8', [int]$Trials = 10)
$ErrorActionPreference = 'Stop'
$logDir = Join-Path $PSScriptRoot '..\test_results\imu-maker-rvc-continuous-20260907'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logFile = Join-Path $logDir "esp-reset-trials-$stamp.txt"
$serial = [System.IO.Ports.SerialPort]::new($Port,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 200
$writer = [System.IO.StreamWriter]::new($logFile,$false)
function Capture-Window([int]$Milliseconds) {
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $received = [System.Text.StringBuilder]::new()
    while ($watch.ElapsedMilliseconds -lt $Milliseconds) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length) { $writer.Write($chunk); [void]$received.Append($chunk) }
        Start-Sleep -Milliseconds 20
    }
    $writer.Flush()
    return $received.ToString()
}
try {
    $serial.Open()
    $writer.WriteLine("HOST START $(Get-Date -Format o) port=$Port; IMU power untouched. RTS resets ESP only.")
    $initial = Capture-Window 3000
    Write-Output ($initial -split "`n" | Where-Object { $_ -match '^RUN ' } | Select-Object -Last 1)
    for ($trial = 1; $trial -le $Trials; $trial++) {
        $writer.WriteLine("HOST ESP_RESET trial=$trial $(Get-Date -Format o)")
        $serial.RtsEnable = $true
        Start-Sleep -Milliseconds 100
        $serial.RtsEnable = $false
        $data = Capture-Window 4000
        $boot = $data -match 'MAKER_IMU_RVC_CONTINUOUS V1'
        $readyMatch = [regex]::Match($data,'EVENT READY boot_ms=(\d+) acquisition=1')
        Write-Output "TRIAL $trial boot=$boot ready=$($readyMatch.Success) ready_boot_ms=$($readyMatch.Groups[1].Value)"
        if (-not $boot -or -not $readyMatch.Success) { throw "Trial $trial did not confirm restart and READY within 4 seconds; inspect log." }
    }
    $data = Capture-Window 15000
    Write-Output ($data -split "`n" | Where-Object { $_ -match '^(RUN|COUNTS|UART_EVENTS) ' } | Select-Object -Last 3)
} finally {
    if ($serial.IsOpen) { $serial.RtsEnable=$false; $serial.Close() }
    $serial.Dispose()
    $writer.Dispose()
    Write-Output "Saved: $logFile"
}

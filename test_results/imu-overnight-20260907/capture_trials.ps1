param(
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Label,
  [ValidateSet('ceva','strdc','stream')][string]$Protocol='ceva',
  [string]$Commands='a',
  [int]$StreamSeconds=35
)
$ErrorActionPreference='Stop'
if ($Commands -notmatch '^[agrmblh]+$') { throw 'Only documented trial commands are accepted.' }
if ($StreamSeconds -lt 1 -or $StreamSeconds -gt 60) { throw 'Stream duration must be 1..60 seconds.' }
$imuSerial=[System.IO.Ports.SerialPort]::new('COM7',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$imuSerial.DtrEnable=$false
$imuSerial.RtsEnable=$false
$imuSerial.ReadTimeout=200
$imuSerial.WriteTimeout=1000
$imuSerial.NewLine="`n"
$outputRoot=$PSScriptRoot
$imuTranscript=[System.Text.StringBuilder]::new()
function Read-ImuWindow([int]$Milliseconds) {
  $windowWatch=[System.Diagnostics.Stopwatch]::StartNew()
  while ($windowWatch.ElapsedMilliseconds -lt $Milliseconds) {
    [void]$imuTranscript.Append($imuSerial.ReadExisting())
    Start-Sleep -Milliseconds 20
  }
}
try {
  $imuSerial.Open()
  # Release GPIO0 through DTR, pulse EN through RTS. No flash writes.
  $imuSerial.RtsEnable=$true
  Start-Sleep -Milliseconds 100
  $imuSerial.DtrEnable=$false
  $imuSerial.RtsEnable=$false
  Read-ImuWindow 2000
  [System.IO.File]::WriteAllText((Join-Path $outputRoot "$Label-boot.txt"),$imuTranscript.ToString())
  if ($Protocol -eq 'stream') {
    Read-ImuWindow ($StreamSeconds*1000)
    $resultPath=Join-Path $outputRoot "$Label-stream.txt"
    [System.IO.File]::WriteAllText($resultPath,$imuTranscript.ToString())
    "Saved stream: $resultPath"
    $imuTranscript.ToString() -split "`r?`n" | Select-Object -First 22
  } else {
    $expected=if ($Protocol -eq 'ceva') {'IMU_TRIALS V1 CUSTOM'} else {'STRDC_READBACK V1'}
    if (-not $imuTranscript.ToString().Contains($expected)) { throw "Expected firmware banner '$expected' missing; no trial commands sent." }
    $endPattern=if ($Protocol -eq 'ceva') {'(?m)^END (?:#\d+;[^\r\n]*|FAIL_[^\r\n]*)\r?\n'} else {'(?m)^DONE [^\r\n]*\r?\n'}
    $trialIndex=0
    foreach ($trialChar in $Commands.ToCharArray()) {
      ++$trialIndex
      [void]$imuTranscript.Clear()
      $imuSerial.DiscardInBuffer()
      $trialPath=Join-Path $outputRoot ('{0}-{1:d2}-{2}.txt' -f $Label,$trialIndex,$trialChar)
      "Starting $Label trial $trialIndex command=$trialChar"
      $imuSerial.Write([string]$trialChar)
      $trialWatch=[System.Diagnostics.Stopwatch]::StartNew()
      $complete=$false
      while ($trialWatch.ElapsedMilliseconds -lt 55000) {
        [void]$imuTranscript.Append($imuSerial.ReadExisting())
        if ($imuTranscript.ToString() -match $endPattern) { $complete=$true; break }
        Start-Sleep -Milliseconds 25
      }
      [System.IO.File]::WriteAllText($trialPath,$imuTranscript.ToString())
      $imuTranscript.ToString() -split "`r?`n" | Where-Object { $_ -match '^(BEGIN|TRIAL|INIT|BOOT|SET|PRE|POST|GET_|ID_|FINAL|SUMMARY|RESULT|END|DONE|OFF| ERR| ID| 0x)' -and $_ -notmatch '^ 0x' } | ForEach-Object { $_ }
      if ($Protocol -eq 'ceva') {
        $textLines=$imuTranscript.ToString() -split "`r?`n"
        $finalIndex=-1
        for ($lineIndex=0; $lineIndex -lt $textLines.Length; ++$lineIndex) { if ($textLines[$lineIndex] -like 'FINAL *') { $finalIndex=$lineIndex; break } }
        if ($finalIndex -ge 0) { for ($i=$finalIndex+1; $i -lt $textLines.Length -and $textLines[$i] -match '^ 0x'; ++$i) { $textLines[$i] } }
      }
      if (-not $complete) { throw "Trial exceeded 55 seconds. Saved partial log $trialPath; sequence stopped." }
      Start-Sleep -Milliseconds 350
    }
  }
} finally {
  if ($imuSerial.IsOpen) { $imuSerial.Close() }
  $imuSerial.Dispose()
}

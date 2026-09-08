param(
    [string]$ZigPath = 'zig'
)

# Host simulation only. Does not enumerate, open, or upload to serial devices.
$ErrorActionPreference = 'Stop'
$movementBuild = Join-Path ([System.IO.Path]::GetTempPath()) ('maker-rvc-movement-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $movementBuild | Out-Null
$movementExe = Join-Path $movementBuild 'maker_rvc_movement_test.exe'
$movementSource = Join-Path $PSScriptRoot 'maker_rvc_movement_test.cpp'
$movementStubs = Join-Path $PSScriptRoot 'maker_rvc_movement_host_stubs'
$movementLog = Join-Path $movementBuild 'test-results.txt'

& $ZigPath c++ -std=c++17 -O0 -g "-I$movementStubs" $movementSource -o $movementExe
if ($LASTEXITCODE -ne 0) { throw "Movement host-test compilation failed ($LASTEXITCODE)." }

$movementCases = @('', 'attach-failure', 'task-failure', 'mutex-failure', 'clock-failure', 'uart-failure', 'uart-rx-failure')
foreach ($movementCase in $movementCases) {
    if ($movementCase) { $movementOutput = & $movementExe $movementCase 2>&1 }
    else { $movementOutput = & $movementExe 2>&1 }
    $movementExit = $LASTEXITCODE
    $movementOutput | Tee-Object -FilePath $movementLog -Append
    if ($movementExit -ne 0) { throw "Movement host test '$movementCase' failed ($movementExit)." }
}
Write-Output "Host test results: $movementLog"

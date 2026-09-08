param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$testDir = $PSScriptRoot
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$env:PATH = (Split-Path -Parent $compilerPath) + ';' + $env:PATH
$binaryPath = Join-Path $testDir 'test_sketch.exe'
& $compilerPath -std=c++17 -O2 -Wall -Wextra -I (Join-Path $testDir 'stubs') (Join-Path $testDir 'test_sketch.cpp') -o $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Host sketch compilation failed' }
$testNames = @('boot','timed','noedge','emergency_rest','emergency_run','busy_command','boot_channel1','boot_channel2','boot_channel8','boot_timer','boot_stop','timerfailure','clockfailure','writefailure','updatefailure','stopfailure','readbackfailure','parser','overflow','balanced','scaling','quiet_timeout','quadrature','wrap','restart','queue_progression')
$failedTests = @()
foreach ($testName in $testNames) {
  & $binaryPath $testName
  if ($LASTEXITCODE -ne 0) { $failedTests += $testName }
}
if ($failedTests.Count) { throw ('Failed tests: ' + ($failedTests -join ', ')) }
Write-Output ("All {0} host simulations passed. No hardware used." -f $testNames.Count)

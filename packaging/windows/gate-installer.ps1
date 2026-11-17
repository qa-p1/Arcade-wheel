# Release gate (gates.yml): the published Setup.exe of a release candidate on a clean Windows runner. Installs it
# silently into an isolated Arcade home and profile, runs the installed app's smoke test and the native install
# checks, uninstalls it and checks nothing of Wheel is left. CI only: it uses the runner's account.
param([Parameter(Mandatory = $true)][string]$Setup)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:RUNNER_TEMP) { throw 'Run this only on the Windows GitHub Actions runner.' }
$env:ARCADE_HOME = Join-Path $env:RUNNER_TEMP 'wheel-gate-arcade'
$env:ARCADE_WHEEL_INSTANCE = Join-Path $env:RUNNER_TEMP 'wheel-gate-profile'
$installer = Get-Item -LiteralPath $Setup
$destination = Join-Path $env:RUNNER_TEMP 'Arcade Wheel Gate'
$process = Start-Process -FilePath $installer.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/MERGETASKS=arcade_startup', "/DIR=`"$destination`"" -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Installer failed: $($process.ExitCode)" }
python packaging/smoke-test.py "$destination/arcade-wheel.exe"
if ($LASTEXITCODE -ne 0) { throw 'Installed app smoke test failed' }
python packaging/check-native-install.py windows "$destination/arcade-wheel.exe"
if ($LASTEXITCODE -ne 0) { throw 'Installed Windows lifecycle checks failed' }
$receiptPath = Join-Path $env:ARCADE_HOME 'installs/arcade.wheel.json'
if (-not (Test-Path $receiptPath)) { throw 'Installer did not write the Arcade receipt' }
$process = Start-Process -FilePath "$destination/unins000.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Uninstaller failed: $($process.ExitCode)" }
if (Test-Path "$destination/arcade-wheel.exe") { throw 'Uninstaller left the executable behind' }
if (Test-Path $receiptPath) { throw 'Uninstaller left the Arcade receipt behind' }
if (Test-Path (Join-Path $env:ARCADE_HOME 'bin/arcade-wheel.cmd')) { throw 'Uninstaller left the CLI shim behind' }
if (Test-Path (Join-Path $env:ARCADE_HOME 'apps/arcade.wheel.json')) { throw 'Uninstaller left the Arcade Link manifest behind' }
Write-Output 'Published Windows installer: install, smoke test, native checks and uninstall passed.'

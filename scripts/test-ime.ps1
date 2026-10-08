$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $locator)) { throw 'Visual Studio C++ Build Tools are required for the native IME test.' }
$installation = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
New-Item -ItemType Directory -Force (Join-Path $taskRoot 'artifacts') | Out-Null
Push-Location $taskRoot
try {
  $testCommand = 'call "' + $setup + '" && cl /nologo /EHsc /utf-8 /std:c++17 tests\hangul.cpp /Fe:artifacts\hangul-test.exe /Fo:artifacts\hangul-test.obj && artifacts\hangul-test.exe'
  & cmd.exe /d /c $testCommand
  if ($LASTEXITCODE -ne 0) { throw 'Native IME test failed.' }
  $navCommand = 'call "' + $setup + '" && cl /nologo /EHsc /utf-8 /std:c++17 tests\navigation.cpp /Fe:artifacts\navigation-test.exe /Fo:artifacts\navigation-test.obj && artifacts\navigation-test.exe'
  & cmd.exe /d /c $navCommand
  if ($LASTEXITCODE -ne 0) { throw 'Native navigation test failed.' }
  $connectionCommand = 'call "' + $setup + '" && cl /nologo /EHsc /utf-8 /std:c++17 tests\connection.cpp /Fe:artifacts\connection-test.exe /Fo:artifacts\connection-test.obj && artifacts\connection-test.exe'
  & cmd.exe /d /c $connectionCommand
  if ($LASTEXITCODE -ne 0) { throw 'Native connection/locale test failed.' }
  $pairingCommand = 'call "' + $setup + '" && cl /nologo /EHsc /utf-8 /std:c++17 tests\pairing.cpp /Fe:artifacts\pairing-test.exe /Fo:artifacts\pairing-test.obj && artifacts\pairing-test.exe'
  & cmd.exe /d /c $pairingCommand
  if ($LASTEXITCODE -ne 0) { throw 'Native host pairing test failed.' }
} finally { Pop-Location }

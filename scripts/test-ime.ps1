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
} finally { Pop-Location }

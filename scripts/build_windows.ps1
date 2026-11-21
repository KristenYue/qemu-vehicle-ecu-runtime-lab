$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Build Tools installer metadata was not found."
}
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) {
    throw "Visual C++ x64 build tools are required."
}
$developerPrompt = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
$runtimeDirectory = Join-Path $repositoryRoot ".runtime\windows-build"
New-Item -ItemType Directory -Force $runtimeDirectory | Out-Null

$environmentLines = & cmd.exe /d /s /c ('call "{0}" -arch=x64 >nul && set' -f $developerPrompt)
foreach ($line in $environmentLines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
    }
}

Push-Location $repositoryRoot
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /permissive- /Z7 /Icpp\include `
        cpp\src\application_manifest.cpp cpp\src\dependency_graph.cpp cpp\src\heartbeat_protocol.cpp `
        cpp\tests\test_core.cpp `
        /Fe"$runtimeDirectory\vecu_core_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Native compile failed." }
    & "$runtimeDirectory\vecu_core_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Core tests failed." }
} finally {
    Pop-Location
}


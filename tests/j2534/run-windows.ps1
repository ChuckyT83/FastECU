# Native x64 Windows only. Requires an MSVC developer shell and Qt 5 in PATH.
# Every driver operation goes to our mock DLL; no hardware is enumerated or used.
[CmdletBinding()]
param([string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build-j2534'))
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$source = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = [IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Force $build | Out-Null
$logs = Join-Path $build 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

function Invoke-Logged {
    param([string]$Name, [string]$File, [string[]]$Arguments)
    & $File @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $logs "$Name.log")
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        # Compiler/test output contains no credentials. Keep the diagnostic tail
        # in a public annotation as well as the downloadable artifact.
        $content = Get-Content (Join-Path $logs "$Name.log")
        $diagnostics = $content | Where-Object { $_.Length -lt 1000 -and $_ -match '(?i)error|fatal|FAIL!|Totals:|exception' } | Select-Object -Last 20
        if (-not $diagnostics) { $diagnostics = $content | Where-Object { $_.Length -lt 500 } | Select-Object -Last 12 }
        $tail = $diagnostics -join "`n"
        if ($tail.Length -gt 3500) { $tail = $tail.Substring($tail.Length - 3500) }
        $escaped = $tail.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Output "::error title=$Name (exit $code)::$escaped"
        throw "$Name failed with exit code $code"
    }
}

Push-Location $build
try {
    "Source commit: $(git -C $source rev-parse HEAD)" | Set-Content (Join-Path $logs 'revision.txt')
    Invoke-Logged -Name 'qmake-version' -File 'qmake.exe' -Arguments @('-v')
    Invoke-Logged -Name 'qmake-configuration' -File 'qmake.exe' -Arguments @('-query')
    Invoke-Logged -Name 'compiler-path' -File 'where.exe' -Arguments @('cl.exe')
    $mock = Join-Path $source 'tests/j2534/mock_dll.cpp'
    # /Bv records the actual compiler version; /MD matches the release Qt runtime.
    Invoke-Logged -Name 'build-mock' -File 'cl.exe' -Arguments @('/nologo', '/Bv', '/W4', '/std:c++14', '/EHsc', '/MD', '/D_CRT_SECURE_NO_WARNINGS', '/LD', $mock, '/Fe:mock-j2534.dll')
    Invoke-Logged -Name 'build-incomplete-mock' -File 'cl.exe' -Arguments @('/nologo', '/W4', '/std:c++14', '/EHsc', '/MD', '/D_CRT_SECURE_NO_WARNINGS', '/DOMIT_IOCTL', '/LD', $mock, '/Fe:incomplete-j2534.dll')
    Invoke-Logged -Name 'build-loader' -File 'cl.exe' -Arguments @('/nologo', '/W4', '/std:c++14', '/EHsc', '/MD', '/D_CRT_SECURE_NO_WARNINGS', (Join-Path $source 'tests/j2534/loader.cpp'), (Join-Path $source 'serial_port/J2534_win.cpp'), '/Fe:loader-test.exe')
    Invoke-Logged -Name 'loader-test' -File (Join-Path $build 'loader-test.exe') -Arguments @((Join-Path $build 'mock-j2534.dll'), (Join-Path $build 'incomplete-j2534.dll'))
    Invoke-Logged -Name 'qmake-tests' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/j2534/windows.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
    Invoke-Logged -Name 'build-initialization-tests' -File 'nmake.exe' -Arguments @('/NOLOGO')
    $env:QT_QPA_PLATFORM = 'offscreen'
    Invoke-Logged -Name 'initialization-tests' -File (Join-Path $build 'j2534-windows-reliability.exe') -Arguments @('-o', "$(Join-Path $logs 'initialization-results.txt'),txt", '-o', "$(Join-Path $logs 'initialization-results.xml'),junitxml", '-o', '-,txt')
    $transportBuild = Join-Path $build 'transport'
    New-Item -ItemType Directory -Force $transportBuild | Out-Null
    Copy-Item (Join-Path $build 'mock-j2534.dll') $transportBuild -Force
    Push-Location $transportBuild
    try {
        Invoke-Logged -Name 'qmake-transport' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/j2534/transport.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-transport' -File 'nmake.exe' -Arguments @('/NOLOGO')
        Invoke-Logged -Name 'transport-tests' -File (Join-Path $transportBuild 'j2534-transport.exe') -Arguments @('-o', "$(Join-Path $logs 'transport-results.txt'),txt", '-o', "$(Join-Path $logs 'transport-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $subaruBuild = Join-Path $build 'subaru'
    New-Item -ItemType Directory -Force $subaruBuild | Out-Null
    Push-Location $subaruBuild
    try {
        Invoke-Logged -Name 'qmake-subaru' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/subaru/safety.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-subaru' -File 'nmake.exe' -Arguments @('/NOLOGO')
        Invoke-Logged -Name 'subaru-tests' -File (Join-Path $subaruBuild 'subaru-safety.exe') -Arguments @('-o', "$(Join-Path $logs 'subaru-results.txt'),txt", '-o', "$(Join-Path $logs 'subaru-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $evidence = @(
        (Get-Content (Join-Path $logs 'qmake-version.log') | Select-Object -First 2)
        (Get-Content (Join-Path $logs 'loader-test.log') | Select-String 'checks passed')
        ('Initialization: ' + ((Get-Content (Join-Path $logs 'initialization-results.txt') | Select-String 'Totals:') -join ' '))
        ('Transport: ' + ((Get-Content (Join-Path $logs 'transport-results.txt') | Select-String 'Totals:') -join ' '))
        ('Subaru safety: ' + ((Get-Content (Join-Path $logs 'subaru-results.txt') | Select-String 'Totals:') -join ' '))
        ('MSVC toolset: ' + $env:VCToolsVersion)
        (Get-Content (Join-Path $logs 'build-mock.log') | Select-String 'Compiler Version' | Select-Object -First 1)
    ) -join '; '
    Write-Output "::notice title=Native Windows J2534 results::$evidence"
    if ($env:GITHUB_STEP_SUMMARY) {
        $evidence | Add-Content $env:GITHUB_STEP_SUMMARY
    }
} finally {
    Pop-Location
}

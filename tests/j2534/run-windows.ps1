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
    if ($code -ne 0) { throw "$Name failed with exit code $code" }
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
} finally {
    Pop-Location
}

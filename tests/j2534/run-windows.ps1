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
    param([string]$Name, [string]$File, [string[]]$Arguments, [int]$ExpectedExitCode = 0)
    & $File @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $logs "$Name.log")
    $code = $LASTEXITCODE
    if ($code -ne $ExpectedExitCode) {
        # Compiler/test output contains no credentials. Keep the diagnostic tail
        # in a public annotation as well as the downloadable artifact.
        $content = Get-Content (Join-Path $logs "$Name.log")
        $context = $content | Where-Object { $_.Length -lt 500 -and $_ -match '(?i)PASS|INFO.*entering|Safety harness phase' } | Select-Object -Last 6
        $stack = $content | Where-Object { $_.Length -lt 500 -and $_ -match '^#' } | Select-Object -First 12
        $errors = $content | Where-Object { $_.Length -lt 1000 -and $_ -match '(?i)error|fatal|FAIL!|Totals:|exception' } | Select-Object -Last 6
        $diagnostics = @($context; $stack; $errors)
        if (-not $diagnostics) { $diagnostics = $content | Where-Object { $_.Length -lt 500 } | Select-Object -Last 12 }
        $tail = $Name + "`n" + ($diagnostics -join "`n")
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
        # Qt 5.15.2 QMessageBox on Windows dereferences platformNativeInterface
        # for its system menu; minimal/offscreen lack that native interface.
        # Timers drive the real Windows widgets without manual interaction.
        $env:QT_QPA_PLATFORM = 'windows'
        $env:QT_STYLE_OVERRIDE = 'Fusion'
        Invoke-Logged -Name 'subaru-tests' -File (Join-Path $subaruBuild 'subaru-safety.exe') -Arguments @('-v1', '-o', "$(Join-Path $logs 'subaru-results.txt'),txt", '-o', "$(Join-Path $logs 'subaru-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $facadeBuild = Join-Path $build 'facade'
    New-Item -ItemType Directory -Force $facadeBuild | Out-Null
    Push-Location $facadeBuild
    try {
        Invoke-Logged -Name 'qmake-facade' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/subaru/facade.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-facade' -File 'nmake.exe' -Arguments @('/NOLOGO')
        Invoke-Logged -Name 'facade-tests' -File (Join-Path $facadeBuild 'subaru-facade.exe') -Arguments @('-o', "$(Join-Path $logs 'facade-results.txt'),txt", '-o', "$(Join-Path $logs 'facade-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $kernelBuild = Join-Path $build 'kernel-offline'
    New-Item -ItemType Directory -Force $kernelBuild | Out-Null
    Push-Location $kernelBuild
    try {
        # Qt Core only; no GUI, J2534 loader, vendor DLL or serial backend.
        Invoke-Logged -Name 'qmake-kernel-offline' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/subaru/kernel_transfer.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-kernel-offline' -File 'nmake.exe' -Arguments @('/NOLOGO')
        Invoke-Logged -Name 'kernel-offline-tests' -File (Join-Path $kernelBuild 'subaru-kernel-offline.exe') -Arguments @('-o', "$(Join-Path $logs 'kernel-offline-results.txt'),txt", '-o', "$(Join-Path $logs 'kernel-offline-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $uploadBuild = Join-Path $build 'upload-candidate'
    New-Item -ItemType Directory -Force $uploadBuild | Out-Null
    Copy-Item (Join-Path $build 'mock-j2534.dll') $uploadBuild -Force
    Push-Location $uploadBuild
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        Invoke-Logged -Name 'qmake-upload-candidate' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/subaru/upload_candidate.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-upload-candidate' -File 'nmake.exe' -Arguments @('/NOLOGO')
        Invoke-Logged -Name 'upload-candidate-tests' -File (Join-Path $uploadBuild 'sh7058-upload-candidate.exe') -Arguments @('-o', "$(Join-Path $logs 'upload-candidate-results.txt'),txt", '-o', "$(Join-Path $logs 'upload-candidate-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $diagnosticsBuild = Join-Path $build 'diagnostics'
    New-Item -ItemType Directory -Force $diagnosticsBuild | Out-Null
    Copy-Item (Join-Path $build 'mock-j2534.dll') $diagnosticsBuild -Force
    Copy-Item (Join-Path $build 'incomplete-j2534.dll') $diagnosticsBuild -Force
    Push-Location $diagnosticsBuild
    try {
        Invoke-Logged -Name 'qmake-diagnostics' -File 'qmake.exe' -Arguments @((Join-Path $source 'tests/j2534/diagnostics.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-diagnostics' -File 'nmake.exe' -Arguments @('/NOLOGO')
        Invoke-Logged -Name 'diagnostics-tests' -File (Join-Path $diagnosticsBuild 'j2534-diagnostics-tests.exe') -Arguments @('-o', "$(Join-Path $logs 'diagnostics-results.txt'),txt", '-o', "$(Join-Path $logs 'diagnostics-results.xml'),junitxml", '-o', '-,txt')
    } finally { Pop-Location }
    $utilityBuild = Join-Path $build 'diagnostic-tool'
    New-Item -ItemType Directory -Force $utilityBuild | Out-Null
    Push-Location $utilityBuild
    try {
        Invoke-Logged -Name 'qmake-diagnostic-tool' -File 'qmake.exe' -Arguments @((Join-Path $source 'tools/j2534_diagnostics/diagnostics.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.')
        Invoke-Logged -Name 'build-diagnostic-tool' -File 'nmake.exe' -Arguments @('/NOLOGO')
        # Expected refusal is a successful safety assertion, never a device probe.
        Invoke-Logged -Name 'identification-blocked' -File (Join-Path $utilityBuild 'fastecu-j2534-diagnostics.exe') -Arguments @('--identification-plan') -ExpectedExitCode 2
        $package = Join-Path $build 'diagnostic-tool-package'
        New-Item -ItemType Directory -Force $package | Out-Null
        Copy-Item (Join-Path $utilityBuild 'fastecu-j2534-diagnostics.exe') $package -Force
        Invoke-Logged -Name 'package-diagnostic-tool' -File 'windeployqt.exe' -Arguments @('--no-translations', '--no-compiler-runtime', (Join-Path $package 'fastecu-j2534-diagnostics.exe'))
        Invoke-Logged -Name 'packaged-identification-blocked' -File (Join-Path $package 'fastecu-j2534-diagnostics.exe') -Arguments @('--identification-plan') -ExpectedExitCode 2
        # Passive HKLM/view enumeration is safe on a host with no J2534 adapter.
        $discovery = & (Join-Path $package 'fastecu-j2534-diagnostics.exe') 2>&1 | Tee-Object -FilePath (Join-Path $logs 'driver-discovery.json')
        $discoveryCode = $LASTEXITCODE
        if ($discoveryCode -notin @(0, 1)) { throw "Discovery failed unexpectedly ($discoveryCode)" }
        $discoveryReport = ($discovery -join "`n") | ConvertFrom-Json
        if ($discoveryReport.validation_level -ne 'discovery_only' -or $discoveryReport.device_opened -or $discoveryReport.ecu_communication) { throw 'Discovery exceeded passive capability' }
        if ($discoveryReport.registry_errors.Count -ne 0) { throw 'Native registry enumeration reported errors' }
        if (($discoveryCode -eq 1) -ne ($discoveryReport.compatible_04_04_count -eq 0)) { throw 'Discovery exit/result mismatch' }
    } finally { Pop-Location }
    $evidence = @(
        (Get-Content (Join-Path $logs 'qmake-version.log') | Select-Object -First 2)
        (Get-Content (Join-Path $logs 'loader-test.log') | Select-String 'checks passed')
        ('Initialization: ' + ((Get-Content (Join-Path $logs 'initialization-results.txt') | Select-String 'Totals:') -join ' '))
        ('Transport: ' + ((Get-Content (Join-Path $logs 'transport-results.txt') | Select-String 'Totals:') -join ' '))
        ('Subaru production safety and offline upload candidate: ' + ((Get-Content (Join-Path $logs 'subaru-results.txt') | Select-String 'Totals:') -join ' '))
        ('Facade: ' + ((Get-Content (Join-Path $logs 'facade-results.txt') | Select-String 'Totals:') -join ' '))
        ('Synthetic kernel model: ' + ((Get-Content (Join-Path $logs 'kernel-offline-results.txt') | Select-String 'Totals:') -join ' '))
        ('Production upload candidate: ' + ((Get-Content (Join-Path $logs 'upload-candidate-results.txt') | Select-String 'Totals:') -join ' '))
        ('Driver diagnostics: ' + ((Get-Content (Join-Path $logs 'diagnostics-results.txt') | Select-String 'Totals:') -join ' '))
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
# An accepted diagnostic refusal/no-driver exit is not a failed test job.
# Unexpected native statuses and assertions throw before reaching this point.
exit 0

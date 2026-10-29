#Requires -Version 7.2
# Native Windows MinGW x64 full-application build. No adapter/ECU operation.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OpenSslSource,
    [string]$ReviewedOpenPortSHA256='',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../../build-windows-development')
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Native Windows is required: this script builds and executes Windows PE targets; Linux parser validation is not a Windows build.' }
$PSNativeCommandUseErrorActionPreference = $false
$source = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = [IO.Path]::GetFullPath($BuildDirectory)
$sslSource = (Resolve-Path $OpenSslSource).Path
New-Item -ItemType Directory -Force $build | Out-Null
$logs = Join-Path $build 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
function Invoke-Logged {
    param([string]$Name,[string]$File,[string[]]$Arguments)
    & $File @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $logs "$Name.log")
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        # Retain the bounded tool tail even when a diagnostic does not contain
        # the word 'error' (for example, a Perl platform/path mismatch).
        $tail = (Get-Content (Join-Path $logs "$Name.log") | Select-Object -Last 12) -join "`n"
        throw "$Name failed with exit $code; see $Name.log`n$tail"
    }
}
# Refuse an unreviewed dependency revision. Never download vendor J2534 DLLs.
$sslRevision = (& git -C $sslSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sslRevision -ne '67b5686b4419b4cb8caa502711c41815f5279751') {
    throw 'Expected official OpenSSL 3.5.5 source revision 67b5686b4419b4cb8caa502711c41815f5279751'
}
$qtRoot = (& qmake.exe -query QT_INSTALL_PREFIX).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Qt qmake unavailable' }
$mingw = Join-Path (Split-Path (Split-Path $qtRoot)) 'Tools/mingw810_64/bin'
if (-not (Test-Path (Join-Path $mingw 'g++.exe'))) { throw 'Qt 5.15.2 x64 MinGW 8.1 toolchain required' }
$env:PATH = "$mingw;$env:PATH"
Invoke-Logged 'qmake-version' 'qmake.exe' @('-v')
Invoke-Logged 'compiler-version' 'g++.exe' @('--version')
Invoke-Logged 'qmake-paths' 'qmake.exe' @('-query')
$qtPlugins = (& qmake.exe -query QT_INSTALL_PLUGINS).Trim()
if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $qtPlugins 'platforms/qwindows.dll'))) {
    throw "Qt SDK Windows platform plugin missing under $qtPlugins"
}
# Qt 5.15 cannot reliably classify MinGW DLLs by their PE debug-stripped flag.
# Keep windeployqt's default MinGW matching instead of forcing --release.
# Check deployment on a supplied SDK binary without executing it, before the
# expensive source build. This fixture is not included in the final package.
$sdkProbe = Join-Path $qtRoot 'bin/qtdiag.exe'
if (Test-Path $sdkProbe) {
    $probeDirectory = Join-Path $build 'sdk-deploy-probe'
    New-Item -ItemType Directory -Force $probeDirectory | Out-Null
    Copy-Item $sdkProbe $probeDirectory
    Invoke-Logged 'sdk-deploy-probe' 'windeployqt.exe' @('--compiler-runtime', '--no-translations', (Join-Path $probeDirectory 'qtdiag.exe'))
}
$sslPrefix = Join-Path $build 'openssl'
$sslBuild = Join-Path $build 'openssl-build'
New-Item -ItemType Directory -Force $sslBuild | Out-Null
# OpenSSL's MinGW target requires Unix-path Perl/make, not native Windows Perl.
# Use the runner's MSYS2 build tools, but retain Qt's exact native x64 compiler.
$msysBin = 'C:/msys64/usr/bin'
foreach ($tool in @('bash.exe', 'perl.exe', 'make.exe', 'cygpath.exe')) {
    if (-not (Test-Path (Join-Path $msysBin $tool))) { throw "MSYS2 build tool required: $msysBin/$tool" }
}
$opensslScript = (Join-Path $PSScriptRoot 'openssl-mingw.sh').Replace('\', '/')
Invoke-Logged 'openssl-build' (Join-Path $msysBin 'bash.exe') @('--noprofile', '--norc', $opensslScript,
    $mingw.Replace('\', '/'), $sslSource.Replace('\', '/'), $sslBuild.Replace('\', '/'), $sslPrefix.Replace('\', '/'))
$appBuild = Join-Path $build 'application'
New-Item -ItemType Directory -Force $appBuild | Out-Null
Push-Location $appBuild
try {
    Invoke-Logged 'app-qmake' 'qmake.exe' @((Join-Path $source 'FastECU.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=release', "OPENSSL_PREFIX=$sslPrefix", "REVIEWED_OPENPORT_SHA256=$ReviewedOpenPortSHA256")
    Invoke-Logged 'app-build' 'mingw32-make.exe' @('-j2')
    $exe = Join-Path $appBuild 'release/FastECU.exe'
    if (-not (Test-Path $exe)) { throw "Full application executable not found at the explicit qmake destination: $exe" }
    $package = Join-Path $build 'FastECU-offline-development-x64'
    New-Item -ItemType Directory -Force $package | Out-Null
    Copy-Item $exe $package
    "[Paths]`nPrefix=.`nPlugins=." | Set-Content (Join-Path $package 'qt.conf')
    Invoke-Logged 'qt-deploy' 'windeployqt.exe' @('--compiler-runtime', '--no-translations', (Join-Path $package 'FastECU.exe'))
    # Archive committed public defaults, never untracked/local configuration.
    & git -C $source diff --quiet HEAD -- config config.qrc
    if ($LASTEXITCODE -ne 0) { throw 'Configuration defaults/resources differ from the reviewed commit' }
    $defaultsArchive = Join-Path $build 'committed-config-defaults.tar'
    Invoke-Logged 'archive-defaults' 'git.exe' @('-C', $source, 'archive', '--format=tar', "--output=$defaultsArchive", 'HEAD', 'config')
    Invoke-Logged 'extract-defaults' 'tar.exe' @('-xf', $defaultsArchive, '-C', $package)
    Copy-Item (Join-Path $source 'LICENSE') $package
    $licenses = Join-Path $package 'third-party-licenses'
    New-Item -ItemType Directory -Force $licenses | Out-Null
    Copy-Item (Join-Path $sslSource 'LICENSE.txt') (Join-Path $licenses 'OpenSSL-LICENSE.txt')
    if (Test-Path (Join-Path $qtRoot 'licenses')) { Copy-Item (Join-Path $qtRoot 'licenses') $licenses -Recurse }
    @'
OFFLINE DEVELOPMENT BUILD — NOT A VEHICLE-READY FLASHER
Production SH7058 upload, write and test-write gates remain active.
No adapter or ECU compatibility is established by this package.
Do not start vehicle testing with a flash. Driver discovery/device-only validation
requires the separate diagnostic procedure and matching official x64 J2534 DLL.
Qt remote TLS runtime and real hardware are not qualified by the --help smoke test.
'@ | Set-Content (Join-Path $package 'DEVELOPMENT-BUILD.txt')
    # Exercise the packaged PE/Qt runtime without constructing MainWindow, which
    # performs discovery. PATH excludes Qt/compiler locations to detect omissions.
    $savedPath = $env:PATH
    $savedPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    $savedPluginPath = $env:QT_PLUGIN_PATH
    Push-Location $package
    try {
        $env:PATH = "$package;$env:SystemRoot/System32;$env:SystemRoot"
        Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
        Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
        Invoke-Logged 'packaged-startup-help' (Join-Path $package 'FastECU.exe') @('--help')
    } finally {
        $env:PATH = $savedPath
        if ($null -ne $savedPlatformPath) { $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPlatformPath }
        if ($null -ne $savedPluginPath) { $env:QT_PLUGIN_PATH = $savedPluginPath }
        Pop-Location
    }
    Get-ChildItem $package -Recurse -File | Get-FileHash -Algorithm SHA256 | Format-Table -AutoSize | Out-String -Width 500 | Set-Content (Join-Path $logs 'package-sha256.txt')
    @("Source commit: $(& git -C $source rev-parse HEAD)", "OpenSSL source: $sslRevision", "Qt prefix: $qtRoot", 'Full Windows app built; packaged --help executed; no MainWindow/device test.') | Set-Content (Join-Path $logs 'build-result.txt')
} finally { Pop-Location }
exit 0

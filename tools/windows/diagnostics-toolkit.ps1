#Requires -Version 7.2
# Native offline tests and standalone Qt Core-only diagnostic packaging.
[CmdletBinding()]
param([ValidateSet('x64','x86')][string]$Architecture='x64', [string]$BuildDirectory='build-diagnostics-x64')
$ErrorActionPreference='Stop'
if (-not $IsWindows) { throw 'Native Windows is required; this is not a cross-build validation' }
$PSNativeCommandUseErrorActionPreference=$false
$source=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build=[IO.Path]::GetFullPath($BuildDirectory)
$logs=Join-Path $build 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
function Invoke-Kit {
    param([string]$Name,[string]$File,[string[]]$Arguments,[int]$Expected=0)
    & $File @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $logs "$Name.log")
    $code=$LASTEXITCODE
    if($code -ne $Expected) {
        $tail=(Get-Content (Join-Path $logs "$Name.log") | Select-Object -Last 12) -join "`n"
        $message=($Name+" exit "+$code+"`n"+$tail)
        if($message.Length -gt 3000){$message=$message.Substring($message.Length-3000)}
        $message=$message.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
        Write-Output "::error title=Diagnostic toolkit failure::$message"
        throw "$Name failed"
    }
}
$commit=(& git -C $source rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'Source revision unavailable'}
$qt=(& qmake.exe -query QT_INSTALL_PREFIX).Trim()
if($LASTEXITCODE -ne 0){throw 'qmake unavailable'}
$bits=if($Architecture -eq 'x64'){'64'}else{'32'}
$compiler=Join-Path (Split-Path (Split-Path $qt)) "Tools/mingw810_$bits/bin"
if(-not(Test-Path (Join-Path $compiler 'g++.exe'))){throw 'Matching Qt MinGW compiler missing'}
$env:PATH="$compiler;$env:PATH"
Invoke-Kit 'qmake-version' 'qmake.exe' @('-v')
Invoke-Kit 'compiler-version' 'g++.exe' @('--version')
function Build-Target {
    param([string]$Directory,[string]$Project,[string[]]$Extra=@())
    New-Item -ItemType Directory -Force $Directory | Out-Null
    Push-Location $Directory
    try {
        $label=Split-Path $Directory -Leaf
        Invoke-Kit "$label-qmake" 'qmake.exe' (@((Join-Path $source $Project),'CONFIG+=release','CONFIG-=debug_and_release','DESTDIR=.')+$Extra)
        Invoke-Kit "$label-make" 'mingw32-make.exe' @('-j2')
    } finally {Pop-Location}
}
$tool=Join-Path $build 'tool'
$testTool=Join-Path $build 'test-tool'
$unit=Join-Path $build 'unit'
$cli=Join-Path $build 'cli'
Build-Target $tool 'tools/j2534_diagnostics/diagnostics.pro'
Build-Target $testTool 'tools/j2534_diagnostics/diagnostics.pro' @('CONFIG+=diagnostic_test')
Build-Target $unit 'tests/j2534/diagnostics.pro'
Build-Target $cli 'tests/j2534/diagnostic_cli.pro'
$mock=Join-Path $source 'tests/j2534/mock_dll.cpp'
Push-Location $unit
try {
    $variants=@(
        @{Name='mock-j2534.dll';Define=''},
        @{Name='incomplete-j2534.dll';Define='OMIT_IOCTL'},
        @{Name='incomplete-device-j2534.dll';Define='OMIT_VERSION'},
        @{Name='open-error-j2534.dll';Define='MOCK_OPEN_FAILURE'},
        @{Name='version-error-j2534.dll';Define='MOCK_VERSION_FAILURE'},
        @{Name='close-error-j2534.dll';Define='MOCK_CLOSE_FAILURE'},
        @{Name='delayed-j2534.dll';Define='MOCK_OPEN_DELAY_MS=500'})
    foreach($v in $variants) {
        $arguments=@('-std=c++11','-O2','-shared',$mock,'-o',$v.Name)
        if($Architecture -eq 'x86'){$arguments+='-Wl,--kill-at'}
        if($v.Define){$arguments+="-D$($v.Define)"}
        Invoke-Kit "mock-$($v.Name)" 'g++.exe' $arguments
        Copy-Item $v.Name $cli -Force
    }
    Invoke-Kit 'unit-results' (Join-Path $unit 'j2534-diagnostics-tests.exe') @('-o',"$(Join-Path $logs 'unit-results.txt'),txt",'-o','-,txt')
} finally {Pop-Location}
$env:FASTECU_DIAGNOSTIC_TOOL=Join-Path $testTool 'fastecu-j2534-diagnostics.exe'
Push-Location $cli
try {Invoke-Kit 'cli-results' (Join-Path $cli 'j2534-diagnostic-cli-tests.exe') @('-o',"$(Join-Path $logs 'cli-results.txt'),txt",'-o','-,txt')}
finally {Pop-Location;Remove-Item Env:FASTECU_DIAGNOSTIC_TOOL -ErrorAction SilentlyContinue}
$package=Join-Path $build 'package'
New-Item -ItemType Directory -Force $package | Out-Null
Copy-Item (Join-Path $tool 'fastecu-j2534-diagnostics.exe') $package
Invoke-Kit 'deploy-diagnostics' 'windeployqt.exe' @('--compiler-runtime','--no-translations',(Join-Path $package 'fastecu-j2534-diagnostics.exe'))
"[Paths]`nPrefix=.`nPlugins=." | Set-Content (Join-Path $package 'qt.conf')
$Architecture | Set-Content (Join-Path $package 'toolkit-architecture.txt')
Copy-Item (Join-Path $source 'LICENSE') $package
$notices=Join-Path $package 'third-party-licenses'
New-Item -ItemType Directory -Force $notices | Out-Null
# Include the SDK's supplied license texts where present; never bundle vendor drivers.
foreach($candidate in @((Join-Path $qt 'licenses'),(Join-Path $qt 'LICENSES'),(Join-Path (Split-Path (Split-Path $qt)) 'Licenses'))) {
    if(Test-Path $candidate){Copy-Item $candidate $notices -Recurse -Force}
}
foreach($candidate in @((Join-Path $compiler '../share/licenses'),(Join-Path $compiler '../share/doc/gcc'))) {
    if(Test-Path $candidate){Copy-Item $candidate $notices -Recurse -Force}
}
Copy-Item (Join-Path $source 'docs/diagnostic-runtime-notices.md') $notices
foreach($document in @('adapter-arrival-checklist.md','j2534-diagnostic-command-reference.md','adapter-diagnostics-troubleshooting.md','sh7058-identification-preflight.md')) {
    Copy-Item (Join-Path $source "docs/$document") $package
}
Copy-Item (Join-Path $PSScriptRoot 'collect-adapter-diagnostics.ps1') $package
@'
DEVICE-ONLY DIAGNOSTICS — NOT A VEHICLE-READY FLASHER
Passive inspection is the default. No vehicle connection or firmware update.
Clone/OpenPort device calls are blocked pending Task 007.
OBDX calls require a separately reviewed driver, exact hash, explicit firmware-
behavior review and vehicle-disconnected acknowledgements. These are owner
assertions, not proof of identity/compatibility. See the command reference.
An uncertain worker call is never forcibly terminated by this toolkit.
Qt Core and MinGW runtimes are deployed. No vendor DLL, EcuFlash, ECU image,
adapter firmware, private calibration or credential is included.
'@ | Set-Content (Join-Path $package 'READ-FIRST.txt')
$saved=$env:PATH
Push-Location $package
try {
    $env:PATH="$package;$env:SystemRoot/System32;$env:SystemRoot"
    Invoke-Kit 'packaged-help' (Join-Path $package 'fastecu-j2534-diagnostics.exe') @('--help')
    Invoke-Kit 'packaged-self-inspection' (Join-Path $package 'fastecu-j2534-diagnostics.exe') @('--inspect-file',(Join-Path $package 'fastecu-j2534-diagnostics.exe'),'--output',(Join-Path $logs 'packaged-self.json'))
    $self=Get-Content (Join-Path $logs 'packaged-self.json') -Raw | ConvertFrom-Json
    $expectedMachine=if($Architecture -eq 'x64'){0x8664}else{0x14c}
    if($self.pe.machine -ne $expectedMachine -or $self.test_build -or $self.development_commit -ne $commit){throw 'Packaged executable architecture/revision/test-mode mismatch'}
    Invoke-Kit 'packaged-passive' (Join-Path $package 'fastecu-j2534-diagnostics.exe') @('--output',(Join-Path $logs 'packaged-passive.json'))
    $passive=Get-Content (Join-Path $logs 'packaged-passive.json') -Raw | ConvertFrom-Json
    if($passive.device_opened -or $passive.ecu_communication -or $passive.stage -ne 'A_passive'){throw 'Packaged discovery exceeded passive stage'}
} finally {Pop-Location;$env:PATH=$saved}
$files=Get-ChildItem $package -Recurse -File | ForEach-Object { @{path=$_.FullName.Substring($package.Length+1);sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} }
$files | ConvertTo-Json | Set-Content (Join-Path $package 'SHA256.json')
& (Join-Path $PSScriptRoot 'test-packaged-diagnostics.ps1') -PackageDirectory $package -Architecture $Architecture -EvidenceDirectory (Join-Path $logs 'relocated-passive')
$summary="Architecture: $Architecture; commit: $commit; executable SHA256: $($self.file_info.sha256); unit: $((Get-Content (Join-Path $logs 'unit-results.txt') | Select-String 'Totals:') -join ' '); CLI: $((Get-Content (Join-Path $logs 'cli-results.txt') | Select-String 'Totals:') -join ' '); packaged passive/help/self-PE: passed"
$summary | Set-Content (Join-Path $logs 'summary.txt')
Write-Output "::notice title=Native diagnostic toolkit results::$summary"
exit 0

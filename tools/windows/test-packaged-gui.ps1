#Requires -Version 7.2
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'This test requires native Windows' }
$package = (Resolve-Path $PackageDirectory).Path
$logs = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force $logs | Out-Null
$required = @('FastECU.exe', 'qt.conf', 'platforms/qwindows.dll',
    'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Xml.dll', 'Qt5Network.dll',
    'Qt5SerialPort.dll', 'Qt5RemoteObjects.dll', 'Qt5WebSockets.dll',
    'config/fastecu.cfg', 'config/menu.cfg', 'config/protocols.cfg', 'config/logger.cfg',
    'collect-adapter-diagnostics.ps1', 'adapter-arrival-checklist.md',
    'j2534-diagnostic-command-reference.md', 'adapter-diagnostics-troubleshooting.md', 'sh7058-identification-preflight.md',
    'adapters/openport/j2534.dll','adapters/openport/libusb-1.0.dll','adapters/openport/adapter-manifest.json',
    'LICENSE', 'third-party-licenses', 'diagnostics/x64/fastecu-j2534-diagnostics.exe')
# qmake's Charts SDK declaration does not imply an imported Charts runtime:
# the application currently has no QChart symbols. Check the real PE imports.
$qtRoot = (& qmake.exe -query QT_INSTALL_PREFIX).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Qt SDK location unavailable for matching PE inspector' }
$objdump = Join-Path (Split-Path (Split-Path $qtRoot)) 'Tools/mingw810_64/bin/objdump.exe'
if (-not (Test-Path $objdump)) { throw 'Matching MinGW PE inspector missing' }
$imports = @(& $objdump -p (Join-Path $package 'FastECU.exe') |
    Select-String '^\s*DLL Name:' | ForEach-Object { ($_.Line -replace '^\s*DLL Name:\s*','').Trim() })
if ($LASTEXITCODE -ne 0 -or $imports.Count -eq 0) { throw 'Cannot inspect packaged executable imports' }
$imports | Set-Content (Join-Path $logs 'application-imports.txt')
foreach ($dll in $imports) {
    if ($dll -match '^(Qt5|libgcc|libstdc|libwinpthread)') { $required += $dll }
}
Write-Output ("::notice title=Packaged application imports::" + ($imports -join ', '))
foreach ($file in $required) {
    if (-not (Test-Path (Join-Path $package $file))) { throw "Missing package member: $file" }
}
Get-ChildItem $package -Recurse -File | ForEach-Object { $_.FullName.Substring($package.Length + 1) } |
    Set-Content (Join-Path $logs 'package-contents.txt')
# Relocate the entire package to a path with spaces, outside the build/source tree.
$relocated = Join-Path ([IO.Path]::GetTempPath()) ('FastECU arrival ' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $relocated | Out-Null
Copy-Item "$package/*" $relocated -Recurse
$working = Join-Path $relocated 'unrelated working directory'
New-Item -ItemType Directory $working | Out-Null
$previous = @{}
foreach ($name in @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QT_STYLE_OVERRIDE')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name)
}
function Invoke-Smoke([string]$Name, [string[]]$Arguments, [int]$Expected = 0) {
    $p = Start-Process -FilePath (Join-Path $relocated 'FastECU.exe') -ArgumentList $Arguments `
        -WorkingDirectory $working -PassThru -RedirectStandardOutput (Join-Path $logs "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $logs "$Name.stderr.log")
    if (-not $p.WaitForExit(45000)) {
        # Only the hardware-disabled GUI test process is terminated on timeout.
        $p.Kill(); $p.WaitForExit()
        throw "$Name hung beyond 45 seconds"
    }
    $p.WaitForExit()
    if ($p.ExitCode -ne $Expected) { throw "$Name exited $($p.ExitCode), expected $Expected; see captured logs" }
}
try {
    $env:PATH = "$relocated;$env:SystemRoot/System32;$env:SystemRoot"
    Remove-Item Env:QT_PLUGIN_PATH,Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_STYLE_OVERRIDE = 'Fusion'
    Invoke-Smoke 'relocated-help' @('--help')
    $result = Join-Path $logs 'packaged-gui.json'
    Invoke-Smoke 'packaged-gui' @('--gui-smoke-test', "`"$result`"")
    $r = Get-Content $result -Raw | ConvertFrom-Json
    if (-not $r.adapter_manager_menu -or -not $r.adapter_manager_visible -or -not $r.adapter_manager_screenshot -or -not $r.ok -or -not $r.mainwindow_constructed -or -not $r.window_visible -or -not $r.window_exposed -or
        -not $r.window_closed -or -not $r.mainwindow_destroyed -or $r.heartbeats -lt 20 -or $r.paint_events -lt 1 -or
        -not $r.hardware_opening_disabled -or $r.remote_enabled -or $r.platform -ne 'windows') {
        throw 'GUI did not establish native visible/responsive/closed state'
    }
    # Refusal paths return before any driver enumeration/load/device call.
    $refused = Join-Path $logs 'identity-refused.json'
    Invoke-Smoke 'identity-no-authorization' @('--sh7058-identify', 'unreviewed', '--identity-output', "`"$refused`"") 2
    $i = Get-Content $refused -Raw | ConvertFrom-Json
    if ($i.ok -or $i.programming_allowed -or $i.detail -notlike '*no device opened*') { throw 'Identity authorization guard failed' }
    $mixed = Join-Path $logs 'identity-mixed.json'
    Invoke-Smoke 'identity-smoke-conflict' @('--gui-smoke-test', "`"$mixed`"", '--sh7058-identify', 'unreviewed') 2
    if (Test-Path $mixed) { throw 'Mixed identity/smoke modes unexpectedly started' }
    # Missing defaults must fail explicitly, never silently pass via resources.
    $config = Join-Path $relocated 'config/protocols.cfg'
    Move-Item $config "$config.saved"
    try {
        $negative = Join-Path $logs 'missing-config.json'
        Invoke-Smoke 'missing-config' @('--gui-smoke-test', "`"$negative`"") 1
        $n = Get-Content $negative -Raw | ConvertFrom-Json
        if ($n.ok -or $n.error -notlike '*Packaged config missing*') { throw 'Missing config did not fail as expected' }
    } finally { Move-Item "$config.saved" $config }
    $summary = "Native packaged GUI: visible/exposed/painted; $($r.heartbeats) event-loop heartbeats; normal close and destruction passed. Relocated help, missing-config rejection and identity authorization/mixed-mode refusals passed. No device opening or remote connection."
    $summary | Set-Content (Join-Path $logs 'gui-summary.txt')
    Write-Output "::notice title=Native packaged GUI results::$summary"
} catch {
    $detail = $_.Exception.Message
    $stderr = Join-Path $logs 'packaged-gui.stderr.log'
    if (Test-Path $stderr) { $detail += "`n" + ((Get-Content $stderr | Select-Object -Last 10) -join "`n") }
    if ($detail.Length -gt 3500) { $detail = $detail.Substring($detail.Length - 3500) }
    $detail = $detail.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Output "::error title=Native packaged GUI failure::$detail"
    throw
} finally {
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name]) }
    Remove-Item $relocated -Recurse -Force
}

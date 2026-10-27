#Requires -Version 7.2
# Disposable native CI fixtures only. Never loads a registered DLL.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][ValidateSet('x64','x86')][string]$Architecture,
      [Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if (-not $IsWindows) { throw 'Native Windows required' }
$package = (Resolve-Path $PackageDirectory).Path
$logs = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force $logs | Out-Null
$copy = Join-Path ([IO.Path]::GetTempPath()) ('FastECU diagnostic ' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $copy | Out-Null
Copy-Item "$package/*" $copy -Recurse
$fixture = 'FastECU-006P-' + [guid]::NewGuid().ToString('N')
$view = if ($Architecture -eq 'x64') { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 }
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
$keyPath = "SOFTWARE\PassThruSupport.04.04\$fixture"
$saved = $env:PATH
function Invoke-Passive([string]$Label, [string[]]$Extra = @()) {
    $report = Join-Path $logs "$Label.json"
    $summary = Join-Path $logs "$Label.txt"
    & (Join-Path $copy 'fastecu-j2534-diagnostics.exe') --output $report --summary $summary @Extra |
        Set-Content (Join-Path $logs "$Label.stdout.log")
    if ($LASTEXITCODE -ne 0) { throw "Packaged passive check $Label failed: $LASTEXITCODE" }
    $r = Get-Content $report -Raw | ConvertFrom-Json
    if (-not $r.ok -or $r.device_opened -or $r.ecu_communication -or $r.test_build -or
        $r.stage -ne 'A_passive' -or -not (Get-Content $summary -Raw).Contains('hardware compatibility remains UNTESTED')) {
        throw "Packaged report $Label exceeded passive scope or failed summary/export"
    }
    return $r
}
try {
    $env:PATH = "$copy;$env:SystemRoot/System32;$env:SystemRoot"
    & (Join-Path $copy 'fastecu-j2534-diagnostics.exe') --help | Set-Content (Join-Path $logs 'relocated-help.log')
    if ($LASTEXITCODE -ne 0) { throw 'Relocated help failed' }
    $initial = Invoke-Passive 'initial-passive'
    $key = $registry.CreateSubKey($keyPath)
    $key.SetValue('Name', 'Synthetic passive CI fixture (never loaded)')
    $key.SetValue('Vendor', 'FastECU offline tests')
    $path = Join-Path $copy 'synthetic.dll'
    $key.SetValue('FunctionLibrary', $path)
    try {
        $missing = Invoke-Passive 'missing-dll'
        $entry = @($missing.drivers | Where-Object { $_.id -eq "${Architecture}:04.04:$fixture" })
        if ($entry.Count -ne 1 -or $entry[0].issue -notlike '*missing or inaccessible*') { throw 'Missing DLL not diagnosed' }
        [IO.File]::WriteAllText($path, 'not an executable')
        $malformed = Invoke-Passive 'malformed-dll'
        $entry = @($malformed.drivers | Where-Object { $_.id -eq "${Architecture}:04.04:$fixture" })
        if ($entry[0].issue -notlike '*Not a Windows PE DLL*') { throw 'Malformed DLL not diagnosed' }
        foreach ($bits in @('x86','x64')) {
            $bytes = [byte[]]::new(512)
            $bytes[0] = 0x4d; $bytes[1] = 0x5a; $bytes[60] = 64; $bytes[64] = 0x50; $bytes[65] = 0x45
            if ($bits -eq 'x86') { $bytes[68] = 0x4c; $bytes[69] = 1; $bytes[84] = 96; $bytes[89] = 1 }
            else { $bytes[68] = 0x64; $bytes[69] = 0x86; $bytes[84] = 112; $bytes[89] = 2 }
            $bytes[70] = 1; $bytes[87] = 0x20; $bytes[88] = 0x0b
            [IO.File]::WriteAllBytes($path, $bytes)
            $pe = Invoke-Passive "pe-$bits"
            $entry = @($pe.drivers | Where-Object { $_.id -eq "${Architecture}:04.04:$fixture" })
            if ($entry[0].architecture_from_header -ne $bits) { throw 'Wrong PE classification' }
            if ($bits -ne $Architecture -and $entry[0].issue -notlike '*architecture does not match*') { throw 'Architecture mismatch not diagnosed' }
            if ($bits -eq $Architecture -and $entry[0].issue) { throw 'Matching fixture unexpectedly rejected' }
        }
    } finally { $key.Dispose(); $registry.DeleteSubKeyTree($keyPath, $false) }
    $summary = "Packaged $Architecture diagnostics: relocated help, initial passive discovery, missing DLL, malformed DLL, x86/x64 PE inspection, JSON/text export and clean exit passed. Synthetic registry fixture removed; no DLL loaded."
    $summary | Set-Content (Join-Path $logs 'packaged-summary.txt')
    Write-Output "::notice title=Packaged passive diagnostic results::$summary"
} catch {
    $detail = $_.Exception.Message.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Output "::error title=Packaged passive diagnostic failure::$detail"
    throw
} finally {
    $registry.DeleteSubKeyTree($keyPath, $false)
    $registry.Dispose()
    $env:PATH = $saved
    Remove-Item $copy -Recurse -Force
}

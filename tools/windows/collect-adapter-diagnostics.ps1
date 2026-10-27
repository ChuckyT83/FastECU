#Requires -Version 5.1
# Passive only; this wrapper has no device-open or ECU communication option.
[CmdletBinding()]
param([ValidateSet('x64','x86')][string]$Architecture,
      [ValidateSet('obdx-pro-vx','openport-clone','unreviewed')][string]$Adapter='unreviewed',
      [string]$OutputDirectory=(Join-Path $PSScriptRoot 'reports'),[switch]$AllUsb)
$ErrorActionPreference='Stop'
$localMarker=Join-Path $PSScriptRoot 'toolkit-architecture.txt'
if(-not $Architecture){$Architecture=if(Test-Path $localMarker){(Get-Content $localMarker -Raw).Trim()}else{'x64'}}
$exe=Join-Path $PSScriptRoot "diagnostics/$Architecture/fastecu-j2534-diagnostics.exe"
if(-not(Test-Path $exe)) {
    if(-not(Test-Path $localMarker) -or (Get-Content $localMarker -Raw).Trim() -ne $Architecture){throw 'Requested diagnostic architecture is not included'}
    $exe=Join-Path $PSScriptRoot 'fastecu-j2534-diagnostics.exe'
}
if(-not(Test-Path $exe)){throw 'Diagnostic executable missing; extract the complete toolkit first'}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$name=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+"-$Adapter-$Architecture.json"
$report=Join-Path $OutputDirectory $name
$summary=[IO.Path]::ChangeExtension($report, '.txt')
$arguments=@('--adapter-kind',$Adapter,'--output',$report,'--summary',$summary)
if($AllUsb){$arguments+='--usb-all'}
& $exe @arguments
$code=$LASTEXITCODE
if(Test-Path $summary){Get-Content $summary | Write-Host}
Write-Output "Local report: $report; readable summary: $summary (review JSON before sharing; device instance suffixes and paths redacted by default)"
exit $code

#Requires -Version 7.2
[CmdletBinding()]
param([string]$BuildDirectory='build-usb-qualification')
$ErrorActionPreference='Stop'
$qt=(& qmake.exe -query QT_INSTALL_PREFIX).Trim()
$compiler=Join-Path (Split-Path (Split-Path $qt)) 'Tools/mingw810_64/bin'
$env:PATH="$compiler;$env:PATH"
New-Item -ItemType Directory -Force $BuildDirectory | Out-Null
$archive=Join-Path $BuildDirectory 'libusb.7z'
Invoke-WebRequest 'https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.7z' -OutFile $archive
if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '964a38152ca9a104cd00ec8d2f0617b89cd814f9b635e29763c68563d951521d'){throw 'Unexpected libusb archive'}
& 7z.exe x -y "-o$BuildDirectory/libusb" $archive
if($LASTEXITCODE){throw 'libusb extraction failed'}
& "$env:pythonLocation/python.exe" tools/openport_usb_driver/build.py --cc gcc --libusb "$BuildDirectory/libusb" --output "$BuildDirectory/driver-build" --native-mocks
if($LASTEXITCODE){throw 'Driver build/fake USB tests failed'}
$package=Join-Path $BuildDirectory 'driver-package'
New-Item -ItemType Directory -Force $package | Out-Null
foreach($file in @('j2534.dll','libusb-1.0.dll','LICENSE-driver','DRIVER_SHA256.txt','build-manifest.json','adapter-manifest.json')) {Copy-Item "$BuildDirectory/driver-build/$file" $package}
Copy-Item docs/openport-usb-qualification.md $package
Invoke-WebRequest 'https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.tar.bz2' -OutFile "$package/libusb-1.0.29.tar.bz2"
if((Get-FileHash "$package/libusb-1.0.29.tar.bz2" -Algorithm SHA256).Hash.ToLowerInvariant() -ne '5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85'){throw 'Unexpected libusb source'}
& "$env:pythonLocation/python.exe" -c "import tarfile,pathlib; p=pathlib.Path(r'$package'); t=tarfile.open(p/'libusb-1.0.29.tar.bz2'); (p/'LICENSE-libusb').write_bytes(t.extractfile('libusb-1.0.29/COPYING').read())"
if($LASTEXITCODE){throw 'libusb license extraction failed'}

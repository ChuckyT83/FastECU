# Windows offline development build

Task 006M prepares a full GUI build separately from the native MSVC production-
component regression workflow. The new full-build job has **not been executed**
while the Task 006M revision is unpublished. No Windows executable is claimed yet.

Use Windows Server 2022/Windows 10 or 11 x64, Qt 5.15.2 `win64_mingw81` including
Charts/RemoteObjects/WebSockets/SerialPort, and Qt's matching MinGW 8.1 x64 tools.
Legacy modules use GNU variable-length array extensions; this avoids a broad MSVC
portability rewrite. The existing MSVC x64 regression job remains unchanged.

The full application also needs OpenSSL crypto. The project now takes a matching
architecture `OPENSSL_PREFIX` qmake argument (or `FASTECU_OPENSSL_ROOT`) rather
than linking a hardcoded Win32 DLL before the application's object files. A
32-bit OpenSSL or J2534 DLL cannot be loaded/linked into this x64 process.

The prepared workflow `.github/workflows/windows-development-build.yml` pins
checkout/Qt/Python/artifact actions and official OpenSSL 3.5.5 source at
`67b5686b4419b4cb8caa502711c41815f5279751`. It builds static crypto from source,
then the full application, deploys Qt/compiler runtimes with `windeployqt`, and
runs the packaged executable's `--help` after removing SDK paths. `qt.conf` fixes
plugin lookup to the package. The script requires native Windows and PowerShell
7.2+. A matching Qt toolchain and Perl must already be available.

```powershell
./tools/windows/package-development.ps1 -OpenSslSource C:/deps/openssl-source `
    -BuildDirectory C:/build/FastECU-development
```

Alternatively, after building the matching crypto dependency:

```powershell
qmake.exe C:/src/FastECU/FastECU.pro CONFIG+=release CONFIG-=debug_and_release `
    OPENSSL_PREFIX=C:/deps/openssl-x64
mingw32-make.exe -j2
```

Logs record compiler/Qt versions, failures, dependency revision and package hashes.
Only a successful build/smoke job publishes the development package; failed jobs
retain diagnostic logs. No vendor J2534 DLL, private BIN, EcuFlash application,
firmware research extract or private configuration is packaged. Redistribution
must retain project and dependency licenses/notices and corresponding source
availability; review the actual native artifact before distributing it.

The packaged `--help` smoke exercises PE loading and Qt bootstrap without
constructing MainWindow or enumerating/opening an adapter. It does not verify
full MainWindow rendering, remote TLS, adapter communication or flashing. Qt
5.15.2's TLS backend is separate from the statically linked OpenSSL 3 crypto:
remote TLS remains unqualified by this build and must not be assumed functional.

The package is labelled **offline development, not vehicle-ready**. All SH7058
upload/write/test-write gates and the real candidate-link block remain active.
Use the existing diagnostic utility for passive driver discovery and, only with
separate hardware authorization, device-only Open/ReadVersion/Close. Install an
official x64 04.04 OBDX J2534 driver for an x64 application; discovery/ABI checks
and a successful mock DLL do not prove VX hardware compatibility.

Task 006M must not be pushed or dispatched without separate owner authorization.
Publishing the reviewed development commit would trigger both the native MSVC
regressions and the prepared full MinGW application job; older runs do not
validate these new changes.

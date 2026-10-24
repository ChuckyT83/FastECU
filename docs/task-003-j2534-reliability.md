# Task 003: J2534 initialization reliability

## Baseline and scope

Existing checkout: `https://github.com/ChuckyT83/FastECU.git`, initially clean on
`work`, HEAD `d5f8c25d27608253a45c7e5c05c4eb65166c400a`.
Development branch: `fix/j2534-initialization-reliability`.
Both fetched `origin/master` and the requested upstream
`miikasyvanen/FastECU` master match that reference. No newer master changes or
conflicts were found. No Task 001/002 reports were available in this checkout or
conversation; the supplied Task 003 findings were used.

Reviewed upstream [PR #157](https://github.com/miikasyvanen/FastECU/pull/157), commit
`86005b81231d601944c06dc652f1143a469a1d1e`, via its read-only Git PR ref.
The PR initializes metadata storage, reserves a serial terminator byte, and
removes unconditional deletion of the final character. Those concerns are
addressed here without cherry-picking or duplicating the PR. This implementation
also bounds version-response termination checks and replaces the disabled vendor
serial-query scaffold with an explicit empty/unavailable result. PR #157 alone
would still report the disabled serial query as successful. It contains no
initialization/cleanup/loader fixes. Future merging of the overlapping metadata
hunks will require resolving textual overlap, preserving these semantics.

## Changes

- `serial_port_actions_direct.cpp/.h`: acquire device and channel ownership only
  after the corresponding successful API call, independently from readiness.
  Remove synthetic device-ID increments. Zero can be a valid returned handle.
- Check Connect, configuration IOCTLs, filter clearing and filter creation in
  sequence; stop immediately on failure. Existing DST-i buffer-clear operations
  are now executed and checked during setup rather than skipped because the
  public adapter-ready flag was still false. No timing values are changed.
- Cleanup disconnects the acquired **channel**, closes the acquired **device**,
  closes the Linux transport or unloads the Windows DLL, and clears state.
  It runs on failed/partial initialization, explicit reset, repeat initialization
  and destruction. Each acquired resource is released once per cleanup, even if
  Disconnect fails. An invalid/error-returned output handle is never treated as
  acquired. A driver refusing both Disconnect and Close can still retain external
  resources; unloading/resetting local state cannot guarantee vendor recovery.
- `unset_j2534_can` now disconnects `chanID`, not `devID`. `READ_VBATT` uses the
  device ID as required by J2534. Public channel helpers reject use after failure.
- Preserve the registered DLL path rather than replacing it with the vendor
  display name. Retain local-DLL precedence and registered-path fallback on a
  local DLL loading/export-resolution failure. Eliminate the double-open probing
  stage from ordinary open; a selected J2534 failure no longer falls through to
  an unrelated serial-port attempt. Empty/missing serial descriptions are safe.
- Zero-initialize version buffers; accept empty strings without underflow, retain
  all returned characters, reject no terminator in the 80-byte version contract.
  Unsupported optional versions/serial metadata are reported as unavailable;
  other version errors fail initialization and release the device.
- Diagnostics retain operation name, numeric primary J2534 status, and bounded
  driver text. Unavailable driver error text does not replace the primary status.
- `J2534_win.cpp/.h`: initialize/reset function pointers and error storage, unload
  in the destructor, reset on disable, report missing export names and Windows
  loader status, reject null/empty/overlong DLL names, bound version debug reads
  and debug formatting. Disable/reload does not reuse stale exports.
- `get_is_tx_done` returns false on Windows instead of reaching the end of a
  non-void function; this does not add a new Windows transmission-status feature.

CAN identifiers, protocol choices, baud rates, timing parameters, payload/filter
semantics, flashing routines, security access and checksums are unchanged.
Zero-initializing unused filter storage does not change the declared DataSize.
No ROM/XML files, CI workflows, ECU programming code or programming kernels were
modified or used. No physical adapters/ECUs were accessed. No remote CI was run.

## Validation

See `tests/j2534/README.md` for test boundaries and reproduction commands.

- Full native Linux release application build: passed with the prepared Qt 5.15.8
  toolchain and `make -j4`. Existing unrelated compiler warnings remain.
- Shared production initialization suite: 40 passed, 0 failed, 0 skipped
  (includes QtTest init/cleanup cases).
- ASan + UBSan suite under Xvfb/X11: 40 passed, process exited successfully with
  sanitizers and leak detection enabled. Initial offscreen-plugin run passed
  assertions but exited nonzero for 368 bytes in seven Qt display allocations;
  switching to X11 resolved that report without suppressions.
- Production Windows-loader logic via the Linux bridge: 33 checks passed, also
  under ASan/UBSan with leak detection enabled.
- Native Windows test sources and projects added, but not compiled/executed here:
  Linux host lacks a Windows compiler/SDK, Windows Qt libraries and Windows runtime
  (no MSVC, MinGW cross-compiler or Wine). ELF loader tests cannot substitute for
  Windows calling conventions, registry discovery, DLL loading or hardware tests.
- Static review and `git diff --check` performed. GCC `-fanalyzer` was run on
  the production Windows wrapper using the test bridge and on the shared
  production connection code. The wrapper emitted only the pre-existing inactive
  macOS syntax warning. The shared-code analysis emitted 21 unknown-value
  diagnostics in Qt QString/QList/QObject return-value/container paths (including
  the unchanged hex-formatting helper), plus existing unused warnings. These are
  not a clean static-analysis result; the initialized metadata/error buffers were
  also reviewed and exercised under sanitizers. Raw reports remain at
  `/workspace/fastecu-env/task003-analyzer.log` and
  `/workspace/fastecu-env/task003-direct-analyzer.log`.

The implementation is tested offline on Linux; the Windows integration remains
**unverified** until the native tests execute. Passing mocks does not prove
OBDX Pro VX compatibility or readiness for ECU flashing.

## Proposed Windows Actions workflow (not installed or executed)

The repository has no existing `.github` workflows. The current runner-images
Windows 2022 manifest lists Visual Studio 2022/MSBuild. A `windows-2022` x64 runner
can build the test DLLs and test targets after installing matching Qt 5.15.2 MSVC
binaries. Tests need Qt Core/Widgets/SerialPort/Test; they do not link application
OpenSSL or Qt Remote Objects. A full Windows application build additionally needs
Charts, WebSockets, Remote Objects and a compatible OpenSSL development setup;
its existing GNU-style OpenSSL link flags need separate assessment for MSVC.

This is a review proposal only. Adding the following workflow or triggering it
requires explicit authorization. Dependency actions would be pinned to reviewed
commit SHAs before installation in the repository.

```yaml
name: Offline Windows J2534 regression
on: workflow_dispatch
permissions:
  contents: read
jobs:
  j2534:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
      - uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: x64
      - uses: jurplel/install-qt-action@v4
        with:
          version: '5.15.2'
          arch: win64_msvc2019_64
          cache: true
      - name: Build offline mock DLLs and tests
        shell: pwsh
        run: |
          $source = (Get-Location).Path
          New-Item -ItemType Directory build-j2534 | Out-Null
          Set-Location build-j2534
          cl /nologo /std:c++14 /EHsc /MD /LD "$source/tests/j2534/mock_dll.cpp" /Fe:mock-j2534.dll
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          cl /nologo /std:c++14 /EHsc /MD /DOMIT_IOCTL /LD "$source/tests/j2534/mock_dll.cpp" /Fe:incomplete-j2534.dll
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          cl /nologo /std:c++14 /EHsc /MD /D_CRT_SECURE_NO_WARNINGS "$source/tests/j2534/loader.cpp" "$source/serial_port/J2534_win.cpp" /Fe:loader-test.exe
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          ./loader-test.exe "$PWD/mock-j2534.dll" "$PWD/incomplete-j2534.dll"
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          qmake "$source/tests/j2534/windows.pro" CONFIG+=release CONFIG-=debug_and_release DESTDIR=.
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          nmake
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          $env:QT_QPA_PLATFORM = 'offscreen'
          ./j2534-windows-reliability.exe
          exit $LASTEXITCODE
```

## Remaining defects and recommended Task 004

1. Run the native x64 Windows integration and loader tests, then add x86 coverage
   for stdcall export decoration/driver bitness if required by the target adapter.
   Registry discovery needs dedicated Windows tests; the added public-selection
   tests also need native execution.
2. Audit runtime message operations separately: ignored WriteMsgs/periodic-message
   statuses, message lengths/counts, timeout/partial-transfer handling, receive
   bounds, five-baud-init response lengths, and programming-voltage return codes.
   Existing debug message dump/format issues and inactive macOS loader syntax also
   remain outside this initialization task.
3. Review the Linux serial-backed J2534 implementation separately: Open/Connect
   currently fail to write their output pointer values; the caller's existing
   channel-as-protocol compatibility override is intentionally retained here.
   Hardware availability is not established by local readiness flags alone.
4. Preserve the owner's Base.bin and definitions. Do not infer programming safety
   from these mocks. Task 004 should first validate native offline Windows tests
   and generic transport error handling. Any subsequent OBDX Pro VX evaluation
   needs a separately approved, non-programming hardware plan; no vendor-specific
   workaround, impersonation or ECU security/programming should be introduced.

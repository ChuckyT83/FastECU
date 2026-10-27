# Device-only diagnostic commands

Extract the full kit. In the combined development package, executables live in
`diagnostics/x64` and (when available) `diagnostics/x86`. In a standalone kit the
executable and Qt Core/compiler runtimes are beside this document. No source
checkout, GUI launch, administrator session or local compiler is required for
normal passive inspection. Installed-driver enumeration is read-only HKLM;
native tests alone create temporary HKCU fixtures.

```powershell
# Default: Stage A, registry/file metadata and candidate USB descriptions only
.\fastecu-j2534-diagnostics.exe --output .\passive-new.json --summary .\passive-new.txt

# Classify/hash an untrusted local file without loading/executing it
.\fastecu-j2534-diagnostics.exe --inspect-file 'C:\Intake\driver.dll' --output .\file-new.json

# Broader USB metadata, still no J2534 calls
.\fastecu-j2534-diagnostics.exe --usb-all --output .\usb-new.json

# Full local paths/USB instance identifiers: explicit disclosure, not the default
.\fastecu-j2534-diagnostics.exe --include-private-details --output .\private-new.json
```

`--summary <new-file.txt>` writes a concise human-readable report using the same
privacy settings as JSON. JSON also contains `human_summary`. The collection
script automatically creates timestamped `.json`/`.txt` pairs. A summary labels
missing registration, missing/malformed DLLs, PE bitness mismatches and hardware
that remains untested. An empty USB candidate list is not proof of physical
absence. Registry errors and refused requests are not compatibility success.

Existing reports are never overwritten. Invalid/unavailable output paths return
nonzero and preserve operation/error details on stdout. A completed empty driver
list returns 0 with `no_drivers_registered`; no compatibility is inferred. Every
report states: **Device-only J2534 diagnostics; no vehicle communication performed.**

File metadata includes size/SHA-256 and bounded PE inspection; Windows version
resources and offline Authenticode policy are recorded where available. Offline
signature checking does not fetch certificates or check revocation. Unsigned,
untrusted, inaccessible and malformed files are not collapsed into “connected.”
A signature result does not authenticate the physical adapter or qualify a DLL.
UNC/device-namespace paths are refused; use a reviewed local copy.

## Explicit reviewed OBDX USB-only operation

**Not a day-one default or a recommendation for an unreviewed driver.** Obtain
separate approval of the exact driver's load/open/firmware behavior first. The
tool cannot independently verify that review or physical adapter identity.
Close other tools and disconnect the adapter's vehicle/DLC cable.

Use the exact driver ID and SHA-256 from the matching-bitness passive report:

```powershell
.\fastecu-j2534-diagnostics.exe --validate-device 'x64:04.04:<exact-listed-key>' `
    --adapter-kind obdx-pro-vx --vehicle-disconnected --reviewed-open-behavior `
    --driver-sha256 '<exact-64-hex-reviewed-hash>' --output .\usb-operation-new.json
```

The hash pins the file selected for loading; it does not establish trust, firmware
safety or hardware compatibility. The worker rechecks it and denies writes or
replacement of that file while loading it. Dependent vendor libraries and
firmware behavior still require review. Known OpenPort/Tactrix entries and clone
mode are blocked even with these flags. Do not relabel a clone as OBDX.

Only DLL load, `PassThruOpen`, `PassThruReadVersion`, `PassThruClose` and unload
exist in the device API. It does not resolve Connect, WriteMsgs, filters,
periodic messages, IOCTLs, voltage, ECU reset/security/identification, ROM or
programming functions. Version failure/malformed metadata and cleanup failure
are reported separately from opening. An API-0 response is not ECU acceptance.

Calls run serially in a detached worker, not a cancellable driver thread. The
parent waits at most 30 seconds (`--wait-ms` accepts 100..30000). Expiry does not
terminate a call, close its handle concurrently or unload its executing DLL.
The result includes an ephemeral worker PID/token and uncertainty. No automatic
retry occurs. Do not open again or unplug during an uncertain call. Windows
shutdown, external process termination or a vendor crash remain outside this
host guarantee; process isolation is not a security sandbox for untrusted code.

```powershell
# Read an eventually completed result only; never repeats a driver operation
.\fastecu-j2534-diagnostics.exe --collect '<reported-worker-token>' --output .\late-review-new.json
```

A collected late result retains the prior timeout and returns review-required,
even if the worker subsequently closed successfully. Private worker data resides
in the current user's temporary directory; it is not automatically exported.

`--trace <new-file.jsonl>` is optional for Stage B. It records only the device API
calls/version strings, not CAN traffic. Review arbitrary vendor strings before
sharing. Trace-sink failure is reported without interrupting acquired-resource
cleanup. `--identification-plan` always refuses ECU communication (exit 2).

Exit 0 means the requested inspection or reviewed device lifecycle/metadata
operation completed as described. Exit 1 means operational/export failure or
uncertainty; exit 2 means refused/invalid/unsupported request. No exit code grants
programming permission. Check raw statuses and `version_ok`/`cleanup_confirmed`.

## Sources and runtime notices

The diagnostic is FastECU GPLv3 source; the accompanying `LICENSE` is retained.
Qt Core 5.15.2 and MinGW 8.1 runtime binaries are deployed with the kit. Their
upstream source/license information is available from:

- <https://github.com/qt/qtbase/tree/v5.15.2>
- <https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html>
- <https://www.mingw-w64.org/disclaimer/>

Build evidence records the exact FastECU revision, compiler/Qt configuration,
artifact digest and per-file SHA-256 manifest. No vendor driver, proprietary
EcuFlash software, standalone firmware or calibration image is redistributed.

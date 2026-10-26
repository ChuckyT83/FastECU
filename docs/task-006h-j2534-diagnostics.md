# Task 006H — Windows offline validation and J2534 diagnostics

Production Subaru SH7058 write/test-write and kernel upload remain disabled.
The B6 response/completion/startup contract is still unresolved. These tools do
not prove OBDX Pro VX, kernel or ECU programming compatibility.

## Windows build and tests

The pinned `.github/workflows/j2534-windows.yml` uses Windows Server 2022,
native x64 MSVC 2022 and Qt 5.15.2 `win64_msvc2019_64`. It builds the production
loader/transport, Subaru module and UI/checksum functions against mock backends,
the separate kernel model, diagnostics tests and the Qt Core-only utility.
Native Windows/Fusion remains configured for GUI safety tests; J2534 tests keep
offscreen. Action pins, read-only contents permission and failure artifacts remain.

In an x64 MSVC developer shell with this Qt kit in PATH:

```powershell
./tests/j2534/run-windows.ps1
```

The script fails on unexpected compiler/test exit codes. Identification refusal
is deliberately expected to exit 2 and is checked independently. Logs include
revision/toolchain/exact counts. Artifacts also include the diagnostic utility
with Qt runtime DLLs; Microsoft's matching Visual C++ runtime is still required.
The packaged utility is exercised with the no-operation identification plan.

This is an **isolated production-code test build**, not a full Windows GUI
application build. The full project has legacy MinGW-style linker settings,
OpenSSL paths and variable-length arrays in unrelated modules. Full application
Windows toolchain/dependency modernization is not claimed by passing these tests.
Full Linux application build and executed platform results are in the external
Task 006H report. Do not substitute historical runs for the exact revision.

To build just the utility outside the checkout:

```powershell
mkdir C:\build-fastecu-diagnostics
cd C:\build-fastecu-diagnostics
qmake C:\src\FastECU\tools\j2534_diagnostics\diagnostics.pro CONFIG+=release CONFIG-=debug_and_release DESTDIR=.
nmake /NOLOGO
```

## Optional application trace

Tracing is **disabled by default**. Enable it only when needed:

```text
FastECU.exe --j2534-trace C:\private-traces\new-session.jsonl
```

The parent directory must already exist; the file must not exist. The logger
never overwrites an existing file or deliberately follows a symlink/pipe. Use
a private local directory and one capture per application session. A restart
needs a new filename. File creation/permissions and Windows ACLs remain subject
to the filesystem; review access controls yourself.

**Raw traces can contain private calibration, identifiers or proprietary data.
Review before sharing.** Known ISO15765 security-access and VIN payloads are
omitted automatically, with declared length and redaction reason preserved.
The checked facade's short debug previews apply the same known-data filter.
This filter does not recognize every private value/protocol, especially raw CAN
segmentation. Driver error text, custom serial-number data and CLI passwords are
not copied into the trace. No switch disables the built-in privacy filter.
Other pre-existing application/UI logs may still contain private information;
this is not a universal redactor for the entire application.

The JSONL trace is captured at the actual J2534 API boundary, separately from
existing short UI/debug previews. It records UTC timestamp, sequence, API name,
direction, actual protocol/flags/driver timestamp, length, complete valid message
bytes, CAN-ID prefix when applicable, status/counts, exact supplied timeout,
channel/baud/ID context, configuration IOCTLs, filters, periodic operations,
versions and connection/cleanup events. Host validation failures are labelled
separately from actual API statuses. `api_success` means API return zero; check
returned counts and host outcome, not ECU acceptance.

This captures the local driver-owning process. A remote client with no local
J2534 backend has no local driver events to capture; the checked remote SH7058
operation contract remains Unsupported.

Example **synthetic** record (other context fields omitted here for readability):

```json
{"operation":"PassThruWriteMsgs","timestamp_utc":"2026-10-26T12:00:00.000Z","sequence":"7","direction":"tx","protocol_id":6,"can_id_prefix_hex":"000007e0","length":8,"data_hex":"000007e001020304","application_hex":"01020304","status":0,"requested_count":1,"returned_count":1,"timeout_ms":100,"outcome":"api_success"}
```

The four CAN-ID prefix bytes are API data, not part of the application payload.
UTC timestamps mark API completion/capture, not bus-wire time; read/write events
also measure API-call duration in `elapsed_us`. Driver timestamps are separate.
For ISO15765 the API exposes reassembled messages/notifications, **not a raw CAN
frame capture**. Configured protocol and actual driver-returned ProtocolID remain
separate even when they disagree. Raw CAN is not guessed to be reassembled UDS.
The derived application view excludes trailing extra data when the driver's
ExtraDataIndex is valid; invalid indices are labelled without altering raw bytes.
No-message results have count/timeout/status, without a fabricated payload.

All valid current single-message API calls capture their complete buffer up to
4,128 bytes. Invalid larger driver lengths are labelled and payload omitted;
the logger never reads beyond the buffer. Generic/custom IOCTL byte arrays and
vendor free-form error strings are not dumped. Filters/configuration are captured
structurally, with at most 32 configuration entries per record.

Memory is bounded: 128 queued records, 32 KiB per encoded record, approximately
4 MiB queue ceiling. A worker writes at most 16 MiB per session. Overload drops
records with drop counters; file-limit/write/open failures disable capture and
warn without changing transport results. Missing sequence/`trace_end`, nonzero
drops or `trace_failed` mean the evidence is incomplete. Do not infer a complete
protocol from a partial log. The queue lock contains no filesystem I/O; draining
and joining occur only after communication. Capture has CPU/scheduling overhead,
but does not change timeout parameters, deadline logic, request order or retries.
No additional protocol or driver query is issued to obtain trace data.

## Passive driver discovery

```powershell
fastecu-j2534-diagnostics.exe
```

Discovery only reads HKLM `PassThruSupport.04.04` and `.05.00`, in both registry
views, and passively checks PE files. **No DLL is loaded, no adapter opened.**
Output contains stable selection ID, description, registered and normalized DLL
paths, registered API/view, PE machine and issues. Missing/malformed/inaccessible
entries are retained rather than silently disappearing. Registry read errors
are reported. An empty/error discovery exits nonzero. API 05.00 may be listed
but its device API is not implemented. Registered version is not proof of conformance.

An x64 process can only validate an x64 DLL; x86 and ARM64 mismatches are reported.
The existing loader also requires a short ANSI-representable path. No alternate
vendor/CWD DLL is substituted. A registered path is not a trust/signature check;
obtain the actual matching VX driver from its official publisher. No OBDX paths,
capabilities or private enumeration commands are hardcoded.

## Explicit device-only validation

Adapter physically disconnected from vehicle/ECU, using documented power only:

```powershell
fastecu-j2534-diagnostics.exe --validate-device "x64:04.04:<listed-key>" --vehicle-disconnected --trace C:\private-traces\new-device.jsonl
```

Use the exact ID printed by discovery. The acknowledgement is a user assertion,
not a sensor that detects a vehicle. Loading a trusted vendor DLL executes vendor
code. Stop on unexpected firmware-update requests or undocumented power needs.
This task did not open a real vendor DLL/adapter; CI uses mock DLLs only.

The utility can only load, Open, ReadVersion, Close and unload. It has **no channel,
filter, ECU read/transmit, voltage, reset, security or flashing capability** in
its device interface. Zero handles are treated as valid on successful Open.
Version failure/unsupported metadata remains distinct from Open/Close success.
Close is attempted once for every successfully opened device, including metadata
failure. A Close failure reports unconfirmed cleanup/state, never a false pass.

`device_open_close_only` confirms only those operations. It does not validate
ISO15765, CAN traffic, ECU identity, kernel compatibility or programming safety.
Load/Open/Close failures are nonzero; unavailable tracing is separately reported.

## ECU identification remains structurally blocked

```powershell
fastecu-j2534-diagnostics.exe --identification-plan
```

This exits 2, reports no approved request/allowed requests, and returns **before
registry, DLL, device or channel operations**, even if other options are supplied.
No safe target-qualified request has been established in this task. The utility
does not link the Subaru flashing module or facade and its device interface
cannot connect/transmit; UI restrictions are not the safety boundary.

Do not use normal ROM Read/`connect_bootloader` as identification: that path can
perform sessions/security before reaching its upload gate. Mode 09/AA source
fragments and another Subaru's successful read do not authorize a live sequence.

## First OBDX Pro VX Windows checklist

1. **Discovery first:** record utility/build architecture, registry view/API,
   exact official driver version/path and PE architecture. Stop on ambiguity,
   missing DLL/dependency/export or architecture mismatch. No ECU is required.
2. **Device-only validation:** vehicle/DLC disconnected; one intended adapter,
   documented power, explicit Open/version/Close. Stop on update request, any
   failure, ambiguous device or unconfirmed cleanup. Capture versions/statuses.
3. Verify optional trace is actually created, complete and private; review before
   sharing. Capture failures do not interrupt transport, so inspect the warnings.
4. Native offline tests at the exact revision must pass. The new diagnostics
   tests never prove real VX behavior.
5. **Stop before ECU communication.** A separate reviewed whitelist/probe and
   complete logging evidence are required for the target. ISO15765 flow-control
   filters can transmit, even before an application request.
6. Keep kernel upload and write/test-write disabled. B6 grammar/correlation,
   completion/startup, kernel provenance/capabilities, exact ECU/BIN/XML/checksum
   policy and practical recovery evidence remain prerequisites for any later
   programming review. Do not make the owner's only ECU the first erase/write test.

# Reviewed custom OpenPort USB-only qualification

This is a separate driver/tooling milestone, not Task 006Q or a vehicle-ready
flasher. Do not run the physical command until a separate USB-only test approval.
First check the supplied validation status: a cross-build is not a native Windows
CI pass. Do not proceed to physical qualification while required native validation
remains pending. The adapter must be USB-connected, physically disconnected from the vehicle/DLC,
and unused by RomRaider, FastECU, EcuFlash or any other adapter application.
No driver installation, registry change or WinUSB rebinding is required.

## Separate packages

Extract the diagnostic package to `C:\OpenPortUSBQual\tool` and the corrected
custom driver package to `C:\OpenPortUSBQual\driver`. Preserve the DLLs currently
installed in System32/SysWOW64. Do not replace/register them. The tool and driver
must come from the same build/pin pair. The driver package contains
`DRIVER_SHA256.txt` and a build/source manifest. Review its source/pin before use.
Neither the original upstream/custom DLL nor the earlier diagnostic candidate is
admitted by this mode. Proprietary Tactrix DLLs are not admitted.

These commands use CMD, not PowerShell. Passive file checking, if desired:

```cmd
cd /d C:\OpenPortUSBQual
tool\fastecu-j2534-diagnostics.exe --inspect-file "%CD%\driver\j2534.dll" --output driver-file-new.json --summary driver-file-new.txt
```

After separate physical-test approval, exact one-attempt command:

```cmd
cd /d C:\OpenPortUSBQual
set /p DRIVER_SHA256=<driver\DRIVER_SHA256.txt
tool\fastecu-j2534-diagnostics.exe --qualify-openport-usb-only "%CD%\driver\j2534.dll" --driver-sha256 "%DRIVER_SHA256%" --vehicle-disconnected --reviewed-open-behavior --wait-ms 15000 --output usb-once-new.json --summary usb-once-new.txt --trace usb-once-new.jsonl
```

Use fresh output names. The tool refuses existing report/trace files. The flags
are acknowledgments of actual conditions and driver review, not automatic proof
that the vehicle is disconnected. Do not relabel this adapter as OBDX or use the
general `--validate-device` path; general OpenPort/clone/Tactrix refusal remains.

The only driver calls in this mode are Open once, ReadVersion once after a
confirmed initialization handshake, and Close once after successful Open.
GetLastError is used only for failure details. Failed Open uses the reviewed
internal resource-cleanup path; the host does not invent a DeviceID or call
Close on an unsuccessful Open. No Connect, messages, filters, periodic APIs,
IOCTLs, programming voltage, firmware update, ECU reset/identification or ROM
operation is resolved or called by this device API.

The reviewed driver sends adapter-only `ati`, `ata` and, on Close, `atz` commands.
No bootloader, nonvolatile-write or firmware-image command was added. This is
active adapter initialization/shutdown even without a vehicle attached.

## Evidence and success

The parent hash-checks the selected x64 driver and colocated libusb dependency.
The isolated worker repeats those checks while denying replacement/writes, loads
the pinned libusb explicitly using restricted dependency search, and obtains
exclusive ownership among qualification workers in the Windows session. libusb
interface claiming separately enforces the available USB ownership. Close other
applications before this test; the software cannot enforce that an unrelated
application has no earlier device activity.

USBQUAL/1 markers from the hash-pinned driver distinguish:

- Actual ati identification reply and ata acknowledgment, not only Open==0.
- A usable firmware/DLL/API version; empty/unavailable metadata is rejected.
- Adapter shutdown acknowledgment from the atz transaction.
- libusb interface-release return status, independently of shutdown status.
- Return from the void libusb_close/libusb_exit calls. These returns are **not**
  independent observation of Windows kernel handle release; the JSON says so.

Both successful acknowledgment and resource-cleanup evidence are required for a
passing custom qualification result. Malformed/missing/duplicated evidence,
version failure and any shutdown/release failure prevent a pass. Original error
status/text is retained when later cleanup also fails. Raw lifecycle logs remain
in the worker's per-user temporary output directory; exported JSON contains the
bounded evidence interpretation. Review vendor/version text before sharing.

The parent waits at most the selected budget and never kills an in-flight driver
call, closes concurrently or unloads its executing DLL. A pending/crashed worker
means uncertainty: **stop; no automatic retry, reopen, unplug or recovery command**.
The isolated worker retains its file locks and ownership until it returns.
A late result can be collected without repeating any adapter call:

```cmd
tool\fastecu-j2534-diagnostics.exe --collect REPORTED_WORKER_TOKEN --output late-review-new.json
```

A late result preserves the prior timeout and requires review even if eventual
cleanup succeeds. Normal exit 0 means only the requested USB lifecycle passed;
exit 1 means operation/export failure or uncertainty; exit 2 means refusal.
No result grants ECU communication or programming permission.

## Build/test evidence

`tools/openport_usb_driver` is the separate BSD-licensed upstream derivative;
its source-revision.json pins reviewed files and libusb 1.0.29. The existing
fake-USB harness covers initialization failures, partial acquisition, short
writes, Close timeout, release failure, combined failure/first-error preservation
and duplicate ownership/Close. Linux sanitizer runs are logic checks, not Windows
or hardware validation.

The existing Qt diagnostic unit/CLI/loader tests remain, with added custom-mode
policy, lifecycle, unavailable firmware, fixture override rejection, exclusive
worker and late-result checks. A separately compiled test executable may admit
synthetic mock hashes; production ignores those runtime environment variables.
Only the production executable is packaged.

The `openport-usb-qualification.yml` Windows workflow builds source-manifest-
checked driver input, executes fake-USB tests, runs the existing native diagnostic
runner and packages driver/tool separately. Its production tool receives the
exact driver artifact SHA-256 at build time, never from a runtime trust override.
A cross-build is not a native Windows CI pass. Consult the delivered evidence
report for which checks actually ran; do not infer a physical pass from artifacts.

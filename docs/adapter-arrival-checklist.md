# Adapter arrival — October 28, 2026

Use the dedicated diagnostic executable. **Do not connect either adapter to the
STI, a powered ECU or any vehicle. Do not use FastECU's ROM Read, Write or Test
Write actions.** ROM Read is not the passive diagnostic mode: it can enter
diagnostic/security sessions. SH7058 programming gates remain in place.

## Quick sequence

1. With both adapters disconnected, extract and hash-check the reviewed package.
   Run the standalone passive diagnostic first; no driver or attached cable is
   needed. The full GUI is optional for arrival checks. Do not use its ROM Read.
2. OBDX Pro VX: preserve/hash/signature-check the official VX installer; review
   installation and firmware behavior, then install only reviewed official
   drivers in the manufacturer's order. An independently approved USB-only
   recognition check must keep the vehicle/DLC cable disconnected.
3. Record Windows Device Manager USB IDs, provider/version and problem code.
   USB recognition is not validated J2534 compatibility.
4. Run Stage A below. Keep the timestamped JSON **and adjacent readable `.txt`**.
   Review missing DLLs, PE architecture and driver versions. Use the x86 kit for
   an x86 DLL; the GUI remains x64. **Stop before Stage B pending explicit approval.**
5. Clone: preserve supplied software, inspect hashes/signatures/metadata, and do
   not launch its EcuFlash or execute its driver. USB identification, if separately
   approved after reviewing automatic driver binding, is observational only.
   Passive diagnostics can run with the clone absent. Defer execution to Task 007.

No firmware update is recommended. No physical operation is authorized by CI
success or by this checklist; the sequence describes preparation for later
approved actions.

## Before installing software

1. Keep the OBDX Pro VX and OpenPort clone physically separate; inspect the
   packaging/source and privately record labels/photos if useful.
2. Preserve original downloaded/supplied packages. Do not launch clone CDs,
   EcuFlash executables, updater tools or unknown installers.
3. Download the toolkit from the reviewed FastECU development Actions run.
   Verify its published artifact SHA-256 and extract the entire directory.
   The diagnostic needs its accompanying runtimes, not the source checkout or a
   manually installed Qt SDK. These development executables are unsigned;
   provenance/hash checks do not establish hardware compatibility. Stop if your
   security software rejects a file; do not disable protections to continue.
4. Inspect each installer/archive/DLL without executing it:

   ```powershell
   Get-Item 'C:\Intake\supplied-package.exe' | Select-Object Name,Length
   Get-FileHash 'C:\Intake\supplied-package.exe' -Algorithm SHA256
   Get-AuthenticodeSignature 'C:\Intake\supplied-package.exe' |
       Select-Object Status,StatusMessage
   .\diagnostics\x64\fastecu-j2534-diagnostics.exe --inspect-file 'C:\Intake\supplied-package.exe' --output .\installer-review.json
   ```

   The native tool inspects local files only. Unknown/archive/non-PE format is
   explicitly reported; an installer EXE's architecture is **not** proof of the
   architecture of DLLs it might install. Files larger than 512 MiB are not
   hashed by the tool; PowerShell's `Get-FileHash` can inspect those separately.

## Windows installation and USB recognition

### OBDX Pro VX

- Official references: <https://obdxpro.com/downloads/> and
  <https://obdxpro.com/setup/>. Use the actual **VX-specific** manufacturer USB
  and J2534 instructions/packages, not a generic driver or third-party mirror.
- Record download URL/date, package version/hash and signature. Do not bypass
  login/download restrictions. Current installer versions, supplied DLL bitness
  and automatic firmware behavior must be verified against those sources; this
  toolkit does not assume them from the product name.
- Install only the reviewed manufacturer packages according to their documented
  order. This task neither installs them nor authorizes a firmware update.
- Before an approved USB attachment, close FastECU, EcuFlash and vendor update
  utilities. Keep the DLC/vehicle cable disconnected. Inspect Device Manager:
  description, Hardware Ids (VID/PID), driver provider/version and problem code.
  USB recognition alone does not prove J2534 functionality.

### OpenPort 2.0 clone

- **File inspection precedes installation/execution.** Keep clone installers,
  driver DLLs and any supplied EcuFlash intact for separate Task 007 review.
- Genuine reference only: <https://www.tactrix.com/index.php?Itemid=61&id=38&option=com_content&view=category>.
  Availability of a genuine driver does not qualify it for clone hardware.
- Do not install modified EcuFlash merely to obtain a driver. Do not flash
  genuine firmware onto the clone or assume genuine USB IDs/firmware behavior.
- Until the supplied software and driver/update behavior are reviewed, stop at
  passive file inspection. USB attachment to a PC with an existing vendor driver
  can bind software automatically; review that environment before attachment.
  No clone device-open operation is enabled by this toolkit.

## Passive FastECU diagnostic reports

For an approved Windows installation/USB-recognition check, run Stage A:

```powershell
.\collect-adapter-diagnostics.ps1 -Adapter obdx-pro-vx
.\collect-adapter-diagnostics.ps1 -Adapter openport-clone
```

The second command is still passive and can be run with the clone absent. It
labels a report; it cannot prove physical adapter identity or authorize loading
clone software. If an unknown USB description is not listed, explicitly include
present USB metadata (serial/instance suffixes remain redacted):

```powershell
.\collect-adapter-diagnostics.ps1 -Adapter openport-clone -AllUsb
```

- No installed J2534 driver is a valid discovery result, not a compatibility pass.
- Compare both registry views, manufacturer/name, DLL filename, file SHA-256,
  PE machine and issue fields. Registry view is not DLL-architecture proof.
- An x64 diagnostic cannot load x86 DLLs. Use `-Architecture x86` for the separate
  x86 toolkit if supplied. The x64 FastECU GUI still cannot use an x86 DLL;
  this tool is **not** a cross-bitness flashing bridge.
- USB kernel drivers follow Windows OS architecture; J2534 user-mode DLLs must
  match the diagnostic process architecture. They are different layers.
- Read the adjacent `.txt` summary for a quick triage; JSON retains full statuses.
- Save the initial JSON, review it locally and share the redacted JSON for
  review. Do not share raw vendor software/private worker files indiscriminately.
  `--include-private-details` is an explicit disclosure decision.

## SH7058 identification and BIN preparation

**SH7058 preflight → BIN/XML compatibility (offline)** never opens an adapter.
It preserves all bytes and reports hardware-family evidence, target integrity,
firmware transition and flash-operation readiness independently. A changed
identifier or target SHA is not an automatic hardware rejection. Missing full
installed identity does not invalidate an independently checked target. Without
a qualified current-image comparison, required sectors remain unknown.
**Read ECU identity (AA/CAL-ID)** is a separate ACTIVE ECU operation using only
the firmware-reviewed AA/09-04 requests. It requires reviewed official OBDX
hash/API, prior USB-only Stage B validation and separate physical-test approval.
Do not run it during Stage A/B or use legacy ROM Read for identification.
The returned short `ZE5T201V` CAL-ID does not prove the full custom `DM24000`
variant. The Alpha parent chain is now located; full-variant live evidence stays incomplete. ROM Read,
upload/write/test-write and the live candidate remain blocked. See
[the identification/preflight procedure](sh7058-identification-preflight.md).
No ECU interaction is authorized by an offline result.

## Stage B requires separate review and approval

Do **not** proceed merely because a DLL is present, signed, or opens in a mock
test. Review the exact manufacturer's DLL package and its load/open/firmware
behavior first. Loading a DLL executes vendor code. No firmware update is
requested by the toolkit, but the vendor implementation may have its own effects.

Only after that review and a separately authorized USB-only plan may the OBDX
operation in the command reference be used, with the vehicle cable disconnected.
Clone/OpenPort calls stay blocked pending Task 007. Channel, CAN, VIN/ECU ID,
security, ROM and flash operations are absent from the diagnostic interface.

## Stop and review

- Unknown/unverified installer, unexpected firmware-update requirement or an
  unreviewed existing driver about to bind to the clone.
- Missing DLL, malformed/unsupported PE, incompatible process/DLL architecture,
  changed reviewed hash, unexpected API version or inconsistent statuses.
- Windows fails to recognize the device, Device Manager reports a problem,
  opening/version/closing fails, cleanup is unconfirmed, or USB resets/disconnects.
- A device worker times out: do not start another operation or unplug during an
  uncertain vendor call. The toolkit does not forcibly terminate it. Collect a
  late result only for review; it does not erase the timeout uncertainty.
- Any unexpected vehicle communication or a tool offering programming actions.

Do not work around a stop by disabling safeguards, resetting unknown states or
rewriting firmware. No ECU flash is part of day-one testing.

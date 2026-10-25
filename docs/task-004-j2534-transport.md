# Task 004 — Generic J2534 transport and Windows discovery

## Inspection before implementation

Checkout: `https://github.com/ChuckyT83/FastECU.git`, clean at
`d4e44cdb4396ba070f563b2dd7ab4c11000294f3` on
`fix/j2534-initialization-reliability`. Task 003 and 003B source/tests/workflow are
present; tested commit `f1a6abd878a0ed78a83dd2decabdb78cae4970ce` is an ancestor.
Task 004 branches from that development history as
`fix/j2534-transport-reliability`, not from master.

Observed defects before changing production code:

- Direct `write_j2534_data` ignores WriteMsgs status and output count, discards
  bytes after failed/zero transfers, and unconditionally reports success. It
  splits buffers at 4128 bytes, including CAN/ISO15765, without validating minima.
  Public QByteArray write/echo methods discard the integer result and return a
  null-pointer conversion of STATUS_SUCCESS (empty bytes), indistinguishable from
  failure. Existing higher-level callers often ignore their return values.
- Direct `read_j2534_data` treats every nonzero status as empty, does not validate
  message count/length/ExtraDataIndex, and performs up to two follow-up reads with
  stale count/status and a new full timeout each. TX_DONE/START_OF_MESSAGE may
  therefore expose uninitialized/stale bytes or exceed the caller's timeout.
- Periodic start/stop ignore errors; repeated/chunked starts overwrite the only
  remembered message ID, so earlier schedules cannot be stopped. Zero/negative
  intervals and invalid lengths are not checked.
- Set-config, battery, programming-voltage and init helpers lose status or ignore
  failure. Five-baud uses a shared 20-byte input/output buffer, copies unbounded
  input while declaring one byte, declares zero output capacity and trusts the
  returned output pointer/count. Fast-init copies unbounded payloads. Generic
  serial receive parsing reads header bytes before checking availability.
- Windows discovery selects Registry32Format even on x64 because _WIN32 is also
  defined there. Missing/empty/non-string registry values are admitted. DLL
  selection prefers a same-named CWD library over the registered path. Load errors
  retain Windows status but give little bitness context. Win32 wrapper write debug
  dumping trusts a driver-reported count and formats unbounded data.

Entry points reviewed: direct/public read_serial_data and read_serial_obd_data,
write_serial_data and write_serial_data_echo_check, private write/read_j2534_data,
periodic start/stop, clear RX/TX, set_j2534_ioctl, read_vbatt, five_baud_init,
fast_init, set/pulse/line-end voltage helpers, getAllJ2534DriversNames,
open_serial_port, and the Windows J2534 forwarding/debug wrapper. The facade
`SerialPortActions` passes through the QByteArray/int results; remote replica
interfaces cannot convey a richer result without a separate interface change.

Implementation and validated results are recorded below. No physical hardware
or ECU programming operations are permitted in this task.

## Implementation decisions

- Submit one checked J2534 message per write, preserve its bytes/flags and the
  existing 100 ms API timeout, validate returned count/status and do not retry an
  uncertain write. Empty, address-short and oversized messages fail before API
  submission. The former arbitrary >4128-byte splitting is removed rather than
  inventing protocol fragmentation; callers requiring larger transfers need a
  reviewed message-boundary design, not implicit extra frames.
- Reads reset the message/count on each call, validate capacity/count/extra-data
  bounds and distinguish Success, NoMessage, Partial and Error with raw status on
  the direct transport. A valid completed message reported alongside timeout/empty
  status is returned with Partial outcome. Other driver errors discard data. API
  notifications are skipped, at most three reads share one monotonic deadline.
  ExtraDataIndex is validated but data is not stripped or reframed.
- The legacy write QByteArray remains empty on success; failure now returns one
  binary status byte 0x01. The facade already passes this through. Existing ECU
  callers ignoring results are not changed. Empty read bytes alone remain
  insufficient to distinguish timeout/failure across the remote interface; a
  future explicit result/remote-signal contract requires review. No error bytes
  are inserted into received ECU payload.
- Periodic scheduling tracks one owned ID, including zero. Invalid lengths and
  nonpositive intervals fail, a second active start is rejected, stop failure
  retains ownership for retry, and disconnect clears it. No interval is changed.
- Runtime invalid-channel/device errors unset local readiness and prevent further
  channel operations. Error diagnostics preserve driver status. A generic driver
  error does not automatically replay a write or destroy a still-valid channel.
- Five-baud uses one address byte and separate capacity-declared output storage;
  returned count/pointer are checked. Fast-init rejects oversized buffers before
  copying or delay. Incomplete serial headers are checked before indexing.
- Voltage setters and pulse helpers propagate failure while retaining existing
  pins, levels, enable/delay/disable order and disable attempts. There is no new
  physical voltage operation, retry or timing strategy. Battery's unsigned return
  and generic-serial echo result limitations remain documented for interface
  review rather than changing ECU programming routines.
- Windows discovery selects the process-matching registry view, ignores malformed
  FunctionLibrary values, expands environment references, and preserves absolute
  existing registered paths. It no longer substitutes a same-named CWD DLL.
  Loader errors include Windows status and process bitness. Native tests use
  isolated HKCU test keys; they never enumerate installed vendor devices.
- Windows debug dumping is bounded and validates returned write counts before
  indexing. Export names and PT_CALL remain unchanged. LLP64 is asserted in the
  native loader test; an OS-supplied 32-bit DLL is an offline incompatible-image
  fixture for the 64-bit loader, not a substitute for testing vendor DLLs/x86 ABI.

The expanded native workflow passed; see the closing validation entry.

## Validation and reproduction

The expanded workflow remains pinned to reviewed action SHAs with only
`contents: read`. It now triggers on J2534-related pushes to
`fix/j2534-transport-reliability` as well as the Task 003 branch. It builds complete
and incomplete x64 mock DLLs, the production Windows loader/initialization tests,
and a separate native production-code transport test target. Every native command
exit status is checked; compiler/version, build, text and JUnit logs are retained
as artifacts for 14 days, including on failure. The successful summary is also
published as a public job annotation. No vendor driver installation, registry
vendor scan, adapter connection, web service or ECU is required by the tests.

Linux reproduction with the prepared Qt toolchain:

```sh
. /workspace/fastecu-env/activate.sh
mkdir -p /workspace/build-FastECU-transport
cd /workspace/build-FastECU-transport
qmake /workspace/FastECU/tests/j2534/transport.pro QMAKE_LIBDIR+=/workspace/fastecu-env/lib
make -j4
QT_QPA_PLATFORM=offscreen ./j2534-transport
```

Linux links the actual direct transport code to the existing offline backend mock.
Windows builds the same transport test source against the actual Windows DLL
wrapper and mock DLL. Shared `mock_transport.inc` controls status/count/length,
notification sequences, payload contents, output-pointer corruption, read delay,
periodic errors and voltage-call capture. It implements only the fake backend;
the logic under test is production code. Native-only cases exercise the actual
registry API through a unique, cleaned-up HKCU test key and actual bitness rejection
using Windows' 32-bit SysWOW64 DLL. No installed adapter keys are enumerated.

Windows reproduction: use an x64 MSVC developer shell, Qt 5.15.2 MSVC x64 in PATH
and PowerShell 7; run `./tests/j2534/run-windows.ps1` from the checkout. Logs are
written to `build-j2534/logs/`. The tested runner uses Windows Server 2022,
MSVC v143 toolset **14.44.35207**, qmake 3.1 and Qt **5.15.2** MSVC 2019 x64
binaries, which are ABI-compatible with MSVC 2022. Exact executable compiler
component versions are retained in the `/Bv` log.

Linux current results:

| Check | Result |
| --- | --- |
| Full native Linux FastECU application build, Qt 5.15.8, make -j4 | Passed |
| Production initialization regressions | 40 passed, 0 failed, 0 skipped |
| Production transport regressions | 38 passed, 0 failed, 0 skipped |
| Transport under ASan/UBSan, Xvfb/X11, leak detection enabled | 38 passed, exit 0, no sanitizer report |
| Windows-loader logic via Linux bridge | 34 checks passed |
| actionlint 1.7.7 and git diff --check | Passed |

QtTest counts include initialization/cleanup hooks (two per executable). Linux
has 36 functional transport rows; Windows adds two platform-specific registry/ABI
cases. Assertions include exact original transmit bytes/flags and 100 ms timeout,
full/failed/zero/invalid-count write results, boundary/oversized input, distinct
receive timeout/error/partial outcomes, notification-followup failure, bounded
retry count and shared deadline, message count/length/ExtraDataIndex bounds,
invalid-channel state, reset/retry, periodic ID zero and failed stop retry,
config/clear failure, malformed five-baud pointer/count, fast-init overflow,
voltage failure propagation and preserved OFF attempts. The latest Windows
five-baud cases explicitly enable wrapper debug logging so diagnostic paths are
included in malformed-response validation.

Earlier native runs (all passed, no assertion or build failures):

- [40153892410](https://github.com/ChuckyT83/FastECU/actions/runs/40153892410),
  initial implementation: 39 transport cases, 36 initialization cases, 34 loader
  checks. This preceded the final closed-write and voltage-off regression.
- [40154084589](https://github.com/ChuckyT83/FastECU/actions/runs/40154084589),
  reviewed closed-write/voltage behavior: 40 transport cases, 36 initialization
  cases, 34 loader checks, all with zero failures/skips. This preceded the final
  debug-diagnostic safety correction.

The final tested commit/run is recorded in the closing validation entry below.

## Files modified

- `serial_port/serial_port_actions_direct.cpp/.h`: bounded transport, observable
  outcomes/status, count/error propagation, periodic ownership, helper bounds,
  voltage error returns without pin/level/timing changes, and registry/path logic.
- `serial_port/J2534_win.cpp`: bounded debug reads/dumps, returned-count and null
  argument checks, and loader bitness diagnostic.
- `tests/j2534/mock_dll.cpp`, `mock_j2534.cpp`, new `mock_transport.inc`: common
  configurable offline driver behavior for native DLL and Linux link-time mocks.
- New `tests/j2534/transport.cpp` and `transport.pro`: production-code transport,
  registry and ABI regressions, including byte-preservation assertions.
- `tests/j2534/loader.cpp`: assert native Windows x64 LLP64 ABI.
- `tests/j2534/run-windows.ps1`: compile/run/log the additional target, record
  separate counts and the actual MSVC toolset.
- `.github/workflows/j2534-windows.yml`: include the isolated Task 004 branch.
- This report: findings before implementation, decisions, evidence and limits.

No ROM, XML, checksum, sector layout, programming kernel, ECU flashing/security
access or verification-algorithm files were changed. Existing framing helpers,
CAN IDs, baud rates, filter definitions, timing values, voltage pins and voltage
levels are preserved. Removing implicit oversized-message splitting and rejecting
invalid transfers are deliberate generic API-contract corrections, not new
protocol segmentation. No physical testing or main-branch merge was performed.

## Compatibility limits and remaining defects

1. Empty reads still require the direct outcome accessor or LOG_E diagnostics to
   distinguish driver error from normal timeout through legacy facades/QtRO.
   Public writes now report 0x01 on failure, but callers ignoring returned bytes
   still ignore failure. No ECU programming caller was rewritten. A reviewed
   status-aware local/remote result contract is recommended before hardware use.
2. Partial writes are never replayed. With one requested message the API can only
   report zero or one completed message, not a byte offset. Error status remains
   failure even if the driver reports one message. A caller must not infer ECU
   acknowledgement from driver queue acceptance or automatically resend data.
3. A vendor DLL can overwrite memory before returning; post-call bounds checks
   cannot sandbox native code. Driver hangs beyond requested timeout and
   concurrent/reentrant DLL behavior are also outside the mock guarantees.
4. x64 registry selection and real native incompatible-image rejection are
   demonstrated. Windows x86 execution, decorated stdcall exports, actual vendor
   installation layouts, permissions/access-denial cases and 32-bit driver
   operation are not validated. A 64-bit app deliberately does not enumerate the
   opposite registry view. Driver export spelling/calling convention is unchanged.
5. CWD DLL override removal means intentional portable DLL overrides must instead
   be registered/explicitly selected in a future reviewed UI. Relative/missing or
   non-string registry paths are rejected. ANSI loader/path conversion and its
   existing 255-byte limit remain; Unicode/long-path support needs separate work.
6. Raw UART write counts/echo failures and the old boolean timing setter contract
   are not generalized by this Windows J2534 task. Battery's unsigned result
   still uses the old error sentinel; no new voltage or shutdown strategy is
   invented. Resource release refused by a vendor can retain external state even
   when local readiness is cleared safely.
7. Existing unconditional ISO15765_FRAME_PAD on generic writes and legacy raw-CAN
   filter flags remain unchanged. Altering those affects protocol framing/filter
   behavior and requires review rather than an unverified compatibility fix.
8. Full Windows application packaging/linking and hardware response semantics are
   not tested by these small native targets. Logs retain compiler warnings; no
   warning-free certification is claimed. GitHub reports Node 20 deprecation and
   forces pinned actions onto Node 24; the actual setup/build/tests still pass.

## Readiness and next steps

The evidence supports review of the generic offline transport/discovery changes,
not an OBDX Pro VX compatibility claim or permission for ECU programming. There is
no physical adapter available, and no hardware operation was attempted.

Recommended next task: review the local/remote failure-result contract and add
x86/registry-permission coverage before designing hardware tests. In particular,
review oversized-message callers, legacy protocol flags and error-ignoring ECU
callers before using this branch in a programming workflow. These items are
explicitly deferred for review rather than modified speculatively here.

When hardware becomes available, propose a separate approved adapter-only plan
without an ECU attached. Vehicle/ECU communication, security access, ROM reads,
test writes, flashing and programming kernels remain outside this task. Changes
remain on `fix/j2534-transport-reliability`; no PR or merge is authorized/performed.


## Closing validation entry

Final executable-code commit: `4335f54708e50be9d958b27a4e5b99be1f698049`.
The following report-only commit changes no executable code or test configuration;
its final repository HEAD is identified in the delivery summary.

**Passing final run:**
<https://github.com/ChuckyT83/FastECU/actions/runs/40154233251>

**Passing job:**
<https://github.com/ChuckyT83/FastECU/actions/runs/40154233251/job/121052036392>

Observed native evidence on that exact code commit:

```text
QMake version 3.1
Qt version 5.15.2 (msvc2019_64)
MSVC toolset: 14.44.35207
Loader regression checks passed: 34
Initialization: 36 passed, 0 failed, 0 skipped, 0 blacklisted
Transport: 40 passed, 0 failed, 0 skipped, 0 blacklisted
```

The final run completed successfully in 1m 43s and retained artifact
`j2534-windows-40154233251-1`. Native transport's 40 count includes two QtTest
lifecycle hooks, 36 shared functional/data rows and two Windows-only cases.
Initialization's 36 count also includes two lifecycle hooks. Loader checks are
separate assertions, not QtTest rows. Debug-enabled malformed five-baud responses,
real 32-bit image rejection by the x64 loader, synthetic registry entries and
all original Task 003B initialization tests executed without skips.

No native build or assertion failure occurred in Task 004. Subsequent runs were
justified by final review findings: closed writes still returning legacy success,
explicit OFF-attempt preservation after voltage error, and unsafe pre-validation
debug dereferences. Those were corrected and the actual final code rerun. Normal
Qt offscreen font-cache messages in the Linux non-sanitized runs did not fail
checks; ASan/UBSan used Xvfb/X11 with leak detection enabled and exited zero.

`git diff --check` and actionlint passed. The Linux full application build and
unit/sanitizer results above correspond to the final shared Linux code; the last
change added Windows-only wrapper checks/debug safety and its conditional test.
The original Task 003B branch and master were not changed. All implementation,
tests and this report are isolated on `fix/j2534-transport-reliability` and pushed
to the fork. No PR, merge, ECU or physical adapter access was performed.

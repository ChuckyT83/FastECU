# Task 006B — Subaru SH7058 communication reliability

## Disposition

**Partial completion. Kernel transfer remains disabled pending protocol review.**
The implemented local J2534 communication, parser, ROM-read and failure-propagation
changes have offline regression coverage. A functioning per-block kernel-transfer
acknowledgement contract has **not** been established or implemented. The user
explicitly confirmed: **“No reference available; fail closed pending review.”**
Do not interpret the kernel gate or mock-test success as working kernel upload,
OBDX Pro VX compatibility, ROM compatibility, recovery capability or permission
to program an ECU.

## Repository and baseline

- Repository/remote: `ChuckyT83/FastECU`, `https://github.com/ChuckyT83/FastECU.git`.
- Verified source branch: `fix/subaru-sh7058-critical-safety`.
- Clean source HEAD: `104d44844c1ff97d82b85970a44b44cda1c19720`.
- Prior native-tested executable code: `e309bd959f425bcca738de0a841c034deb982257`.
- New branch: `fix/subaru-sh7058-communication-reliability`, created directly from
  the expected Task 006A HEAD, without merging or branching from master.
- Implementation revision: `479114722bfe751cce64b7f6df5638ec6feb7f33`.
  The final documentation-only descendant is recorded in the delivered message
  and external final report; tested production code is identified separately.
- Tasks 003/003B/004, all Task 006A safeguards and its production-code test target
  were present. Task 005's audit and Task 006A's report were reviewed.
- No physical ECU, adapter, vendor driver, owner's ROM/XML or real kernel upload
  was used. No merge or pull request was created.

## Existing communication contracts and call chain

```text
MainWindow::start_ecu_operations
  -> SH7058 module run / phase functions
  -> SerialPortActions facade
     -> local SerialPortActionsDirect -> J2534 wrapper -> DLL
     -> legacy QtRO replica / slot_sync (now excluded for SH7058)
```

Before this change:

- `write_serial_data` and `write_serial_data_echo_check` returned empty bytes for
  submission success and binary `0x01` on failure. Many SH7058 calls ignored this
  result or overwrote it with the following read. ECU payload is not this status.
- `read_serial_data` returned bytes or empty bytes. Direct J2534 state already
  distinguished Success / NoMessage / Partial / Error and a status, but the
  facade did not carry that information to the module.
- `read_serial_obd_data` returned only data locally; its remote facade route had
  no implementation. The SH7058 CAN module does not use that entry point.
- QtRO `slot_sync` waited for a pending call and converted a `QVariant` without
  checking the pending-call failure or wait result. Invalid/default byte-array
  replies could therefore appear to be successful empty write results.
- The operation template called `void run()`, and the main-window action returned
  its generic zero value even when the module's UI reported a failure.

The legacy byte-array methods, `.rep` wire contract and `qtrohelper.hpp` remain
unchanged for unrelated modules. Their ambiguous semantics are not globally
reinterpreted or claimed fixed.

## Additive explicit result contract

`serial_port/communication_result.h` defines `CommunicationResult`:

| Field/state | Meaning |
|---|---|
| `Success` on write | Submission succeeded; **not** ECU acceptance/execution |
| `Success` on read | Driver returned data; module still validates the response |
| `Timeout` | No payload from the bounded receive operation |
| `TransportError` | Driver/submission failure, including zero-message writes |
| `Disconnected` | Invalid/disconnected device/channel; no usable connection |
| `Malformed` | Partial transport response or invalid/incomplete ECU frame |
| `Cancelled` | Host cancellation observed; no subsequent submission allowed |
| `Unsupported` | Remote/non-J2534 checked transport or unreviewed protocol |
| `Rejected` | Correlated diagnostic negative response, distinct from timeout |
| `bytes` | Received data, separate from all status values |
| `status`, `hasStatus` | Actual J2534 return when an API call occurred |
| `detail` | Diagnostic reason, including the local contract-error status |

The direct backend now captures the actual API return separately from synthesized
contract errors. For example, a write returning J2534 status zero with message
count zero produces `TransportError`, **raw status zero**, `hasStatus=true`;
it never becomes success. Locally rejected calls have no fabricated raw status.
Partial reads fail closed at this module boundary. Received byte `0x01` is never
interpreted as a driver error sentinel.

The facade adds `write_checked` / `read_checked`; the direct backend adds
`write_j2534_checked` / `read_j2534_checked`. Busy state is released on a failed
write or completed read. Outcomes are captured before optional adapter battery
metadata polling, so polling cannot overwrite the result being returned.
Existing J2534 read/write timeouts, notification limits and frame flags remain
unchanged.

### Remote and non-J2534 limits

SH7058 remote operations are **explicitly unsupported**, regardless of replica
state. The main window rejects them before its legacy prelude makes RPC calls;
the module independently rejects remote entry; the checked facade methods also
return `Unsupported` without calling `slot_sync`. No invalid remote reply can be
converted into a successful empty submission through this API.

Tests exercise the real facade with invalid QtRO replicas and nonexistent local
IPC sources, plus module-level rejection scenarios. They prove **denial before
RPC**, not successful remote communication or pending-call timeout recovery.
A remote-call success/failure/timeout transport implementation remains deferred.
This uses the task's permitted fail-closed alternative without changing the
shared replica schema or unrelated modules.

Local non-J2534 serial transport is also unsupported by the new checked SH7058
API: its legacy byte-array path cannot provide this result contract reliably.
That compatibility restriction is explicit. General serial-transport support
would require separately reviewed result semantics; other modules retain their
existing serial methods.

## Submission failure propagation and operation outcomes

All remaining SH7058 writes go through `submit()`. It checks cancellation, stores
the outstanding request, consumes the explicit facade result and aborts on any
failed/unsupported submission **before reading a response**. A stale positive
reply cannot repair a failed submission. No uncertain write is automatically
replayed.

This covers kernel-ID probes, ECU/VIN/CAL/CVN requests, diagnostic sessions,
security seed/key requests, RAM-word helper requests, ROM reads, CRC comparison,
flash capability/enable queries, voltage query, erase, every buffer write and
validate/commit. Kernel-download/transfer/exit/start requests are not sent at all
while the upload gate is in place.

Required read outcomes also abort their phase. Existing optional informational
queries may continue on a normal timeout or a correlated negative response;
driver errors, malformed frames and unrelated positives abort. NRC `0x78`
(response pending) is unsupported/incomplete, not a definitive optional
rejection: it aborts immediately without inventing a wait or retry strategy. The initial
kernel-ID probe may time out and follow the existing bootloader path. Session
probes must establish exactly one supported branch before security access. Both
or neither accepted previously produced an ambiguous/invalid request; the host
now stops rather than guessing a session or appending two subfunctions.

`read_ram_location` now returns success separately from its numeric output.
A failure can no longer masquerade as RAM value `1` and feed the existing key
algorithm. The algorithms themselves were not changed.

The module retains its compatible `void run()` entry and adds an
`operation_result()` accessor: 0 success, 1 failure, 2 verification mismatch,
3 cancellation. MainWindow consumes it for this module, returns that outcome to
its caller and permits read auto-save only after success. Other ECU modules
retain their prior return behavior. The success dialog and Task 006A CRC mismatch
behavior remain covered by executed tests.

## Response validation

`receive_response` handles transport/cancellation first, then calls the shared
production `validate_response` **before any phase parser indexes fields**.

| Response family | Checked fields and bounds |
|---|---|
| Common ISO15765 envelope | At least an address/service; fixed configured response address `0x7E8` |
| ECU ID `EA` | Expected service and at least 13 bytes before extracting bytes 8–12 |
| VIN/CAL/CVN `49` | Requested PID echoed; header/data available; VIN 17 bytes, CAL nonempty variable string, CVN whole four-byte words |
| Session `50` | Requested subfunction echoed; minimum six bytes; unused timing extension retained |
| Security `67` | Requested subfunction; four-byte seed available or key acknowledgement; ambiguous extra bytes rejected |
| RAM `E8` | Exact four-byte word available before unsigned decoding |
| Kernel envelope | `BEEF`, expected command with positive-response bit, declared length equals actual service+payload length |
| Kernel ID | Nonempty payload; treated as opaque identification, not authenticated kernel identity |
| Capabilities / CRC | Exactly four payload bytes before decoding |
| Programming voltage | Exactly two payload bytes; original conversion/settings preserved |
| ROM read | Exactly the requested 1,024-byte page payload |
| Erase/buffer/validate/commit | Correlated command and complete acknowledgement envelope; unexplained extra payload rejected |
| Diagnostic negative | Complete `7F`, correlated request service and NRC; never a positive acknowledgement |

Variable-length informational responses retain legitimate trailing data. Fixed
responses reject incomplete or unexplained extra payload. No undocumented field
is invented to accept ambiguous transfer acknowledgements. Capability numbers
remain informational as before: their units/limits are not inferred to change
buffer sizes, sector algorithms or timing.

The current kernel responses do not echo every request's address or sequence.
A well-formed stale reply to an **identical** outstanding command cannot always
be distinguished using the available fields. Failed submissions are never
followed by an acknowledgement read, and unrelated commands are rejected, but
this does not add a correlation mechanism absent from the protocol.

## Kernel preparation and the unresolved transfer contract

The audited loop ignored every block reply and used `<= maxblocks`, creating an
extra address-only transfer after the intended payload. No authoritative
ECU-side source or `0xB6` acknowledgement specification was available; related
repository upload code also ignored these replies. Guessing `F6`, address echoes
or another payload rule could falsely accept an unrelated response or reject
valid hardware behavior. The user approved failing closed pending review.

`prepare_kernel` now validates:

- At least four bytes and padded length within the existing SH7058 RAM region.
- Destination alignment and 64-bit end calculation within `kblocks_SH7058`:
  `0xFFFF3000` through `0xFFFFBFFF` (36 KiB).
- Existing 128-byte padding, original checksum-word preparation and encryption.
- No empty prepared output or out-of-bounds indexing.

Unsigned byte decoding removes undefined signed-char shifts while retaining the
same word values. Keys, lookup tables, encryption rounds, padding and checksum
arithmetic are unchanged. Five prepared payload hashes match the **actual
Task 006A uploader's** nonempty payloads on synthetic files.

`upload_kernel` validates the file/read and preparation, then returns
`Unsupported` **before sending download-init, transfer, exit or start commands**.
The unsafe loop is removed, so it cannot submit the extra empty transfer. This
is a safety gate, **not a working replacement uploader**.

**Incomplete:** successful per-block acknowledgements, failed/missing/negative
block acknowledgements, advancing an acknowledged transfer address/progress,
transfer exit/start correlation and cancellation during an active transfer have
no implemented/validated execution path. These are not counted as passing tests
or hidden as skipped cases. The executable tests cover preparation/bounds and
the no-command gate only.

The gate is inside `upload_kernel`. The surrounding legacy bootloader connection
can already have requested diagnostic/security sessions before reaching it.
Nothing in this task authorizes running that path on hardware, and the gate must
not be interpreted as making the whole application noncommunicating.

## ROM-read completeness and cancellation

`run` validates the selected memory model before communication for reads as
well as writes. `read_mem` accepts only a validated SH7058 full-memory read: start zero and exact
selected model size. It preserves the existing page size and addresses, validates
each page, and requires an accumulated 1,048,576 bytes. Failed, short, oversized,
missing, malformed or cancelled reads leave the original working image intact.
Final progress/event processing happens before publishing the complete image;
a cancellation at that boundary cannot publish a cancelled result. The main
window additionally requires a successful SH7058 outcome before auto-save.

Task 006A cancellation checks remain at destructive-command boundaries. The new
submission/receive helpers also detect cancellation between communication
phases and propagate code 3. An acknowledged cancellation prevents further host
submissions. A request already submitted may still execute even if the host sees
a write error, missing acknowledgement or later cancellation. The host reports
failure/cancellation; it cannot claim the ECU command was undone. No recovery
sequence or uncertain-command retry was introduced.

## Source references and corresponding tests

All line references below refer to the implementation revision above. Source
file paths are relative to the repository.

| Correction | Production source/function | Corresponding tests |
|---|---|---|
| Explicit result / raw API return | `serial_port/communication_result.h`; `serial_port_actions_direct.cpp`, checked methods/result mapper near lines 1705–1745 and raw capture at WriteMsgs/ReadMsgs | `J2534TransportTest::checkedWrite`, `checkedRead`, `checkedUnsupported` |
| Real facade / RPC denial | `serial_port/serial_port_actions.cpp`, `write_checked` 1182–1192, `read_checked` 1194–1210 | `SubaruFacadeTest::localUnsupported`, `localDisconnected`, `remoteDenied`; `SubaruSafetyTest::remoteRejected` |
| Submit/receive gate | SH7058 module `submit` 47–60, `communication_status` / `communication_failed` 62–71, `receive_response` (pinned below) | `writeFailurePhases`, `diagnosticFailure`, `readOutcome`, `malformedStopsDestruction` |
| Safe parser | SH7058 `validate_response` (pinned below), used by every receive | `parserFrames` (313 rows), `malformedDiagnosticStops` |
| Diagnostics/security transition | SH7058 `connect_bootloader` / `read_ram_location` | `diagnosticNormal`, `diagnosticFailure`, `sessionAmbiguity`, `optionalMetadata`, parser rows |
| Kernel preparation / gate | SH7058 `prepare_kernel` / `upload_kernel` | `kernelPreparation` (12 rows), `kernelUploadGated`; separate Task 006A baseline experiment |
| Complete reads | SH7058 `read_mem` | `completeRead` (8 rows) |
| Destructive phase failures | SH7058 `check_romcrc`, `init_flash_write`, `reflash_block`, `flash_block` | `writeFailurePhases` (18 rows), `readOutcome`, `malformedStopsDestruction`, inherited `cancellation` (12 rows) |
| Caller/UI/save propagation | SH7058 `run`, `operation_result`; `mainwindow.cpp::start_ecu_operations` | `uiResult`, `failedWriteUi`; MainWindow compiled in full Linux target |
| Successful wire sequence | SH7058 existing phase builders; no constant/map/kernel changes | `normalSequence` (commit/validate), `completeRead(none)`, `diagnosticNormal` |

The full MainWindow GUI/file workflow is not linked into the unit executable.
Its result gate and early remote rejection were reviewed and compiled in the
full Linux build. Module outcomes and UI verdicts execute directly in the unit
suite; the real facade/backend boundary has separate executed tests.


### Pinned implementation index

- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::submit` (47–60)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L47-L60).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::validate_response` (73–125)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L73-L125).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::receive_response` (127–148)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L127-L148).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::run` (156–294)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L156-L294).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::connect_bootloader` (311–739)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L311-L739).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::read_ram_location` (741–795)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L741-L795).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::prepare_kernel` (802–822)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L802-L822).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::upload_kernel` (824–844)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L824-L844).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::read_mem` (851–989)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L851-L989).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::write_mem` (996–1107)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L996-L1107).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::get_changed_blocks` (1114–1143)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L1114-L1143).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::check_romcrc` (1150–1229)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L1150-L1229).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::init_flash_write` (1268–1413)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L1268-L1413).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::reflash_block` (1420–1498)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L1420-L1498).
- [`modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp::flash_block` (1505–1724)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp#L1505-L1724).
- [`serial_port/serial_port_actions_direct.cpp::communication_result` (1705–1720)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/serial_port/serial_port_actions_direct.cpp#L1705-L1720).
- [`serial_port/serial_port_actions_direct.cpp::write_j2534_checked` (1722–1731)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/serial_port/serial_port_actions_direct.cpp#L1722-L1731).
- [`serial_port/serial_port_actions_direct.cpp::read_j2534_checked` (1733–1742)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/serial_port/serial_port_actions_direct.cpp#L1733-L1742).
- [`serial_port/serial_port_actions.cpp::write_checked` (1182–1192)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/serial_port/serial_port_actions.cpp#L1182-L1192).
- [`serial_port/serial_port_actions.cpp::read_checked` (1194–1208)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/serial_port/serial_port_actions.cpp#L1194-L1208).
- [`mainwindow.cpp::start_ecu_operations` (991–1427)](https://github.com/ChuckyT83/FastECU/blob/479114722bfe751cce64b7f6df5638ec6feb7f33/mainwindow.cpp#L991-L1427).

## Executed regression coverage

All **64** Task 006A result names are present and passing in the expanded suite.
The final Subaru suite reports **453 passed**, including two lifecycle results:
451 executable test rows/cases, 389 added results beyond Task 006A.

Important new groups:

- 18 failed submissions across comparison, capabilities, enable, voltage, erase,
  buffer writes, commit and post-write comparison. A stale acknowledgement is
  left available; the test asserts no read or subsequent command after failure.
- 10 diagnostic write failures and nine malformed diagnostic executions.
- Four distinct read outcome failures, two ambiguous-session cases, two pending-response executions and two
  optional-metadata cases that preserve timeout/unsupported behavior.
- 313 table-driven response cases: empty/truncated envelopes, correct minimum
  responses, wrong address/service/subfunction/command, negative/unrelated
  responses, bad declared lengths and permitted/rejected trailing bytes.
- Eight full-read scenarios: complete image, missing/short/header-only/oversized
  page, driver failure, mid-read cancellation and final-progress cancellation.
- Two invalid read-model entry cases assert zero setup/submissions.
- Failed-critical-write UI reporting and explicit operation-result assertions.
- Real-facade unsupported serial/disconnected J2534 checks and remote denial.
- Kernel file preparation boundaries and the explicit no-command upload gate.

Remote scenario names do not imply a pending RPC was issued or timed out. No
per-block kernel-acknowledgement success is claimed. There were no final QtTest
skips; missing protocol-dependent coverage is listed explicitly above.

## Linux builds and results

Toolchain: Debian 13, GCC 14.2.0, Qt 5.15.8, qmake 3.1. The relocated Qt SDK is
activated with `/workspace/fastecu-env/activate.sh` and the build additionally
uses `QMAKE_LIBDIR+=/workspace/fastecu-env/lib`.

| Executed target | Actual result | Evidence under `/workspace/fastecu-env/` |
|---|---|---|
| Complete FastECU application compile/link | Passed, exit 0 | `task006b-app-build.log` |
| J2534 initialization | 40 passed, 0 failed/skipped | `task006b-initialization-results.txt` |
| J2534 transport, including checked API | 51 passed, 0 failed/skipped | `task006b-transport-results.txt` |
| Win32-loader Linux bridge | 34 assertions passed | `task006b-loader-results.txt` |
| Subaru safety/communication | 453 passed, 0 failed/skipped | `task006b-subaru-results.txt`, `.xml` |
| Real facade | 7 passed, 0 failed/skipped | `task006b-facade-results.txt`, `.xml` |
| Subaru ASan + UBSan | 453 passed, 0 failed/skipped; no sanitizer findings | `task006b-subaru-asan-results.txt`, `task006b-subaru-asan-console.log` |
| Workflow syntax | actionlint 1.7.7, exit 0 | Current workflow |
| Diff whitespace | `git -c core.whitespace=cr-at-eol diff --check`, exit 0 | Existing CRLF preserved |

Sanitizers used a separate debug build with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and matching linker flags,
`QT_QPA_PLATFORM=minimal`, leak detection enabled and no suppressions. All 313
malformed-parser rows executed in that build. Full-application memory safety is
not claimed. Existing unused-variable/parameter and Qt/GCC-header warnings remain;
no general warning cleanup was attempted.

During test development, placing slot definitions in an included fragment caused
a moc parse error. The cases were placed directly in the test class and rebuilt;
no new test execution was claimed for the failed build. The separate baseline
experiment initially lacked an include path, which was corrected before its
reported successful execution. These were build-infrastructure failures, not
suppressed test failures.

### Reproduction

With Qt 5.15 and a C++11-compatible compiler in PATH, build in separate directories:

```sh
# Run from the repository root; choose absolute source/build paths as needed.
repo_dir="$PWD"
mkdir -p /tmp/fastecu-subaru /tmp/fastecu-facade
cd /tmp/fastecu-subaru
qmake "$repo_dir/tests/subaru/safety.pro" CONFIG+=release
make -j4
QT_QPA_PLATFORM=minimal ./subaru-safety -o results.txt,txt -o results.xml,junitxml
cd /tmp/fastecu-facade
qmake "$repo_dir/tests/subaru/facade.pro" CONFIG+=release
make -j4
QT_QPA_PLATFORM=minimal ./subaru-facade
```

Use `tests/j2534/reliability.pro`, `transport.pro` and `run-loader-linux.sh` for
the existing suites. The cloud's relocated SDK additionally needs the activation
script/library search path noted above. For sanitizers, add the recorded compiler/
linker flags in a fresh debug build directory; do not reuse a release object build.
On Windows, run `tests/j2534/run-windows.ps1` from an x64 MSVC developer shell
with the recorded Qt SDK in PATH. It builds all native targets and records logs.

## Native Windows CI

The workflow retains `windows-2022`, native x64 MSVC, Qt 5.15.2 MSVC2019 x64,
pinned action SHAs, `contents: read`, nonzero failure propagation and always
uploaded logs. It triggers for `fix/subaru-sh7058-communication-reliability`.
The complete base SDK supplies Remote Objects/WebSockets for the real-facade
production-code target. GUI suites retain native `windows` Qt platform with
Fusion style; the old J2534 suites retain `offscreen`. The Task 006A message-box
backend crash was not reintroduced.

Failed attempts are retained in the record:

1. [40223591963](https://github.com/ChuckyT83/FastECU/actions/runs/40223591963),
   [job 121260195010](https://github.com/ChuckyT83/FastECU/actions/runs/40223591963/job/121260195010):
   Qt installer exited 1 before compilation; the initial artifact pattern had no
   build logs to collect.
2. [40223742066](https://github.com/ChuckyT83/FastECU/actions/runs/40223742066),
   [job 121260656602](https://github.com/ChuckyT83/FastECU/actions/runs/40223742066/job/121260656602):
   installer diagnostics identified `qtremoteobjects` as an unavailable separate
   package in this feed. Removed that add-on selection and installed the complete
   base SDK. Installer log retention/public diagnostics were added.
3. [40224076688](https://github.com/ChuckyT83/FastECU/actions/runs/40224076688),
   [job 121261640309](https://github.com/ChuckyT83/FastECU/actions/runs/40224076688/job/121261640309):
   passed at `51c4f4a`; 440 Subaru, seven facade, 53 transport, 36 initialization
   and 34 loader checks. This preceded the final main-window outcome propagation.

4. [40224714865](https://github.com/ChuckyT83/FastECU/actions/runs/40224714865),
   [job 121263534055](https://github.com/ChuckyT83/FastECU/actions/runs/40224714865/job/121263534055):
   passed at `8344288`; 440 Subaru, seven facade, 53 transport, 36 initialization
   and 34 loader checks. The final parser review then added explicit pending-response denial.

5. [40225456724](https://github.com/ChuckyT83/FastECU/actions/runs/40225456724),
   [job 121265749362](https://github.com/ChuckyT83/FastECU/actions/runs/40225456724/job/121265749362):
   passed at `acef8dc`; 451 Subaru, seven facade, 53 transport, 36 initialization
   and 34 loader checks. The following read-model entry guard added two cases.

Final validation:
[run 40225761755](https://github.com/ChuckyT83/FastECU/actions/runs/40225761755),
[job 121266657149](https://github.com/ChuckyT83/FastECU/actions/runs/40225761755/job/121266657149),
commit **`479114722bfe751cce64b7f6df5638ec6feb7f33`** — **succeeded**, 2m 28s.

| Native Windows target | Executed result |
|---|---|
| Loader/mock DLLs | Built; 34 assertions passed |
| Initialization | 36 passed, 0 failed, 0 skipped; 1,005ms |
| Transport | 53 passed, 0 failed, 0 skipped; 104ms |
| Subaru safety/communication | 453 passed, 0 failed, 0 skipped; 31,918ms |
| Real facade/QtRO denial | 7 passed, 0 failed, 0 skipped; 3ms |

Recorded toolchain: **qmake 3.1, Qt 5.15.2 MSVC2019 x64,
MSVC 2022 v143 toolset 14.44.35207**. The public job notice contains these exact
counts. Text/JUnit results, compiler/build logs and installer evidence are retained
in artifact `j2534-windows-40225761755-1` for 14 days. The existing pinned actions
emitted a Node 20 deprecation/forced-Node-24 warning; every step succeeded.
The two earlier installer failures are not counted as passing tests.


Full Windows application compilation is **not** claimed. Native targets compile
and execute the actual SH7058 module/UI/checksum, facade, direct transport,
QtRO replica implementation and Windows J2534 loader against offline substitutes.
MainWindow is covered by the complete Linux application build and review.

## Successful command/payload preservation

The inherited `normalSequence` assertions continue to match Task 006A/004 hashes
of complete length-prefixed submitted messages:

- Commit: `313959741b6a29c3182871eb3ee854976018c7c7cd8ad2231b9b0e3244788adf`.
- Validate: `a7f629163a60e21569202a7ebe6bd74a99353826ed6238b076abaca251a85703`.

These cover the scripted changed-sector success path. Tests also assert the
CAN prefix, exact buffer data, command order and read-page addresses/size.
`diagnosticNormal` exercises the ten-request successful bootloader path with
one accepted session probe. Header parsing, failed writes and malformed/missing
acknowledgements have independent tests; matching hashes alone are not used as
proof of error handling or hardware compatibility.

A separate build of the actual Task 006A uploader on synthetic `K`-filled files
confirmed one additional empty transfer in every tested size and captured its
nonempty encrypted payloads. The new preparation helper matches these hashes:

| Input bytes | Padded bytes | SHA-256 |
|---:|---:|---|
| 4 | 128 | `422b4f871c3ed2192020620437fb242078df0ceebf6b45d81fbf0a2c87e4882f` |
| 128 | 128 | `627d8c2cf046b7b2ca50d4635b9a2e40ce32cf90ea5ac80d64291c7a7a31d3f6` |
| 129 | 256 | `8c3d1e13522a7f4509a9778248ec44537f25fbc0cba8569649f0c7371bf3910f` |
| 6,428 | 6,528 | `26aef047094d69c844ae0a90db8db478821435a2bae9c8c0092777ef0cabcb1b` |
| 36,864 | 36,864 | `63a61a52a372f1579779f139beee42f80148dddd6e3efa4d03e68516bf578c1c` |

Baseline experiment: **7 passed, 0 failed/skipped**, including lifecycle,
`task006b-kernel-baseline-results.txt`. These are prepared-byte comparisons;
new working kernel transfers/address advancement are not claimed.

No CAN IDs, baud rates, framing, security algorithms, flash-sector definitions,
checksum arithmetic, kernel binaries, voltage levels, commit sizes or timeout/
retry strategy changed. The intentional behavior changes are fail-closed errors,
stricter response validation, explicit remote/non-J2534 denial, host outcome/save
gates and the kernel-upload stop pending review.

## Files modified

- `serial_port/communication_result.h` — new additive result type.
- `serial_port/serial_port_actions.{h,cpp}` — checked local API / remote denial.
- `serial_port/serial_port_actions_direct.{h,cpp}` — result mapping and raw API capture.
- `modules/ecu/flash_ecu_subaru_denso_sh7058_can.{h,cpp}` — gates, parsers, reads,
  kernel preparation/stop and operation outcomes.
- `mainwindow.cpp` — early remote rejection and SH7058 result/save propagation.
- `tests/subaru/safety.cpp`, `stubs/serial_port_actions.h` — production-module cases.
- `tests/subaru/facade.{cpp,pro}` — actual facade/QtRO denial tests.
- `tests/j2534/transport.cpp` — checked low-level contract cases.
- `tests/j2534/run-windows.ps1` — facade build/results and existing GUI backend.
- `.github/workflows/j2534-windows.yml` — branch/paths, complete SDK, setup evidence.
- `docs/task-006b-subaru-communication-reliability.md` — this report.

## Acceptance, remaining limitations and next task

| Criterion | Assessment |
|---|---|
| Failed submissions propagate; no stale ACK repairs failure | Implemented/tested through module and host UI outcome |
| Timeout and driver errors distinguishable | Implemented for checked local J2534; other transports explicitly unsupported |
| Malformed used response parsers reject unsafe indexing | Implemented/tested; unavailable transfer protocol is gated |
| Working per-block kernel ACK/transfer progression | **Incomplete — protocol review required** |
| No unintended empty kernel transfer | Enforced by removing unsafe execution and gating upload; functioning replacement deferred |
| Incomplete ROM cannot report success or auto-save | Implemented/tested at module; host save gate compiled/reviewed |
| Failed required communication cannot advance destruction | Implemented/tested on available kernel-operation paths |
| Task 006A regressions retained | All 64 result names pass |
| Linux/native Windows offline validation | See actual executed results above |
| No physical operations | Satisfied; every ECU exchange was fake/mock only |

Remaining limits:

1. Obtain authoritative ECU-side upload/acknowledgement evidence before enabling
   `0xB6`, transfer-exit and kernel-start paths. No speculative replacement exists.
2. Remote and local non-J2534 SH7058 operations require separately reviewed
   explicit contracts; global legacy QtRO helpers remain ambiguous for other modules.
3. Identical-command stale replies without correlation fields cannot reliably
   be rejected; opaque kernel IDs are not authenticated capability/identity proof.
4. Strict fixed-length/declared-length rules may expose undocumented real-kernel
   differences. Resolve those with authoritative evidence, not vendor workarounds.
5. No ROM/XML identity, Dime Mod inheritance, checksum equivalence, live ECU/ROM
   pairing, in-flight cancellation recovery or voltage-safety policy was established.
6. Full Windows application build and full MainWindow GUI integration tests remain
   separate validation work; native production-code unit coverage is not that build.

**Recommended next task:** obtain and review the existing bootloader/kernel
transfer contract; specify valid response shapes and correlation, then implement
and offline-test the gated upload phases without changing algorithms/addresses.
Keep remote/non-J2534 support and ROM/XML compatibility policy separately scoped.

**Hardware readiness: not ready for ECU programming.** The generic local error
foundation has improved and the implemented paths are suitable for focused code
review. The approved kernel gate means Task 006B is not fully functionally
complete. Mock results do not establish OBDX Pro VX or Subaru ECU compatibility.

# Task 006A — Critical Subaru SH7058 safety fixes

## Repository and baseline

- Repository/remote: `ChuckyT83/FastECU`, `https://github.com/ChuckyT83/FastECU.git`.
- Source branch: `fix/j2534-transport-reliability`.
- Verified clean source HEAD: `59c3a25e1664eeb95bee0a2945a23203af8c3743`.
- New branch: `fix/subaru-sh7058-critical-safety`, created directly from that SHA.
- Production code and regression-test revision: `e309bd959f425bcca738de0a841c034deb982257`.
- The final documentation revision is recorded in the delivered report/message;
  no production changes follow the tested revision unless explicitly noted below.
- Task 003 initialization/metadata fixes, Task 003B native loader tests, and
  Task 004 transport/discovery changes were present. Existing suites were retained.
- No merge or pull request was made. Only the new safety branch was pushed.

All four audited defects were present at the specified source revision. No
substitute implementation or newer upstream baseline was used. The Task 005
read-only audit supplied the source references; each was checked against code.

## F01 — Verification mismatch must fail

**Before:** `write_mem()` completed the final sector CRC comparison, counted
mismatches and logged an error, then returned `STATUS_SUCCESS`. `run()` displayed
the successful-operation dialog. `check_romcrc()` also checked only `length > 5`
before reading response bytes 8–12 and ignored the write result.

**Correction:** a mismatch following actual programming returns
`STATUS_VERIFY_ERROR` (2), distinct from transport/protocol error (1) and
cancellation (3). Nested phase results propagate to `run()`. Its existing final
dialog now explicitly reports verification failure, and the logger marks failure
or cancellation rather than unconditionally announcing completion. No automatic
retry or reprogramming was added. The CRC request requires successful submission
and at least 13 response bytes before parsing; short responses fail closed.

The CRC polynomial, algorithm, input bytes, addresses, lengths, framing and
comparison order are unchanged. Dry-run `test_write` keeps its existing validate
semantics: it is not an assertion that ECU flash has been changed to match the
image. Its successful validation result must not be treated as hardware readiness.

Production references at the tested revision:

- `modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp`: `write_mem`, lines 1036–1147,
  especially the mismatch return at 1135.
- Same file: `check_romcrc`, lines 1190–1269; submission/result and response bound.
- Same file: `run`, lines 44–168, final outcome handling at 142–155.

**Executed tests:** `verification` covers matching final CRCs, one mismatch,
multiple mismatches, a failed final CRC request and a 12-byte malformed response.
`crcBounds` covers every response length 0–12; `failedCrcWrite` rejects submission
failure. `uiResult` executes real `run()` with scripted kernel-ID replies and
checks success, mismatch failure and communication failure dialogs. `noChanges`
asserts only comparison commands are sent when the initial comparison matches.

A separate build of the **Task 004 production module** against the same fake
reproduced the defect: `verification:one-post-program-mismatch` returned 0 rather
than expected 2, producing **2 passed, 1 intentionally failed**, exit 1. This is
baseline evidence, not a failure of the corrected suite.

## F02 — Exact image/model/bounds validation

**Before:** incorrect size in checksum preparation merely warned and returned the
original object. Missing definitions/checksum configuration could bypass that
check. The module copied the image into an allocation of its actual length but
compared/programmed the full selected memory map through unchecked raw pointers.

**Correction:** `modules/ecu/subaru_sh7058_safety.h` provides shared validation:

1. Exact 1,048,576-byte image; no resizing, truncation or padding.
2. A known SH7058 1 MiB model, nonnull block table, 1–16 blocks, contiguous complete
   coverage from zero, nonzero 4 KiB-aligned block lengths and no out-of-range end.
3. A linked definition and the selected supported checksum configuration before
   image preparation is accepted. Missing prerequisites return `Unsupported`.
4. Nonempty bounded source ranges before CRC or flash-buffer indexing. Flash
   ranges also require alignment to the existing 4 KiB commit granularity.

`Definition-availability flags default to false on new calibration objects.
`FileActions::checksum_correction` performs this target-specific preflight before
legacy warning/bypass paths and only assigns a usable same-sized result. It
records a SHA-256 fingerprint of the prepared image. `run()` checks size/model
and that fingerprint **before serial setup/open, bootloader connection or kernel
upload**. `write_mem()` repeats the fingerprint/size guard because UI event
processing can occur between entry and writing. Raw-buffer phase functions have
additional image/map/range guards before using source pointers.

`MainWindow::start_ecu_operations` blocks unavailable checksum configuration for
this module. Write and save callers preserve their object pointer and return on
preparation failure, avoiding the previous null assignment/dereference hazard.
Invalid preparation does not overwrite the working ROM or save it to disk.

The dispatch review found EcuTek/RaceROM/COBB names also enter this same module.
The guard follows those names; the separately dispatched diesel and K-Line
modules are excluded. This is necessary coverage of the existing dispatch,
not a change to vendor-specific behavior. `SH7058_1block` is accepted only if its
existing 1 MiB map passes the same structural checks; its hardware semantics were
not validated by this task.

References:

- `modules/ecu/subaru_sh7058_safety.h`: `usesModule`, `validMap`, `memoryMap`,
  `validImage`, `validRange`, `prepare`.
- `file_actions.cpp`: `checksum_correction`, lines 2551–2570.
- `file_actions.h`: prepared-image fingerprint, lines 334–336.
- `mainwindow.cpp`: write preflight at 1055–1060 and preparation failure at
  1089–1094; save/save-as checks at 1494 and 1539.
- SH7058 module: `valid_image` at 38–42, `run` at 62–69; guards at starts of
  `write_mem`, `get_changed_blocks`, `check_romcrc`, `reflash_block`, `flash_block`.

**Executed tests:** `preflight` covers empty, one-byte-short, exact, oversized,
missing definition, missing checksum, unsupported/wrong MCU, and invalid sizes
with missing prerequisites. Every rejected case executes production `run()` and
asserts **zero serial setup calls and zero submitted commands**. Tests also cover
stale preparation, inconsistent map, raw source boundaries and protocol dispatch.
A malformed checksum configuration independently blocks real module entry, and
a fresh calibration object defaults to unavailable definitions and no fingerprint.
Synthetic data only; definition availability is a flag, not a private XML fixture.

## F03 — Disabled checksum preserves the image

**Before:** the global checksum-disabled sentinel returned `0`, which constructed
an empty `QByteArray`; callers replaced the working image with that empty result.
Checksum descriptor/range reads were also unchecked.

**Correction:** `ChecksumEcuSubaruDensoSH7xxx::evaluate` returns a typed result:
`Disabled`, `Valid`, `Corrected`, `Invalid` or `Unsupported` (the shared preflight
uses the last state for missing prerequisites). Disabled returns the **original
complete image**. Invalid results are not usable. Table bounds, 12-byte descriptor
size, offset-adjusted range ordering, alignment and image bounds are checked
before indexing. Supported checksum arithmetic and descriptor output bytes are
unchanged. Corrections still affect only the specified checksum table.

The existing `calculate_checksum` API delegates to the same implementation and
preserves the original image on an invalid result. The SH7058 programming route
uses the typed result and **blocks** invalid/unsupported preparation; preserving
bytes alone is not considered preparation success. Other ECU modules were not
reworked to adopt typed preparation in this task.

References: `modules/checksum/checksum_ecu_subaru_denso_sh7xxx.{h,cpp}`,
`calculate_checksum` at 13–23, `evaluate` at 25–130, disabled return at 72;
`FileActions` guarded assignment at 2561–2568.

**Executed tests:** exact-size/byte/SHA-256 preservation for the disabled sentinel,
repeat evaluation, legacy wrapper preservation for disabled and invalid results,
missing/short checksum table, reversed/out-of-bounds/unaligned ranges, valid and
corrected arithmetic paths, and complete synthetic 1 MiB preparation. A known
sum of 1 + 2 produces the same expected checksum word `0x5AA5A557`; the complete
expected output is compared, then evaluates as valid without further changes.

No equivalence with EcuFlash's `subarudbw` is established. No owner's ROM/XML was
opened, reconstructed, changed or committed.

## F04 — Cancellation before command submission

**Before:** the final comparison delay processed UI events after the last loop
cancellation check. Reflash initialization and erase could follow an acknowledged
close event; `flash_block` first checked cancellation after erase acknowledgement.
Progress updates could likewise process cancellation before validate/commit.

**Correction:** cancellation is checked after every comparison delay, at reflash
and flash phase entry, before flash initialization, and immediately before every
submission in initialization/reflash/flash. These include flash enable/disable,
erase, every buffer write and validate/commit. Nonzero cancellation results
propagate upward; the final UI predicate also rejects success if the flag is set.
No polling/retry strategy, delay duration, command order or recovery action was
introduced.

References in the SH7058 module:

- `closeEvent`, lines 175–178: existing cancellation flag.
- `get_changed_blocks`, lines 1154–1183: post-delay cancellation check.
- `init_flash_write`, lines 1307–1446: entry and pre-submission guards.
- `reflash_block`, lines 1453–1529: entry, query and downstream result propagation.
- `flash_block`, lines 1536–1752: pre-erase, per-buffer and pre-validate/commit guards.

**Executed tests:** cancellation before comparison, during the final comparison
UI delay, before initialization, before flash enable, before erase, at the erase
submission boundary, after erase acknowledgement, during buffer writes, before
validate/commit and at their submission boundaries. Cancellation is injected
through the real `closeEvent`, queued timer events, fake acknowledgement callbacks
or production log signals. Each asserts the complete command trace stops at the
moment cancellation is observed, and cancellation never means success.

**Boundary:** cancellation before a command prevents that command. Cancellation
once erase/commit has already been submitted cannot undo it or establish the ECU's
state. Tests after erase acknowledgement intentionally show that the erase was
already submitted but no subsequent destructive command is sent. No recovery or
automatic retry is attempted or claimed.

## Files changed

| File | Purpose |
|---|---|
| `modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp` | F01/F02/F04 and required MSVC portability |
| `modules/ecu/flash_ecu_subaru_denso_sh7058_can.h` | Distinct error states, private test friend, guards |
| `modules/ecu/subaru_sh7058_safety.h` | Shared fail-closed preflight |
| `modules/checksum/checksum_ecu_subaru_denso_sh7xxx.cpp` | F03 typed evaluator and bounds |
| `modules/checksum/checksum_ecu_subaru_denso_sh7xxx.h` | Typed result API |
| `file_actions.cpp` | Guarded target-specific image preparation |
| `file_actions.h` | Prepared-image fingerprint |
| `mainwindow.cpp` | Block failed/missing preparation without losing object pointers |
| `tests/subaru/safety.cpp` | 64 QtTest results, production phases and UI outcomes |
| `tests/subaru/stubs/serial_port_actions.h` | Offline compile-time facade substitute |
| `tests/subaru/safety.pro` | Qt unit executable including actual production module/UI/checksum |
| `tests/subaru/README.md` | Reproduction and coverage boundaries |
| `tests/j2534/run-windows.ps1` | New target plus text/JUnit/build evidence |
| `.github/workflows/j2534-windows.yml` | Safety-branch trigger and relevant path filters |
| `docs/task-006a-subaru-critical-safety.md` | This report |

The only supporting production portability changes replace ambiguous zero-byte
append expressions with explicit `char` zero and remove an unused variable-length
array unsupported by MSVC. Initializing existing progress-display speed/time
variables avoids an uninitialized read in the exercised path. These changes do
not change submitted bytes, timing or arithmetic.

## Executed validation and evidence

Linux toolchain: Debian 13, GCC 14.2.0 (`14.2.0-19`), Qt 5.15.8, qmake 3.1.

| Check | Result | Workspace evidence |
|---|---|---|
| Full Linux application compile/link | Passed, exit 0; clean build plus final header rebuild; complete `FastECU` executable | `/workspace/fastecu-env/task006-app-clean-build.log` |
| J2534 initialization | 40 passed, 0 failed/skipped | `task006-initialization-results.txt` |
| J2534 transport | 38 passed, 0 failed/skipped | `task006-transport-results.txt` |
| Linux Win32-loader bridge | 34 assertions passed | `task006-loader-results.txt` |
| Subaru safety, release | 64 passed, 0 failed/skipped | `task006-subaru-results.txt` and `.xml` |
| Subaru safety, ASan + UBSan | 64 passed, 0 failed/skipped; exit 0, no sanitizer findings | `task006-subaru-asan-minimal-results.txt`, `task006-subaru-asan-minimal-console.log` |
| Workflow syntax | actionlint 1.7.7, exit 0 | `.github/workflows/j2534-windows.yml` |
| Whitespace review | `git -c core.whitespace=cr-at-eol diff --check`, exit 0 | Existing source CRLF preserved |

Evidence filenames without full directories above are under
`/workspace/fastecu-env/`. Builds are not warning-free: existing unused variables/parameters, conversions
and Qt/GCC-header warnings remain; no general warning cleanup was attempted.
No tests were skipped. QtTest totals include the two
suite lifecycle results: the 64-result safety suite contains 62 test cases/rows.
The bridge tests are Linux emulation, not native Windows ABI evidence.

Reproduction commands are in `tests/subaru/README.md`; this cloud environment
additionally sources `/workspace/fastecu-env/activate.sh` and passes
`QMAKE_LIBDIR+=/workspace/fastecu-env/lib` for its relocated Qt installation.

ASan/UBSan used a separate debug build with
`-fsanitize=address,undefined -fno-omit-frame-pointer`, matching linker sanitizer
flags, and `QT_QPA_PLATFORM=minimal`. Leak detection remained enabled; no
suppressions were used. During harness development, three UI assertions initially failed because the
automation dismissed a box without selecting its button; clicking the actual
button corrected the harness (no production change). The first X11 sanitizer
attempt also reported 3,672 retained bytes at exit. After that harness correction
and disabling DBus/accessibility integration, the X11 run passed all then-current
56 assertions but still reported 1,608 retained bytes. Those runs are **not**
claimed clean. The final minimal-platform run passed without
those environment switches and without leak findings. This is GUI-backend-limited
sanitizer coverage, not validation of the full application's memory safety.

### Command preservation evidence

The Task 004 module was compiled separately from `git show 59c3a25:...cpp`, using
the same fake transport and synthetic image. Both normal sequences passed
(**4 QtTest results**, including lifecycle). SHA-256 of all submitted messages,
each prefixed by its four-byte big-endian length, matched the corrected module:

- Commit path: `313959741b6a29c3182871eb3ee854976018c7c7cd8ad2231b9b0e3244788adf`.
- Validate path: `a7f629163a60e21569202a7ebe6bd74a99353826ed6238b076abaca251a85703`.

These hashes are now assertions in `normalSequence`. Additional assertions check
CAN address prefix, command order, eight 512-byte buffer writes and their exact
source bytes. Evidence: `task006-baseline-command-trace.txt` and
`task006-current-command-trace.txt`; baseline F01 reproduction is recorded in
`task006-baseline-F01-reproduction.txt`. Coverage is the scripted one-sector
success case, not every possible sector combination or live ECU response.

### Native Windows CI

The final native Windows run passed, as recorded below. Initial run
[40169275468](https://github.com/ChuckyT83/FastECU/actions/runs/40169275468)
failed compilation at an existing ambiguous zero-byte append. The explicit-char
correction preserves bytes, as shown by the baseline trace hashes. Run
[40169515412](https://github.com/ChuckyT83/FastECU/actions/runs/40169515412)
compiled but exited with access violation `0xc0000005`; the initial public
annotation omitted the failing case. Diagnostic follow-ups retained startup markers, unbuffered progress and the
first exception frames. Runs
[40169800147](https://github.com/ChuckyT83/FastECU/actions/runs/40169800147),
[40169998532](https://github.com/ChuckyT83/FastECU/actions/runs/40169998532),
[40170110631](https://github.com/ChuckyT83/FastECU/actions/runs/40170110631),
[40170161713](https://github.com/ChuckyT83/FastECU/actions/runs/40170161713), and
[40170280928](https://github.com/ChuckyT83/FastECU/actions/runs/40170280928)
used nonnative GUI backends and exposed the same Qt dialog issue. These are
failed runs, not passing validation.

The stack pointed into `QMessageBox::showEvent`. Qt 5.15.2 source shows its
Windows-specific system-menu helper dereferences `platformNativeInterface()`;
the minimal integration inherits an implementation returning null. See
[Qt message-box helper](https://github.com/qt/qtbase/blob/v5.15.2/src/widgets/dialogs/qmessagebox.cpp#L78)
and [default platform interface](https://github.com/qt/qtbase/blob/v5.15.2/src/gui/kernel/qplatformintegration.cpp#L115).
Changing style alone did not resolve it. The test runner therefore uses the
**native `windows` Qt platform** and Fusion style for this GUI target; timers
select its real widget buttons automatically. The old serial suites retain
`offscreen`. This corrects test infrastructure without replacing production
dialog/phase logic or skipping UI assertions. Final outcome is recorded below.

Final run: [40170344033](https://github.com/ChuckyT83/FastECU/actions/runs/40170344033),
[job 121099726276](https://github.com/ChuckyT83/FastECU/actions/runs/40170344033/job/121099726276),
commit **`e309bd959f425bcca738de0a841c034deb982257`**: **succeeded**, 2m 8s.

| Native Windows target | Executed result |
|---|---|
| Mock DLLs and Windows loader | Built; **34 assertions passed** |
| Initialization and cleanup | **36 passed, 0 failed, 0 skipped**, 1,007ms |
| Transport/discovery | **40 passed, 0 failed, 0 skipped**, 351ms |
| SH7058 safety | **64 passed, 0 failed, 0 skipped**, 11,245ms |

The public job notice records these exact totals and toolchain:
**qmake 3.1, Qt 5.15.2 MSVC2019 x64, MSVC 2022 v143 toolset 14.44.35207**.
All four test targets compiled and executed. Tests used only our native mock
DLLs or the fake serial facade; no vendor driver/hardware was needed.
Text/JUnit results and compiler/build logs are retained by the workflow artifact
`j2534-windows-40170344033-1` for 14 days and summarized in the job. The repository's
existing pinned actions emitted a Node 20 deprecation/forced-Node-24 warning;
the run nevertheless completed successfully. No action pins or permissions were
weakened to obtain this result.

The workflow retains `windows-2022`, native x64 MSVC, Qt 5.15.2 MSVC2019 x64,
minimal `contents: read`, pinned existing actions, nonzero exit propagation and
always-uploaded evidence. The safety branch/path triggers were added. It builds
our mock J2534 DLLs, native loader, existing initialization/transport executables,
then the production SH7058/checksum unit target. The safety target uses Qt base,
XML, widgets and serialport, so no new installer action or Qt archive was needed.

Full Windows application compilation is not claimed. MainWindow/FileActions are
compiled in the complete Linux build; their shared preparation helper and actual
SH7058 entry guard execute in both platform unit suites. The private friend is
the only module test-access seam. The fake facade implements the exact subset of
serial signatures used; NRC diagnostic text formatting is stubbed. The real
facade/backend boundary remains covered separately by the existing J2534 suites.

## Review, remaining limitations and Task 006B

The complete baseline-to-branch diff was reviewed. No CAN IDs, framing constants,
CRC algorithm, checksum arithmetic, kernel commands, flash-sector definitions,
voltage settings, protocol timing, retry strategy or kernel binaries were changed.
No ROM/XML files, unrelated ECU modules or proprietary data were modified.
All communication in tests uses fakes/mocks, never an ECU or physical adapter.

No normal application route to this module was found that bypasses size/map and
prepared-image checks: direct module write entry checks preparation independently
of the UI and write phase checks it again. Private raw-pointer helpers depend on
their internal caller providing the validated snapshot; they cannot prove the
allocation length of an arbitrary forged pointer. The fingerprint is a stale-image
guard, not a trust/security mechanism. It does not authenticate a definition,
validate the ECU/ROM pairing, or prove that a calibration is safe.

Remaining audit findings require a separate task:

1. `run()` remains a void UI interface; structured outcomes are not exposed to
   its outer caller. Higher-level callers still ignore many non-CRC transmit results; a delayed or
   stale acknowledgement must not mask submission failure. Propagate actual
   transport outcomes before claiming general communication safety.
2. Other SH7058 response parsers still have insufficient length checks (including
   identification/security, kernel capability/voltage and commit responses).
   Malformed responses outside the CRC path remain unvalidated by this task.
3. Kernel-transfer acknowledgements, cancellation and completion semantics need
   their own review/tests. No security or kernel strategy was changed here.
4. Facade empty reads still cannot distinguish transport error from timeout;
   message boundaries, flags and generic-adapter interoperability remain open.
5. ROM/XML identity, inherited/custom definitions and EcuFlash `subarudbw`
   equivalence are not established. No private calibration was available/needed.
6. Full application Windows build/runtime, vendor-driver ABI/interoperability,
   in-flight cancellation and actual ECU recovery are unverified.
7. Shared checksum callers outside this CAN module retain the legacy byte-array
   API; invalid input now preserves bytes, but their programming gates have not
   been upgraded to typed preparation in this task.

**Recommended Task 006B:** narrowly propagate failed/partial writes and structured
receive outcomes through the SH7058 callers, validate every response before any
indexing, and prove fail-closed kernel-transfer acknowledgements and phase
transitions with scripted failures. Add real FileActions/MainWindow preparation
integration coverage and targeted ROM/XML identity tests; keep recovery/protocol
changes separately reviewed. Retain both native Windows and sanitizer gates.

**Hardware readiness: not ready for ECU programming.** These four corrections and
offline tests remove specific dangerous success/bypass paths. They do not prove
FastECU compatibility with OBDX Pro VX, the owner's ROM or the Subaru ECU. Correct
and validate the remaining critical communication/parser/kernel findings before
considering physical programming. No hardware testing is recommended by this task.

## Review disposition

Ready for focused code review of F01–F04 with passing Linux/native Windows
regressions and sanitizer evidence. Task 006A acceptance criteria are met for
the tested offline paths. This is not general flashing-safety approval, ROM
compatibility approval or authorization to begin ECU programming.

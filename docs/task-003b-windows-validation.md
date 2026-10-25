# Task 003B — Native Windows J2534 validation

## Result and provenance

Native x64 Windows validation **passed** on the GitHub-hosted `windows-2022`
runner. No adapters, ECUs, vendor drivers, ROMs or programming operations were used.

Repository: `https://github.com/ChuckyT83/FastECU.git`.
Branch: `fix/j2534-initialization-reliability`.
Initial clean checkout contained Task 003 at
`503a0847be5df5485645c0c9603f90321a02fd9b`.
Reference baseline: `d5f8c25d27608253a45c7e5c05c4eb65166c400a`.

**Tested commit:** `f1a6abd878a0ed78a83dd2decabdb78cae4970ce`.
The subsequent report-only commit does not change the tested workflow, source or
executable tests.

**Passing run:** <https://github.com/ChuckyT83/FastECU/actions/runs/40150698468>

**Passing job:** <https://github.com/ChuckyT83/FastECU/actions/runs/40150698468/job/121041476556>

The public job annotation and run summary report:

```text
QMake version 3.1
Using Qt version 5.15.2 in D:/a/FastECU/Qt/5.15.2/msvc2019_64/lib
Loader regression checks passed: 34
Totals: 36 passed, 0 failed, 0 skipped, 0 blacklisted, 1008ms
```

QtTest's 36 count includes `initTestCase` and `cleanupTestCase`; there are 34
functional/data rows. The standalone loader's 34 assertions are a separate count.
The successful workflow completed in 1m 13s, including dependency setup. Both mock
DLLs, the production loader target and the production initialization target built
successfully. The compiler was native x64 **MSVC 2022 (v143)**; Qt's **5.15.2
MSVC 2019 x64** release binaries are ABI-compatible with this compiler. Actual
compiler component versions are retained by `/Bv` in `build-mock.log`; this report
does not substitute a guessed compiler patch version for that evidence.

Artifact: `j2534-windows-40150698468-1`, retained for 14 days. It contains qmake
version/configuration, compiler path/version output, all build/test logs, and
QtTest text/JUnit results. GitHub login may be required to download it. Build
success is not a claim of a warning-free compile; compiler diagnostics are retained
in these artifacts for detailed review.

## Workflow and reproduction

`.github/workflows/j2534-windows.yml` has only `contents: read`, does not persist
checkout credentials, and runs on J2534-related pushes to the dedicated branch.
It also declares `workflow_dispatch`; GitHub may require the workflow to exist on
the default branch before offering manual dispatch, so the verified trigger here
is the development-branch push. No changes were pushed to master.

All five action references use reviewed, full commit SHAs. The Qt installer uses
the pinned Node sub-action directly because its top-level composite delegates to
mutable action tags. Python 3.11, aqtinstall 3.3.0 and py7zr 1.0.0 prepare only the
Qt base/SerialPort archives needed for the offline targets. Qt downloads retain
normal installer verification. The entire FastECU application and its OpenSSL,
Charts, Remote Objects and WebSockets dependencies are not needed for this suite.

`tests/j2534/run-windows.ps1` reproduces the build/tests in a PowerShell 7 x64 MSVC
developer shell with matching Qt in PATH. Every external command's exit status is
checked. Failures are propagated; logs are uploaded even after failure. Concise
failure annotations avoid GitHub's display truncation. Successful annotations
preserve exact test counts on the public job page.

Git operations worked with existing platform authentication. The environment
proxy rejected `api.github.com` CONNECT requests, so `gh api` could not retrieve
logs. Authorized pushes triggered the workflow, and public GitHub run/job pages
provided status and evidence. No API token was requested or extracted. A temporary
network-domain draft addition was removed when the public-page route sufficed;
no environment-settings change is needed from the user.

## Native coverage

| Requirement | Native evidence |
| --- | --- |
| DLL load/unload | Loader checks module presence after incomplete loading, disable, repeated disable/reload and destruction using `GetModuleHandleA`, which adds no reference |
| Missing/invalid paths | Missing file, null/empty path and overlong path fail; valid DLL subsequently loads |
| Required exports | DLL omitting `PassThruIoctl` fails resolution and unloads; initialization can retry with complete DLL |
| Device/channel ownership | Distinct IDs 41/99, wrong-ID rejection and valid zero-valued handles; disconnect before close |
| Open and Connect failures | Four-protocol failure matrix; no ownership acquired from error-returned output handles |
| Mandatory IOCTL/filter failures | SET_CONFIG failure across four protocols, CLEAR_MSG_FILTERS failure and message-filter setup failure |
| Partial initialization/retry | Failure matrix verifies acquired resource cleanup, unset readiness, released wrapper reference and subsequent successful retry |
| Cleanup errors | Disconnect/Close failure clears local state, does not double-release on another reset, and permits retry |
| Metadata | Normal, unsupported, empty, 79-byte terminated, 80-byte unterminated and 256-byte unterminated responses; serial query unavailable without buffer overwrite |
| Successful lifecycle | Successful initialization, repeated initialization, explicit reset and destructor cleanup |
| Public selection | Selected mock DLL path retained instead of vendor display name; no probing double-open and no serial fallback after J2534 failure |

The integration fixture holds a control DLL reference to configure/read the mock.
It checks release of the production wrapper's reference. The separate standalone
loader process verifies actual unloading without that control reference.
Linux shared initialization regressions were also rerun after the compile
correction: **40 passed, 0 failed, 0 skipped**. The updated Linux loader bridge
passed **34 checks**. Workflow syntax passed actionlint 1.7.7 (release archive
checksum verified).

## Failures investigated and narrowly scoped corrections

1. Initial run [40150161754](https://github.com/ChuckyT83/FastECU/actions/runs/40150161754)
   built and ran the loader but failed compiling the initialization target.
2. Run [40150364962](https://github.com/ChuckyT83/FastECU/actions/runs/40150364962)
   added public diagnostic tails, but long generated compiler command lines
   exhausted GitHub's annotation display length before the useful errors.
3. Run [40150532350](https://github.com/ChuckyT83/FastECU/actions/runs/40150532350)
   filtered/capped diagnostics and exposed three MSVC C2668 errors:
   `QByteArray::append((uint8_t)0x00)` was ambiguous between byte and pointer
   overloads. All three pre-existing calls now use `append('\0')`, selecting the
   byte overload explicitly. Each still appends exactly one zero byte. No transmit
   algorithm, framing, identifier, rate or timing was changed.
4. The next run, linked above, compiled both targets and passed all tests.

One unmatched quote in the inactive macOS loader branch was also corrected to
avoid malformed-string preprocessing diagnostics. It has no active Windows logic
or ECU behavior change. There were no native regression assertion failures after
compilation succeeded. GitHub reports a Node 20 action-runtime deprecation warning
and forces the pinned actions onto Node 24; setup/upload and the job passed under
that policy. Future action upgrades should retain SHA review and rerun the suite.

One local push attempt inherited the workspace Qt toolchain's library path and
failed before contacting GitHub due to an OpenSSL version mismatch. Repeating it
from a clean shell succeeded; this was not a permissions or Windows-code failure.

## Files changed since Task 003

- `.github/workflows/j2534-windows.yml`: native runner, pinned dependencies,
  permissions, branch/path trigger, timeout, artifact retention.
- `tests/j2534/run-windows.ps1`: reproducible build/run, failure propagation and
  evidence recording.
- `tests/j2534/loader.cpp`: actual native unload assertions and incomplete-load
  cleanup check, mirrored by Linux bridge checks.
- `tests/j2534/mock_dll.cpp`: configurable zero-valued handles/reset defaults.
- `tests/j2534/windows_reliability.cpp`: zero-handle and cleanup-error retry cases.
- `tests/j2534/README.md`: runner and local reproduction instructions.
- `serial_port/J2534_win.cpp`: inactive-branch quote correction only.
- `serial_port/serial_port_actions_direct.cpp`: three explicit zero-byte appends.
- This report.

Task 003's original diff and these corrections were reviewed. No checksum,
security-access, flashing, ROM/XML, sector-layout or programming-kernel files or
algorithms were modified. Generic initialization failures remain fatal, optional
unsupported version/serial metadata remains nonfatal, and cleanup failures leave
local readiness/ownership flags reset. No protocol values or filters were altered.

## Review readiness and Task 004

Task 003 is ready for code review with native x64 offline evidence. This is not a
claim of OBDX Pro VX compatibility or ECU programming safety. Remaining risks:

- Windows x86/stdcall decoration, driver bitness mismatches and real registry
  discovery are untested. The existing registry-format selection checks `_WIN32`
  before `_WIN64`, so x64 discovery still warrants a separate focused review.
- Real vendor DLL responses, concurrent API behavior and drivers refusing to
  release resources cannot be proven by mocks. Failed vendor Close/Disconnect can
  retain external resources despite safe local state reset.
- Full Windows application packaging/linking, hardware transport and flashing are
  outside this native test target. Runtime transmit/receive defects listed in the
  Task 003 report remain outside scope.
- Compiler warnings are preserved rather than suppressed; the full artifact
  warning inventory was not retrieved through the blocked API. The public run
  evidence establishes build/test success, not a clean-warning certification.

Proceed to Task 004 as an **offline generic transport/registry/ABI review** with
mock-based runtime status/length/timeout checks. Do not proceed directly to ECU
programming or infer hardware support. Any physical evaluation needs a separate,
explicitly approved plan. No merge or pull request was performed.

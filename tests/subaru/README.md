# SH7058 offline safety regression

Task 006G's candidate upload orchestration, production-entry gate, sector-range
guards and their limitations are documented in
[CANDIDATE_UPLOADER.md](CANDIDATE_UPLOADER.md).

For the separately isolated kernel state-machine experiment, see
[KERNEL_OFFLINE.md](KERNEL_OFFLINE.md). Its simulated acknowledgements do not
establish an ECU upload contract or enable the production uploader.

This QtTest target compiles the production SH7058 CAN module, its generated UI,
SH7xxx checksum implementation and shared preflight helper. Only the serial
facade and NRC diagnostic formatting are replaced at link/compile time. The
fake serial header implements the same subset of facade signatures used by the
module. No real serial, J2534 or remote backend is linked. No vendor DLL, private
ROM, definition file or physical hardware is needed. Fixtures are synthetic.

The friend fixture invokes production private phase functions and `run()`;
QTimers select dialog buttons and deliver cancellation during the actual final
comparison delay. Assertions cover results, final UI verdicts, whole-image
preservation and exact command order/payloads. Both commit and validate paths
use only scripted fake acknowledgements. These tests do not execute any kernel.

Linux (Qt 5.15, C++11):

```sh
mkdir build-subaru && cd build-subaru
qmake ../tests/subaru/safety.pro CONFIG+=release
make -j4
QT_QPA_PLATFORM=offscreen ./subaru-safety -o results.txt,txt -o results.xml,junitxml
```

For an ASan/UBSan build, use a fresh build directory:

```sh
qmake ../tests/subaru/safety.pro CONFIG+=debug CONFIG-=release \
  'QMAKE_CXXFLAGS+=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  'QMAKE_LFLAGS+=-fsanitize=address,undefined'
make -j4
QT_QPA_PLATFORM=minimal ./subaru-safety
```

For relocatable Qt installations also pass the relevant `QMAKE_LIBDIR` and set
`LD_LIBRARY_PATH`. Windows: use the repository's `tests/j2534/run-windows.ps1`
from an x64 MSVC developer shell with Qt 5.15.2 MSVC2019 x64 on PATH. That script
runs the old suites plus this target and records text/JUnit/build logs.

Coverage limits: FileActions/MainWindow integration is compiled by the full
Linux application build, not linked into this unit executable. Tests call the
same preparation helper those routes use and assert rejected inputs cause
zero serial setup/submissions in production `run()`. Native Windows tests
compile the production module/checksum code, not the full application. They
cannot establish adapter compatibility, kernel correctness, voltage safety or
recovery behavior. A cancellation guard prevents a new submission after the
flag is observed; it cannot retract an operation already in flight.

The normal commit/validate tests assert whole-command-trace SHA-256 values
measured from the Task 004 production module using the same fixture. Windows
runs the GUI suite with `QT_QPA_PLATFORM=windows` and Fusion style; the serial suites retain their
existing backend. The explicit test main flushes output so abnormal termination
preserves startup markers and the last entered test in CI diagnostics.

## Task 006N identification/compatibility

`preflight.pro` tests the production identity parsers, offline-only acquisition/
request/deadline/cleanup workflow, XML inheritance, exact byte-preserving BIN/
ECU/definition/protocol/kernel policy and permanent ROM Read admission denial.
Fixtures are synthetic; no private XML/BIN or physical J2534 provider is linked.
Build/run like safety.pro with Qt Widgets/XML/Test. The native Windows runner
includes this target and publishes its exact QtTest total. Passing a fixture is
not ordinary-session firmware evidence or live identification permission.

Task 006Q extends this same target with independent hardware-family, target-integrity,
transition and operation-readiness assertions. Stock/custom transition images and
qualified-capture scenarios are synthetic. Production family association requires
the reviewed public definition fingerprint and ROM ECU-ID layout evidence; fixture
fingerprints are injected only by unit callers. No GUI/CLI force option exists.
All results retain disabled programming admission and immutable inputs.

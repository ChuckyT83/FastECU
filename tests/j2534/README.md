# Offline J2534 regression tests

These tests never connect to a physical adapter or ECU. They do not establish
OBDX Pro VX compatibility, ECU programming safety, or ROM compatibility.

## Linux: production connection orchestration

Requires Qt 5 Core, Widgets, SerialPort and Test development packages, qmake,
a C++11 compiler and make. Build outside the checkout:

```sh
mkdir -p /workspace/build-FastECU-tests
cd /workspace/build-FastECU-tests
# In the prepared cloud environment:
. /workspace/fastecu-env/activate.sh
qmake /workspace/FastECU/tests/j2534/reliability.pro QMAKE_LIBDIR+=/workspace/fastecu-env/lib
make -j4
QT_QPA_PLATFORM=offscreen ./j2534-reliability
```

`mock_j2534.cpp` replaces the hardware backend at link time, while compiling the
actual `serial_port_actions_direct.cpp`. The friend fixture accesses the private
initialization entry point, avoiding serial enumeration, registry access and
physical I/O. Linux's existing backend represents channel IDs as protocol IDs;
these tests explicitly check that contract. The mock allocates the physical-port
QObject but never opens it.

The suite covers all four protocol paths, each mandatory initialization failure,
partial cleanup, repeated attempts, destructor cleanup, disconnect/close errors,
retry, zero device ID, unavailable error text, unsupported serial metadata, and
version strings of length 0, 5, 79, 80 and 256. Unterminated responses at the
80-byte API boundary are rejected. A malicious driver writing beyond the caller's
allocation cannot be contained by this API.

For ASan/UBSan add both `QMAKE_CXXFLAGS+=-fsanitize=address,undefined
-fno-omit-frame-pointer` and `QMAKE_LFLAGS+=-fsanitize=address,undefined` as quoted
qmake arguments in a separate build directory. Run under `xvfb-run -a` using the
X11 plugin: Qt 5's offscreen plugin retains display objects at process shutdown.
Keep sanitizer checks enabled.

## Linux: production Windows loader logic with a test-only bridge

```sh
/workspace/FastECU/tests/j2534/run-loader-linux.sh /workspace/build-FastECU-loader
```

The script builds complete and incomplete mock shared libraries and compiles the
actual `J2534_win.cpp` with a tiny Win32-loader bridge to `dlopen`/`dlsym`/`dlclose`.
Checks include missing DLL/export, returned failures, version argument order,
distinct device/channel IDs, invalid DLL names, disable/reload, double disable,
and destructor unloading. This tests loader logic on ELF. Linux `unsigned long`
size and calling conventions differ from Windows: this is **not Windows ABI
validation**. The pre-existing syntax warning in the inactive macOS loader block
is unrelated to these checks.

## Native Windows (implemented, not executed in this cloud instance)

Use an x64 MSVC developer shell with Qt 5 x64 MSVC in PATH. Build both mock DLLs,
then the standalone loader test and the production integration suite. See the
proposed commands in `docs/task-003-j2534-reliability.md`.

`windows_reliability.cpp` links the actual connection code and actual Windows DLL
wrapper to the mock DLL, including failure/retry matrices and metadata boundaries.
It never calls `open_serial_port`/enumeration and therefore never touches adapters.
The control `QLibrary` deliberately holds a reference during integration tests;
the standalone loader test tests unloading without that reference. Windows x86
stdcall decorated exports and real driver behavior remain unvalidated.

## Task 003B Windows runner

The committed `.github/workflows/j2534-windows.yml` runs the native x64 targets on
`windows-2022` using MSVC 2022 and the ABI-compatible Qt 5.15.2 MSVC 2019 x64
binaries. It runs on pushes affecting J2534 code/tests/workflow on the dedicated
`fix/j2534-initialization-reliability` branch, or by manual dispatch. It never
runs the FastECU application or scans the registry/serial ports.

For local Windows reproduction, enter an x64 MSVC developer shell with Qt 5's
`bin` directory in PATH, then run PowerShell 7 from the checkout:

```powershell
./tests/j2534/run-windows.ps1
```

The script compiles both mock DLLs and the actual production Windows wrapper and
connection code. It records compiler versions, qmake configuration, build output,
loader assertion counts, and QtTest text/JUnit reports in `build-j2534/logs`.
Any native command failure terminates the script; the workflow uploads available
logs even on failure. `build-j2534` is ignored by the existing `*build-*` rule.
The Windows loader now checks actual module presence after partial-load failure,
disable and destruction, without adding references using `GetModuleHandleA`.
The Linux bridge mirrors these assertions (34 checks after Task 003B additions).
Native integration cases additionally cover valid zero-valued handles and
Disconnect/Close failure followed by retry.

Task 003B native x64 results and the passing run are recorded in
[`docs/task-003b-windows-validation.md`](../../docs/task-003b-windows-validation.md).

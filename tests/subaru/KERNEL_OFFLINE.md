# Offline kernel-transfer experiment

This is an **isolated synthetic model**, not an uploader or an ECU specification.
It has no serial/J2534 backend or I/O callbacks. No production target includes
`experimental_kernel_transfer.h`. The production `upload_kernel` gate remains
unchanged and returns Unsupported before any upload request.
The model also refuses compilation without the dedicated
`FASTECU_OFFLINE_KERNEL_TESTS` define set only by its test target.

The only production addition is `SubaruSH7058::KernelLayout`, used by the existing
preparation function to validate host bounds/padded sizes. It provides exactly
`preparedSize / 128` nonempty blocks, rejects an index equal to the count, and
uses wide arithmetic. The SH7058 runtime RAM map is a host memory-safety limit;
it is **not** proof of the bootloader's allowed download range or block size.
The existing safety suite calls the actual production preparation function and
checks its Task 006A synthetic encrypted-payload fingerprints unchanged.

## Evidence versus fixture assumptions

Request construction mirrors historical FastECU: API ID 7E0, RequestDownload,
128-byte B6/address/data, exit, start and BEEF kernel ID. The model removes the
historical address-only extra iteration mathematically; no live sequence is
changed. There is no real timing, waiting, retry or recovery implementation.

F6 is a **reported prefix**, not a complete demonstrated acceptance contract.
The exact reply bytes/lengths in `SyntheticProfile` are explicitly invented
fixture boundaries to exercise host error transitions. They are not a proposal
to apply those rules to an ECU. Negative/pending cases are synthetic fault inputs;
only the related 2009 STI's empty-block 7F B6 13 was reported in prior research.
The simulator's ready result is named `SimulatedReady`, not programming success.

Most importantly, `Request::fixtureToken` is **out-of-band fake bookkeeping**.
It is never included in transmitted bytes and is not provided by the actual
J2534/ECU protocol. Only the test driver can attach it to a reply. This lets tests
inject wrong/stale/duplicate ordering without pretending identical F6 responses
are distinguishable on real hardware. Replies without this fake correlation fail
closed. Constructing the model without an explicit synthetic profile issues no
requests and returns Unsupported. The model cannot justify lifting the gate.

All failures are terminal for that attempt. Failed/zero/excess-count submissions
never enter reply acceptance. Late positives cannot restore success; no automatic
resubmission exists. Cancellation prevents subsequent model requests, but cannot
undo a request that was already handed to a hypothetical transport.

## Reproduction

With Qt 5 Core/Test and a C++11 compiler, build outside the checkout:

```sh
mkdir -p /workspace/build-FastECU-kernel-offline
cd /workspace/build-FastECU-kernel-offline
. /workspace/fastecu-env/activate.sh  # prepared cloud SDK only
qmake /workspace/FastECU/tests/subaru/kernel_transfer.pro CONFIG+=release \
  QMAKE_LIBDIR+=/workspace/fastecu-env/lib
make -j4
./subaru-kernel-offline -o results.txt,txt -o results.xml,junitxml
```

For ASan/UBSan use a fresh directory, debug configuration and:

```sh
qmake /workspace/FastECU/tests/subaru/kernel_transfer.pro CONFIG+=debug CONFIG-=release \
  QMAKE_LIBDIR+=/workspace/fastecu-env/lib \
  'QMAKE_CXXFLAGS+=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  'QMAKE_LFLAGS+=-fsanitize=address,undefined'
make -j4
./subaru-kernel-offline -o results.txt,txt
```

The native Windows runner script includes this Qt Core-only target. Its existing
native Windows/Fusion GUI configuration is preserved for production safety tests.
No new native Windows result is implied by modifying a runner script; consult
the Task 006F report for executed results and platform limitations.

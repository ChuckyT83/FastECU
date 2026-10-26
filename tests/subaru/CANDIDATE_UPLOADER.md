# Task 006G offline upload candidate

`candidate_uploader.h` composes the Task 006F transfer engine into a complete,
finite **software-only candidate**: image/identity review, download, kernel
blocks, exit, start and readiness. It has no physical transport interface,
driver callback, erase/write hook or production caller. It cannot compile
without `FASTECU_OFFLINE_KERNEL_TESTS`, set by the dedicated test targets only.

Production `run()` now rejects write/test-write before connection setup,
including when a kernel was already answering. `upload_kernel()` remains gated.
No runtime setting or test-profile switch enables production programming.
Existing private write routines are exercised by the link-time fake serial
facade in the safety suite, not by the candidate orchestrator.

## What is established

Requests mirror historical FastECU CAN: API ID prefix 7E0, RequestDownload
`34 04 33 + BE24 address + BE24 size`, B6/address/128 prepared bytes, `37`,
`31 01 02 02 02`, and the existing BEEF kernel-ID request. Actual production
preparation supplies padding/checksum/encryption in the tests. No preparation
or security algorithm was copied or changed. Complete emitted bytes are compared
to a separate mock recording. Every block requires an accepted response before
address/progress advances. The 6,428-byte synthetic kernel yields 51 nonempty
blocks, without an extra address-only request.

The production `validSector` guard accepts only an exact start/length pair from
the selected existing memory map. Aligned slices inside large sectors, partial
sectors and ranges spanning sectors are rejected before erase submission.
This changes validation, not geometry or command bytes.

## Synthetic assumptions and unknowns

Exact response bytes/lengths in `SyntheticProfile` are **fixture assumptions**.
Reported F6 is not a complete target-applicable response specification. There
is no production/approved profile: omitting the synthetic profile returns
Unsupported without submission. Complete target acknowledgement, pending,
completion, startup and recovery contracts remain unresolved.

Each recording supplies an attempt ID and request token as **out-of-band fake
provenance**. These are not ECU fields or J2534 guarantees. New fixture attempts
must use distinct IDs. Missing/old-attempt/stale/duplicate provenance is rejected;
identical real F6 bytes cannot be correlated by inventing tokens.
Historical FFxxxxxx/BE24 encoding is the only candidate address window. Valid
host memory does not prove bootloader download permission.

`IdentityReview` compares explicitly supplied fixture metadata exactly. It does
not identify a BIN, parse private XML, authenticate an ECU, prove compatibility,
or establish EcuFlash checksum equivalence. It calls existing production
size/map/checksum preparation checks and rejects an unapplied correction.
It never changes the supplied BIN. Production ROM-identity/compatibility policy
remains incomplete; this candidate policy cannot enable it.

## Failure and review boundary

The recording must match complete requests and be consumed exactly. Failed,
zero or excess submissions, transport errors, finite timeouts, negative or
malformed/unrelated replies, ordering faults and cancellation stop the attempt.
No automatic resend or recovery exists. Missing recording entries cannot cause
unrecorded submissions. Raw transport status/replies remain available. Unexpected
trailing exchanges prevent a ready result.

Cancellation after the final acknowledgement still prevents candidate readiness.
It cannot undo a submitted operation or prove ECU state. Synthetic success yields
only `ReadyForOfflineReview`; `programmingAllowed()` is always false. No
destructive transition is implemented or claimed to simulate a validated flash.

## Production coverage and reproduction

`safety.cpp` invokes production `prepare_kernel`, `flash_block`, `write_mem`,
parsers and outcome reporting with the fake facade. New `candidate*` cases cover
orchestration; `sectorBounds` covers every mapped sector/invalid slices;
`programmingEntryGated` covers the UI entry. Earlier verification, cancellation,
UI and transport-error cases remain. Write-result UI tests now call the common
production reporting helper after a mocked private write operation because the
production write entry is deliberately blocked.

Build outside the checkout with Qt 5 Widgets/Test and project dependencies:

```sh
mkdir -p /workspace/build-subaru-offline
cd /workspace/build-subaru-offline
. /workspace/fastecu-env/activate.sh  # cloud SDK only
qmake /workspace/FastECU/tests/subaru/safety.pro CONFIG+=release \
  QMAKE_LIBDIR+=/workspace/fastecu-env/lib
make -j2
QT_QPA_PLATFORM=minimal QT_STYLE_OVERRIDE=Fusion ./subaru-safety \
  -o results.txt,txt -o results.xml,junitxml
```

The Windows runner includes these tests through `safety.pro`. Native Windows/
Fusion for GUI tests, offscreen for J2534 tests, Windows 2022, x64 MSVC, Qt 5.15.2,
pinned actions and log artifacts remain. Prepared CI is not an executed result.
Consult the external Task 006G report for actual counts and limitations.

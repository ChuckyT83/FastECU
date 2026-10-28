# SH7058 identification and immutable preflight (Task 006N)

No physical operation is authorized by this document. The separate identity
operation is ready for a later, explicitly approved OBDX trial. Upload, write,
test-write, live candidate authorization and voltage-based admission remain
DISABLED. ROM Read is not identification. No switch enables programming.

## Identity operation and evidence

The code-owned **SH7058 preflight → Read ECU identity (AA/CAL-ID)** action uses
an isolated native Windows x64 worker, not MainWindow's transport. It requires
an idle local GUI with a closed channel, selection of the exact registered
J2534 driver, the independently reviewed official OBDX DLL SHA-256, and explicit
confirmation of prior device-only validation and separately approved ECU setup.
The worker checks matching bitness, 04.04 API, local DLL/hash and positive OBDX
metadata; clone/OpenPort/Tactrix/unreviewed entries are refused. Descriptions do
not authenticate a device: the reviewed installer/hash and USB-only validation
remain necessary. No vendor DLL is loaded by ordinary startup or GUI smoke.

Only these application frames are admitted (four-byte CAN-ID prefix included;
ISO-TP segmentation/reassembly belongs to J2534):

| Request, 11-bit ISO15765 at 500 kbit/s | Exact response structure in the pinned Base.bin |
|---|---|
| `000007E0 AA` | `000007E8 EA A2 10 11` + five ECU-ID bytes + 96 further ROM capability bytes; 109 API bytes total |
| `000007E0 09 04` | `000007E8 49 04 01` + eight ASCII CAL-ID bytes + eight NULs; 23 API bytes total |

Read-only analysis of SHA-256
`15102939df167a4d76be9f33c20966fa4f707bb171aff846c7117108cc9c875c`
established the following protocol facts. Private firmware/disassembly and XML
are not distributed with this project:

- Service AA handler **0xAA170** requires zero parameter bytes. It builds EA and
  reads exactly 104 ROM bytes through the pointer table at **0xEC8C8**. All
  pointers resolve within **0xE10ED–0xE1154**; none reads RAM, EEPROM or MMIO.
  The first three bytes are A2 10 11; the next five are A6 2A 90 71 07. Only
  response-buffer/length writes and response helpers occur on this path.
- Service 09 handler **0xA9440**, PID table entry **0xF0508**, dispatches PID 04
  to **0xB72B2**. It writes record count 01, copies eight ROM bytes from
  **0xDFFE0–0xDFFE7**, and pads eight NULs. The response builder prefixes 49 04.
  The returned CAL-ID is **ZE5T201V**, NOT **ZE5T201V_DM24000**. No full-variant
  compatibility is inferred from that prefix or from parent AE5T201V.
- Application dispatcher **0xAA4AA** lists AA and 09 in **0xEF1C4**, both with
  availability mask 1. Its state byte **0xFFFFBB18** is initialized to zero
  (0xAA3D0/0xA80E6), selecting mask 1. It also has a restricted request-class
  table at 0xEF2D8 containing 09 but not AA. These tables are not silently
  labelled programming-session tables. Receive handling **0xAA932** accepts
  CAN 0x7E0/0x7DF; historical FastECU uses physical 0x7E0 and response 0x7E8.
  No session-change prerequisite or call to security/programming/reset/upload
  occurs in the reviewed identity handlers. No generic UDS assumption is used.

The first physical trial must confirm this static behavior on the actual ECU.
If unsupported/negative/malformed/late, STOP; no session fallback, seed/key,
BEEF probe, VIN request, reset or automatic retry exists on the identity path.
The EA parser requires the exact pinned length/preamble; the CAL-ID parser
requires one printable 16-byte record with trailing NUL padding only. Raw CAL-ID
and the full custom firmware label are separate fields.

## Ownership, failure and cleanup

`SH7058IdentificationSession` owns a fresh private SerialPortActionsDirect and
production J2534 loader for one lifetime. It sets ISO15765/500000/11-bit
0x7E0/0x7E8, disables loopback, installs the existing flow-control filter and
requires complete matching API metadata. It does not borrow the GUI channel,
start periodic traffic, sample/change voltage or enter any ECU session.
Submission rejects every frame except the two above. The worker has a Windows
session mutex; all other vendor/diagnostic applications must also be closed.

Acquisition, queue checks, submission and response deadlines are bounded by the
identity engine (2000 ms per acquisition/request). It checks exact API status
and counts, empty receive queue before each request, a nonzero unchanged
connection generation, cancellation and full response parsing. A late callback
cannot advance the sequence. Every acquired owner is released exactly once;
Disconnect/Close status survives backend state reset, including failure.
Generation retires on release; there is no reopen/retry in a worker.

The GUI waits at most 30 seconds in a responsive event loop while disabled and
with voltage polling stopped. A noncooperating vendor DLL cannot be preempted by
an API timeout. The worker is **not killed**, and no concurrent Close/unload is
issued on that timeout. The result records PID and late-report path; uncertain
ownership/cleanup latches further identity attempts off in that GUI. Review the
worker and its late evidence before any restart/retry; restarting the GUI alone
does not establish that a vendor worker exited. Normal reports are timestamped
JSON in an isolated temporary directory, contain driver ID/hash, exact returned
ECU/CAL-ID and checked cleanup status, and collect no VIN or calibration image.

The CLI equivalents are visible in `FastECU.exe --help`: `--sh7058-identify`,
`--identity-driver-sha256`, `--reviewed-obdx-driver`,
`--ecu-identification-authorized`, `--identity-output NEW.json`. These authorize
only the identity worker, never programming. They cannot be combined with GUI
smoke, remote or trace options. The packaged tests verify refusal before device
access when authorization is absent and when modes are mixed. Standalone Stage
A/B device-only diagnostic packages retain their narrower capabilities.

## Firmware-transition compatibility (Task 006Q)

**BIN/XML compatibility (offline)** reports four independent assessments in one
readable dialog. Expand Details for complete JSON, reasons and provenance. No
confirmation overrides a result and no action programs an ECU.

| Assessment | Results and scope |
|---|---|
| Hardware compatibility | Compatible with reviewed family, incompatible, or insufficient evidence. ROM-reported ECU identity is observed historically; physical MCU/layout are derived from the reviewed family, not independently measured. |
| Target BIN integrity | Valid, invalid, incomplete, or unsupported, independently of the installed firmware identifier. |
| Firmware transition | Same comparable identity, different comparable identity, partially known, or unknown. Source and target identifiers retain their types. |
| Flash-operation readiness | Sector requirements unknown, protected changes unsupported, programming disabled, or invalid comparison. Every result denies programming admission. |

The model recognizes stock-to-DimeMod, DimeMod-to-stock and updated tuner
identifiers within a reviewed family. A different identifier/hash is not itself
hardware incompatibility. No prefix/parent equivalence is inferred between
ZE5T201V, ZE5T201V_DM24000 and AE5T201V. The eight-character OBD response does
not prove the installed full custom variant or byte-identical calibration.
Missing full installed identity does not invalidate a separately verified target.
The target OBD CAL-ID is established only for the actually reviewed original
Base.bin; new targets do not inherit it by truncating their custom identifiers.

Target integrity requires:

- Exactly 1,048,576 bytes and unchanged original/current/file-recheck SHA-256.
  The historical Base.bin SHA remains informational, not a universal allowlist.
  A new valid target SHA is permitted. The GUI reopens the selected path to catch
  replacement as well as mutation; unreadable/changed targets fail.
- Exact supported sub_ecu_denso_sh7058_can/ISO15765/SH7058 configuration, unchanged
  physical 16-sector geometry, enabled checksum and RAM kernel address 0xFFFF3000.
- Production checksum evaluation on a copy returning Valid without mutation.
  Disabled/unsupported tables and proposed corrections fail appropriately; no
  correction, padding, truncation, identifier rewrite or image substitution.
- Complete selected XML metadata and actual inheritance, with the selected
  target's exact internal identifier at its own declared address. Stock may be
  the leaf; custom/tuner children may have different full identifiers. Missing,
  ambiguous/cyclic/duplicate definitions and DTD/entities remain rejected.
- Kernel SHA
  `5fb9cb8973c6a3746e0e6a3afa328e9db57c66dd8d227ab3872cd2f80946b82e`.

Independent family association currently covers the reviewed 2016 USDM STI
A62A907107/AE5T201V SH7058 layout. It requires the actual reviewed public Alpha
AE5T201V document fingerprint
`432328ab094160180fa0f4cffebc9528dd4a4dd432fe6d2634d4f6d69694e33a`,
as stock leaf or real ancestor, and matching ROM ECU-ID bytes at 0xE10F0 derived
from the reviewed Base.bin AA pointer table. Matching size, checksum, XML claims
or identifier prefixes alone cannot establish this association. Other target
layouts/documents remain unresolved pending specific evidence, not silently
accepted or classified as incompatible merely because a label changed.
The production references are compiled defaults; there is no GUI/CLI/custom
profile override. Synthetic tests inject their own review fingerprints into the
pure evaluator, never into the application or programming gate.

The reviewed stock XML omits filesize. Its 1 MiB requirement is explicitly derived
from the anchored SH7058 family; an unreviewed definition without size evidence
remains incomplete. Configured, definition-supplied, firmware-derived and observed
facts are separately labelled in the JSON. Hardware compatibility means matching
reviewed family evidence, not physical flashing qualification or authentication
of all target code. Historical observations never prove the current connection.

The owner reference remains byte-identical at SHA
`15102939df167a4d76be9f33c20966fa4f707bb171aff846c7117108cc9c875c`.
Use its private child XML plus public AE5T201V.xml and Bases/32BITBASE.xml from
TD-D/SubaruDefs Alpha commit
`0b0c0c42acea0d89ffd8b50378ae13ae32b5b43d`.
Chain: ZE5T201V_DM24000 → AE5T201V → 32BITBASE. No private firmware or XML tables
are distributed. Other stock/custom transition examples are synthetic tests,
not validation of stock or updated tuner binaries which were not supplied.

No current-image acquisition was added. The GUI therefore reports sector
requirements unknown. The pure evaluator can record a complete read-only byte
comparison only when given a qualified current-image capture with original hash
and the same unique observation context and generation. An offline reference or stale context/generation is
insufficient. No production producer for that capture exists in this task.
All 16 differences are reported, never filtered into an erase plan. Changes in
sectors 0..8 or 15 are unsupported under the 006N preservation review proposal;
other differences or no changes still give no programming admission. This adds
no erase/sector-selection policy or partial-programming success path.

SH7058 ROM Read remains rejected before the MainWindow reset/voltage/transport
prelude and at module run() entry. Upload, write, test-write, unknown legacy
commands, live candidate and voltage admission remain disabled. Other ECU
families, identity worker and device-only diagnostic toolkits are unchanged.

## Shortest later RAM-only qualification — NOT authorization

1. Complete reviewed USB-only OBDX Open/ReadVersion/Close (exact official DLL,
   API, firmware, timing/cleanup) and then a separately authorized AA/09-04 trial.
   Use the complete definition chain, record installed and target identities
   separately, and review transition uncertainty and applicable entry behavior.
   Do not treat the short CAL-ID as the full DM24000 label. Clone excluded.
2. Use a separately reviewed vehicle/supply setup and measured stable supply.
   Kernel response 0x02300230 is constant, not measurement. J2534 voltage never
   authorizes programming. No spare ECU or access to a proprietary developer is
   required for continuing software development.
3. A future uploader provider must own device/channel/receive queue exclusively,
   retire state on disconnect/uncertain transitions, and reject stale responses.
   Background logging, periodic traffic, voltage polling and remote access stop.
   Reopening/flushing alone cannot prove an old sequence-less F6 cannot arrive.
   Existing live candidate context stays **Unreviewed**, not this identity link.
4. Qualify ONE applicable programming branch; do not run both as experiments.
   Existing paths are `10 03 → 50 03`, `27 01 → 67 01 seed`, reviewed matching
   `27 02 key → 67 02`, `10 02 → 50 02`, or the separately evidenced 43/42
   variant. These are security/programming transitions, not identification.
   Task 006L and the target handler 0xA97AC establish numeric prerequisites:
   03 requires prior 01/03 and mode 0, 10<=X<14, Y<100, -40<=Z<215, W<1;
   02 requires prior 02/03, authorization=1 and X/Y checks; 43 requires prior
   01/43, mode 1 and X check; 42 requires prior 42/43, authorization=1 and X
   check. Physical meanings/units and the actual applicable branch must be
   qualified; these numbers do not become invented host supply thresholds.
5. Preserve exact candidate sequence: `34 04 33 FF 30 00 00 19 80 → 74 20 00 84`;
   exactly 51 nonempty `B6 + BE24 address + 128 prepared/encrypted bytes` at
   0xFFFF3000+128*i, i=0..50, each → exactly F6; `37 → 77`;
   `31 01 02 02 02 → 71 01 02 02`. API prefixes 000007E0/000007E8.
   No 52nd empty block or invented ECU acknowledgement sequence number.
6. Independently confirm kernel startup:
   `000007E0 BEEF000101000000` must return `000007E8 BEEF002D41` +
   `FastECU Subaru SH7058 iso15765 Kernel v1.00` + NUL.
   Then only capabilities `BEEF000105 → BEEF00054500000200` and
   `BEEF000106 → BEEF00054600001000` (same CAN prefixes). Stop there.
   This is responding protocol identity, not authenticated RAM bytes. No
   flash-enable, erase, buffer/validate/commit, EEPROM or arbitrary RAM reads.
7. Advance only after one checked submission and its full expected in-deadline
   response. On missing/late/malformed ACK, count/status error, disconnect,
   cancellation or generation loss, send nothing further and never resend.
   Missing 71 is not repaired by an automatic BEEF probe. Inherited read limits
   (2000 ms diagnostic, 500 ms capability) still need actual OBDX qualification.
8. Before approving programming entry, establish bounded interrupted-session
   exit/recovery behavior. Close releases J2534 resources but does not restore
   the ECU application. No reset, ignition/power cycle or proprietary recovery
   action is invented. This specific physical uncertainty remains unresolved.

## Focused bootloader-preservation policy proposal

Unchanged sector map: sectors 0..7 are 0x1000 bytes at 0x0000..0x7000;
sector 8 is **0x8000–0x1FFFF**; sectors 9..15 are 0x20000 bytes at
0x20000,0x40000,0x60000,0x80000,0xA0000,0xC0000,0xE0000.

The supplied image confirms reset PC **0xC0C**, SP **0xFFFFBFA0**, and low loader
code in **sector 0**. Reset initialization calls 0x65C, which can dispatch the
entry at **0x1000 = 0x127C**, entering the secondary loader in **sector 1**.
That loader contains the inherited download/transfer handlers at
0x1B3C/0x1FA0/0x1C6E/0x1D30. Its normal application path reads the pointer at
**0xFFFF8 = 0xF9B8**, then jumps via the stack-reset trampoline at 0x1008.
The target at 0xF9B8 performs application startup (SR/VBR/FPSCR initialization)
in **sector 8**. The companion descriptor at 0xFFFFC points to 0x2004.
Consequently 0x2004 is identity metadata, not the main application entry.
The final descriptors are in **sector 15**, which is also recovery-relevant.
This establishes specific critical regions, not the full closure of every loader
helper or an independently accessible OBD recovery mode.

Proposed first future write policy: compare complete current/target images by
exact existing sector boundaries; conservatively protect sectors **0..8 AND
15** pending a reviewed dependency/recovery analysis. Reject any intended change
in a protected sector; never silently skip it. Only changed whole eligible
sectors **9..14**, with all unchanged bytes preserved and independent post-write
verification, could be considered after separate qualification. If a calibration
or checksum update touches protected sector 15, this initial policy rejects it
until safe handling of its application-entry descriptors is established. This
is a concrete proposed qualification policy, not an implemented write mode.

No stock/tuned pair was supplied, so neither unchanged low flash nor the sectors
required for this owner's normal update can be claimed. Preserving low flash
alone cannot guarantee OBD recovery. Loader entry branches exist, but independent
physical recovery prerequisites and behavior after interruption remain unproven.
No DimeMod recovery software is recreated or bypassed.

The bundled kernel remains byte-identical. Its erase dispatcher initializes an
index to zero, searches the exact 16-entry address table, and fails to mark
"not found"; an unmatched address can therefore select sector 0. Exact matched
addresses select their actual indices. The host already rejects nonexact
sector starts/lengths, but this defect must be corrected/requalified before any
future erase. Changed-sector filtering is not a substitute for that work.

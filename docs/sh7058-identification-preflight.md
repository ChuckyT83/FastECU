# SH7058 identification and strict offline compatibility (Task 006N)

No physical operation is authorized by this document. Live identification is
unavailable pending target firmware evidence. Upload/write/test-write and the
live candidate binding remain disabled. ROM Read is not identification.

## Working interfaces

The code-owned **SH7058 preflight** menu exposes **ECU identification (live
unavailable)** and **BIN/XML compatibility (offline)**, including for existing
profiles whose menu.cfg predates these actions. Identification invokes the fixed unavailable
provider and returns before acquiring, configuring or opening a device. There
is no flag/environment variable to admit live identification or programming.
The offline preflight reads an original 1 MiB BIN, selected child XML plus all
parent XML documents, and the selected protocol's local kernel. It hashes the
original bytes, reparses inheritance, evaluates the existing checksum on a copy,
and rechecks the BIN on disk. It never edits a calibration, XML or kernel.

`subaru_sh7058_preflight.{h,cpp}` contains the isolated workflow, parser and
compatibility policy. An injected **offline fixture only** can exercise the
workflow's acquire/queue check/submit/read/release lifecycle. All production
transmission remains unavailable. Successful fixture identification is labelled
OfflineReplay; it cannot become an observed ECU identity. A compatibility result
is evidence reporting, never a token: `programmingAllowed()` is always false.

## Request and parser evidence

Historical production `connect_bootloader()` sends the following before its
session probes/security access. That routine also sends BEEF/VIN/CVN and enters
programming-related sessions, so it is not reused or called by identification.

| Candidate | J2534 application request | Parser |
|---|---|---|
| ECU ID | `00 00 07 E0 AA` | Address `00 00 07 E8`, EA, three preserved/unqualified preamble bytes, five exact ECU-ID bytes; retains the full response |
| CAL-ID | `00 00 07 E0 09 04` | Address `00 00 07 E8`, `49 04 01`, exactly one 16-byte printable identifier record; trailing NUL padding only, never prefixes/multiple records |

These are application bytes including the four-byte CAN-ID prefix, not raw
ISO-TP frames. Historical transport is 11-bit ISO15765 at 500 kbit/s, request
0x7E0/response 0x7E8. The new parser rejects truncated/wrong-address/negative,
pending and ambiguous multi-record replies and preserves exact identifiers/raw
responses. No VIN is requested. No session change, security, reset, BEEF, RAM
read/upload or flash command exists in this workflow.

**Not established:** the modified Base.bin's ordinary-session dispatcher for AA
and 09/04, exact EA preamble/capability meaning, any session prerequisite and
its effects, and whether that CAL-ID record actually equals the owner's full
firmware identifier. Base.bin, private XML and Task 006J–006L reports were absent
from this resumed workspace. The owner-provided SHA is a reference identity,
not a substitute for reading those handlers. No generic UDS/SSM service or 10/03
session is assumed safe. Recover the relevant ordinary-state dispatcher/handler
analysis for the exact image (or inspect it read-only) before adding a live
provider. A separately authorized initial identification trial must use only the
reviewed requests and no automatic session fallback.

The lifecycle rejects busy ownership, zero/changed generations, queued traffic,
unconfirmed API status, late success, timeout, disconnect, cancellation and
malformed replies. It releases only acquired ownership exactly once, reports
cleanup failure, and never retries/resets. A host deadline cannot preempt a
blocking vendor DLL. A future native provider must bind acquisition to exclusive
device/channel/receive-queue ownership and checked facade operations; these
capabilities are intentionally not fabricated by this task.

## Strict compatibility

The GUI currently has no complete observed ECU identity, so an otherwise exact
offline match reports **missing identity**, never compatible with an unqueried ECU.
The policy distinguishes compatible available evidence, confirmed incompatible,
missing evidence, unsupported configuration and further hardware qualification.
It checks:

- Exact 1,048,576-byte size and original/current/reference SHA-256. The owner's
  pinned reference is `15102939df167a4d76be9f33c20966fa4f707bb171aff846c7117108cc9c875c`.
  Other or edited BINs require a separately reviewed reference; filenames do not
  establish identity. No padding/truncation/substitution is performed.
- Exact selected `sub_ecu_denso_sh7058_can`, SH7058, enabled checksum, existing
  16-sector physical geometry and kernel address 0xFFFF3000. Aggregate/rearranged
  maps and other ECU families/variants are unsupported by this target policy.
- Existing production checksum evaluation must return Valid and preserve bytes.
  Disabled tables are unsupported. A proposed correction is reported and rejected
  for separate review, never applied to the original image.
- Selected XML child, actual inherited parents, no missing/cyclic/duplicate IDs,
  full internal ID at the XML's explicit hexadecimal BIN address, ECU ID,
  `subarucan`, `subarudbw`, SH7058 and exact size metadata (1048576 or 1024KB).
  AE5T201V must be an actual ancestor, not an alias for ZE5T201V_DM24000. The
  parser supports RomRaider `rom base` and a single EcuFlash `include`; unsupported
  inheritance is rejected, not guessed. No calibration tables are interpreted.
- Selected kernel SHA `5fb9cb8973c6a3746e0e6a3afa328e9db57c66dd8d227ab3872cd2f80946b82e`.
- Exact observed ECU A62A907107 and calibration ZE5T201V_DM24000; a known mismatch
  is rejected even if the other identifier is missing. Offline/inferred replay
  never supplies physical identity. A generation number alone authenticates
  neither a device nor an ECU. Hardware/kernel qualification remains separate.

The ECU-ID-to-BIN binding here is the owner's pinned reference and explicit XML
metadata; no unknown raw ECU-ID offset is invented. Full reference/calibration
hashes and XML document hashes are reported. No private BIN/XML content is
committed or automatically exported. The existing editor/checksum code is not
turned into programming admission by this report.

SH7058 ROM Read now fails at both MainWindow and module run() entry before
serial reset/opening, voltage sampling or `connect_bootloader`. Its private
implementation remains for later qualification. Unknown operation strings are also
rejected rather than falling through to bootloader entry. MainWindow rejects
write/test-write before its legacy reset/voltage prelude as well. Other ECU
families are unchanged.

## Shortest later RAM-only qualification plan — not authorization

1. Finish reviewed USB-only OBDX Open/ReadVersion/Close, including exact official
   driver hash/bitness/API/firmware, cleanup and timeout behavior. Separately
   qualify ordinary-state identity and record both exact identifiers; satisfy
   strict BIN/XML/kernel checks. Keep the clone outside this plan.
2. With separately approved vehicle setup, verify actual supply stability using
   the appropriate measured source and reviewed vehicle procedure. The kernel's
   0x02300230 voltage response is a constant, not supply qualification. A driver
   voltage sample does not grant programming permission.
3. Add a reviewed provider with one exclusive owner across device, channel and
   receive queue. Stop background logging, voltage polling, periodic senders,
   alternate readers and remote access. Bind generation to the acquired session;
   retire it on every exit/disconnect/uncertain request. Reopening/flushing alone
   does not prove that old F6 responses cannot arrive. Keep the current live
   candidate `Unreviewed` until this separate implementation/authorization.
4. Resolve the exact target's programming entry first. Historical code probes
   `10 03`/`10 43`, expects exactly one family, uses `27 01`/`67 01 seed`, then
   `27 02 key`/`67 02`, followed by `10 02` or `10 42`/matching `50` response.
   These are programming/security transitions, NOT safe identification. The
   exact modified-target branch/effects must be recovered from the missing
   firmware evidence; do not run both probes as a qualification experiment or
   invent an ordinary-session equivalence. No entry sequence is newly enabled.
5. Once separately admitted, retain the existing exact candidate sequence:
   `34 04 33 FF 30 00 00 19 80` -> `74 20 00 84`; then exactly 51 nonempty
   `B6 + BE24 address + 128 existing prepared/encrypted bytes` requests at
   0xFFFF3000 + 128*i (i=0..50), each -> exactly `F6`; `37` -> `77`;
   `31 01 02 02 02` -> `71 01 02 02`. All use API prefix 000007E0 and complete
   responses 000007E8. No 52nd empty transfer or invented ECU sequence number.
6. Confirm independent kernel startup using existing
   `000007E0 BEEF000101000000`; require exact bundled-kernel ID response
   `000007E8 BEEF002D41` + `FastECU Subaru SH7058 iso15765 Kernel v1.00` + NUL.
   This confirms a responding protocol/string, not authenticated RAM bytes.
   Then the shortest additional non-destructive check is capabilities:
   `000007E0 BEEF000105` -> `000007E8 BEEF00054500000200` (512),
   `000007E0 BEEF000106` -> `000007E8 BEEF00054600001000` (4096).
   Stop there. No flash-enable/disable, buffer/validate/commit/erase, EEPROM,
   security/reset or arbitrary RAM reads are part of this initial kernel test.
7. Require one successful checked submission and one complete expected response
   per stage before advancing, within the established host/API deadlines. The
   historical module defaults are 2000 ms for diagnostic reads and 500 ms for
   capability reads; these are inherited settings, not measured OBDX timing
   qualification, and are not changed here. On
   missing/late/malformed ACK, count/status error, cancellation or generation
   loss, submit no next command and never resend. Preserve raw outcome and
   acquired-resource cleanup state. Missing 71 is not repaired by an automatic
   BEEF probe: that fallback remains an offline experiment only.
8. Before authorizing step 4, establish the target's interrupted-session exit
   behavior and bounded observation/cleanup plan. Closing J2534 does not exit
   ECU programming state. No reset, ignition/power cycle or proprietary recovery
   behavior is invented. If the transition becomes uncertain, stop and follow
   that reviewed plan. Its exact behavior is a specific remaining evidence gap,
   not a requirement to buy a spare ECU before continuing software development.

## Focused bootloader-preservation proposal

The unchanged existing physical map has sectors 0..7 of 0x1000 bytes starting
0x0000..0x7000; sector 8 is 0x8000..0x1FFFF; sectors 9..15 are 0x20000-byte
sectors at 0x20000, 0x40000, 0x60000, 0x80000, 0xA0000, 0xC0000, 0xE0000.
Reset vectors are in low flash (sector 0). The committed firmware-derived
candidate cites target download handlers at 0x1B3C/0x1FA0 and transfer handlers
0x1C6E/0x1D30, within sector 1. These are inherited analysis provenance, not a
new disassembly of unavailable Base.bin. Loader extent and normal application
entry are not established by this checkout. Protocol CAL-ID address 0x2004 is
metadata, not proof that the application starts at 0x2000.

Proposed later policy: compare complete original/target images sector-by-sector;
conservatively protect sectors 0..8 (low 128 KiB) until exact loader boundaries
are verified. Require those regions byte-identical; reject, rather than silently
omit, any intended protected-region change. Admit only verified changed whole
non-protected sectors after all other qualification, retaining unchanged sector
bytes, post-write CRC verification and the existing geometry. This is a proposed
policy only; it does not enable writes or assert that all bootloader bytes are
inside the provisional protected region.

There is only one owner-reference SHA here and no original/modified image pair,
so no unchanged-low-flash comparison or calibration-only sector claim can be
made. Whether this tune requires bootloader sectors remains unknown. The known
kernel erase-address defect must also be resolved/qualified before ANY erase:
host changed-sector filtering cannot guarantee where a defective kernel erases.
The kernel binary is unchanged. Preserving low flash does not prove independent
OBD recovery. No independent recovery entry, interrupted-session reset behavior
or proprietary DimeMod recovery mechanism is established or recreated.

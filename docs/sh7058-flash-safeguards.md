# SH7058 post-kernel safeguards (Task 006M)

Live uploading, write and test-write remain blocked at their existing production
entry points. The candidate's real J2534 binding remains Unreviewed. No runtime
configuration or diagnostic voltage sample grants programming permission.

The private flash workflow now requires the existing 16-sector map, matching the
pinned kernel's erase-address table. An aggregate one-block image/compare map is
not a physical erase plan. Sector definitions and kernel bytes are unchanged.

The pinned kernel reports 512 data bytes per buffer transfer and a 4096-byte
programming buffer. Different capabilities stop before enable/erase. Its command
22 appends into that rolling buffer and ignores its address field. Command 23
checks RAM CRC; command 24 programs the fixed 4096 bytes and then checks flash
CRC. The host therefore requires a valid 23 acknowledgement before each real 24.
Protected test-write submits 23 only, and switching from a cached write mode
requires reinitializing protection. A protected validation is not hardware
qualification or permission to perform an actual flash.

Kernel voltage body `44 02 30 02 30` is accepted as four data bytes and explicitly
labelled a constant, not an electrical preflight. The additive checked
`read_vbatt_checked()` API preserves the actual IOCTL status and reports raw
millivolts, host monotonic observation interval and a retiring device-context
identifier. It does not open a device. Remote mode is unsupported. Failed refresh
or closed context invalidates older observations. Assessment requires explicit
caller-supplied age/range bounds; it never authorizes programming. Sensor-cache
freshness and physical accuracy remain unknown even after a fresh driver call.
No vehicle-specific voltage threshold or automatic polling was added.

For this SH7058 module, checksum preparation must preserve the supplied BIN.
A proposed correction is rejected for separate review, rather than silently
replacing the working image. The SH7058-only endpoint policy accepts aligned exclusive or last-byte upper
bounds and checks the full final word access. Other modules retain the existing
aligned-only default. Checksum arithmetic is unchanged. A valid
or disabled checksum result does not establish equivalence with EcuFlash's
subarudbw module or prove XML/ECU compatibility.

The production-function tests use a stateful, synthetic flash/RAM model derived
from the exact kernel's static command contract. It is not SH instruction/MMIO
execution or physical flash evidence. They cover complete/private workflows,
protection switching, buffer corruption, partial programming, lost acknowledgements,
final CRC mismatch, cancellation and all inherited entry gates. A successful driver read returned past the requested host deadline is rejected
with its raw bytes/status retained. No timeout constant or driver argument is
changed. No automatic
retry/reset/recovery is implemented after uncertain destructive operations.

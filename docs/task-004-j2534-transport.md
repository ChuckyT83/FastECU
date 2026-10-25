# Task 004 — Generic J2534 transport and Windows discovery

## Inspection before implementation

Checkout: `https://github.com/ChuckyT83/FastECU.git`, clean at
`d4e44cdb4396ba070f563b2dd7ab4c11000294f3` on
`fix/j2534-initialization-reliability`. Task 003 and 003B source/tests/workflow are
present; tested commit `f1a6abd878a0ed78a83dd2decabdb78cae4970ce` is an ancestor.
Task 004 branches from that development history as
`fix/j2534-transport-reliability`, not from master.

Observed defects before changing production code:

- Direct `write_j2534_data` ignores WriteMsgs status and output count, discards
  bytes after failed/zero transfers, and unconditionally reports success. It
  splits buffers at 4128 bytes, including CAN/ISO15765, without validating minima.
  Public QByteArray write/echo methods discard the integer result and return a
  null-pointer conversion of STATUS_SUCCESS (empty bytes), indistinguishable from
  failure. Existing higher-level callers often ignore their return values.
- Direct `read_j2534_data` treats every nonzero status as empty, does not validate
  message count/length/ExtraDataIndex, and performs up to two follow-up reads with
  stale count/status and a new full timeout each. TX_DONE/START_OF_MESSAGE may
  therefore expose uninitialized/stale bytes or exceed the caller's timeout.
- Periodic start/stop ignore errors; repeated/chunked starts overwrite the only
  remembered message ID, so earlier schedules cannot be stopped. Zero/negative
  intervals and invalid lengths are not checked.
- Set-config, battery, programming-voltage and init helpers lose status or ignore
  failure. Five-baud uses a shared 20-byte input/output buffer, copies unbounded
  input while declaring one byte, declares zero output capacity and trusts the
  returned output pointer/count. Fast-init copies unbounded payloads. Generic
  serial receive parsing reads header bytes before checking availability.
- Windows discovery selects Registry32Format even on x64 because _WIN32 is also
  defined there. Missing/empty/non-string registry values are admitted. DLL
  selection prefers a same-named CWD library over the registered path. Load errors
  retain Windows status but give little bitness context. Win32 wrapper write debug
  dumping trusts a driver-reported count and formats unbounded data.

Entry points reviewed: direct/public read_serial_data and read_serial_obd_data,
write_serial_data and write_serial_data_echo_check, private write/read_j2534_data,
periodic start/stop, clear RX/TX, set_j2534_ioctl, read_vbatt, five_baud_init,
fast_init, set/pulse/line-end voltage helpers, getAllJ2534DriversNames,
open_serial_port, and the Windows J2534 forwarding/debug wrapper. The facade
`SerialPortActions` passes through the QByteArray/int results; remote replica
interfaces cannot convey a richer result without a separate interface change.

Implementation and validated results will be appended below. No physical hardware
or ECU programming operations are permitted in this task.

## Implementation decisions

- Submit one checked J2534 message per write, preserve its bytes/flags and the
  existing 100 ms API timeout, validate returned count/status and do not retry an
  uncertain write. Empty, address-short and oversized messages fail before API
  submission. The former arbitrary >4128-byte splitting is removed rather than
  inventing protocol fragmentation; callers requiring larger transfers need a
  reviewed message-boundary design, not implicit extra frames.
- Reads reset the message/count on each call, validate capacity/count/extra-data
  bounds and distinguish Success, NoMessage, Partial and Error with raw status on
  the direct transport. A valid completed message reported alongside timeout/empty
  status is returned with Partial outcome. Other driver errors discard data. API
  notifications are skipped, at most three reads share one monotonic deadline.
  ExtraDataIndex is validated but data is not stripped or reframed.
- The legacy write QByteArray remains empty on success; failure now returns one
  binary status byte 0x01. The facade already passes this through. Existing ECU
  callers ignoring results are not changed. Empty read bytes alone remain
  insufficient to distinguish timeout/failure across the remote interface; a
  future explicit result/remote-signal contract requires review. No error bytes
  are inserted into received ECU payload.
- Periodic scheduling tracks one owned ID, including zero. Invalid lengths and
  nonpositive intervals fail, a second active start is rejected, stop failure
  retains ownership for retry, and disconnect clears it. No interval is changed.
- Runtime invalid-channel/device errors unset local readiness and prevent further
  channel operations. Error diagnostics preserve driver status. A generic driver
  error does not automatically replay a write or destroy a still-valid channel.
- Five-baud uses one address byte and separate capacity-declared output storage;
  returned count/pointer are checked. Fast-init rejects oversized buffers before
  copying or delay. Incomplete serial headers are checked before indexing.
- Voltage setters and pulse helpers propagate failure while retaining existing
  pins, levels, enable/delay/disable order and disable attempts. There is no new
  physical voltage operation, retry or timing strategy. Battery's unsigned return
  and generic-serial echo result limitations remain documented for interface
  review rather than changing ECU programming routines.
- Windows discovery selects the process-matching registry view, ignores malformed
  FunctionLibrary values, expands environment references, and preserves absolute
  existing registered paths. It no longer substitutes a same-named CWD DLL.
  Loader errors include Windows status and process bitness. Native tests use
  isolated HKCU test keys; they never enumerate installed vendor devices.
- Windows debug dumping is bounded and validates returned write counts before
  indexing. Export names and PT_CALL remain unchanged. LLP64 is asserted in the
  native loader test; an OS-supplied 32-bit DLL is an offline incompatible-image
  fixture for the 64-bit loader, not a substitute for testing vendor DLLs/x86 ABI.

Results are pending execution of the expanded native workflow.

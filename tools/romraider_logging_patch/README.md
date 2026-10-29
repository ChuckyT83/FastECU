# Separate RomRaider 1.1.0 logging-integrity candidate

This GPL-compatible patch targets only the pinned 1.1.0-NOV26-2025 release JAR
and three normalized source files. build.py refuses mismatching inputs and always
writes to a separate directory, preserving the original release/installation.
It does not modify upstream, logger definitions, ECU firmware or J2534 registration.

Repairs: exact connection-response length before copying; returned count/metadata
validation and bounded message arrays; SSM read opcode and exact address-byte
count checks before assigning parameter responses. One narrowly scoped setup
accommodation permits unsupported TX purge only for 3.0.0-logging.1 before any
application message has been sent on a fresh channel. Active-channel purge errors
remain errors. This is required because no verified adapter TX-purge command is
available; the driver no longer falsely claims to perform one.

    python build.py --romraider C:\reviewed\RomRaider --output C:\candidates\logging

Requires a JDK >=9 (build uses --release 8). Output includes the separate candidate
JAR, applied source, build/source hashes and offline regression results. The test
substitutes the J2534 interface/native binding with Java mocks; it never registers
a vendor library or opens hardware. The JNA runtime bridge itself is used for
mock structure marshalling. The test checks counts, metadata, exact/short/long
responses, unchanged destination on mismatch, explicit four-address SSM requests,
byte assignment, duplicate addresses, response opcode, AVCS/OCV conversion and
fresh-channel-only purge accommodation.

This is not an official RomRaider release. Review both the driver and patched JAR
as a matched candidate pair before a separately authorized physical logging test.
No original DLL/JAR or settings should be overwritten. No physical test is part
of these build scripts. Same-length replies from a prior request cannot be tied
to addresses cryptographically: SSM does not echo an address list/transaction ID.
A raw USB/JNA capture and independent passive CAN evidence remain the way to
establish actual ECU-to-display accuracy. No claim about mechanical cam timing
or the cause of P0016/P0018 follows from these fixes.

# Maintained OpenPort derivative — logging-integrity candidate

Based on NikolaKozina/j2534 revision
22ae8e85aa421dd74b2a3678bd9bf0f0db8ff55a and the preserved USBQUAL/1 lifecycle
corrections. Source/dependency bytes are pinned in source-revision.json.
This component remains separate from FastECU Subaru production protocols/gates.

Version 3.0.0-logging.1 adds incremental USB record parsing, complete-message-only
bounded FIFO delivery, exact lengths/copy bounds, finite total read/write waits,
partial-transfer counts, channel validation and first-error preservation.
Only whole framed command replies satisfy command waits; binary messages received
during control commands are retained. Calls are mutually exclusive without
unbounded lock waiting, and GetLastError is thread-local and available during I/O.
The existing unbuffered LOG_ENABLE capture and lifecycle acknowledgments remain.
No updater, bootloader, programming or new persistent command is introduced.

A receive timeout with an unfinished message/request, malformed stream, queue
exhaustion or partial write faults the channel. Subsequent payload operations
fail rather than replay partial/stale data. The caller must close/reopen explicitly;
there is no automatic retry/reconnect or ECU recovery command. Zero-time polling
uses a bounded 1 ms USB wait rather than libusb's infinite timeout 0. RX clearing
is a bounded drain to a quiet host-USB boundary, not a guarantee about future
adapter/ECU responses. A faulted/in-flight request cannot be cleared into success.
The adapter protocol has no transaction identifier: same-length late SSM replies
cannot be authenticated to addresses by this driver. Extended-address USB record
semantics remain unqualified and are explicitly refused.

CLEAR_TX_BUFFER returns ERR_NOT_SUPPORTED: the driver has no verified adapter TX
purge command. The separately supplied RomRaider 1.1.0 patch tolerates this only
for this exact driver version on a fresh channel that has sent no application
messages. Unmodified RomRaider may fail its connection setup at that IOCTL.
Other clients must handle this unsupported capability; FastECU production gates
are not weakened. Firmware identity and real CAN/ISO-TP behavior remain untested.

Build with MinGW x64 and the extracted official libusb 1.0.29 Windows archive:

    python build.py --cc gcc --libusb C:\deps\libusb --output C:\build\driver --native-mocks

--native-mocks executes 22 existing lifecycle cases and 18 actual-driver logging
cases with all USB functions replaced by the existing fake harness. It never
loads the real DLL or opens an adapter. Tests cover every split point/byte-wise
records, coalescing/notifications, timeout bytes, partial-message quarantine,
size/queue bounds, write counts, filter validation, control routing, clearing,
concurrent calls/thread-local errors, 10,000 changing replies and 10,000 malformed
inputs. Native Linux CI also runs ASan/UBSan/leak detection. These software checks
do not qualify a real clone, adapter firmware, vehicle logging or flashing.

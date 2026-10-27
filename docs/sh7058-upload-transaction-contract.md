# SH7058 upload candidate: transaction ownership

This contract describes the gated, offline-tested candidate. It grants no live
communication or programming permission. The concrete J2534 binding remains
`Unreviewed`, generation zero, with no runtime enable switch.

## Required context

One owner must exclusively control the channel, its receive queue and its entire
loader-session lifetime. UI busy flags alone do not provide that exclusion.
A reviewed provider must have established a fresh loader session without an
unresolved previous request, periodic sender, background metadata poll, alternate
reader, remote call or earlier timed-out transfer. It supplies a nonzero local
session generation, changes it on channel replacement/loss, and retires the lease
on every eligible attempt's exit. A new channel handle alone does not establish
a fresh ECU-side session. Reopening, flushing and quiet time do not prove that an
old F6 cannot subsequently arrive. Recovery after an uncertain request requires a
separately reviewed session reestablishment procedure; none is implemented here.

## Stop and wait

- Submit one request at a time through checked J2534 operations; success requires
  the correct write count and API status. Submission success does not prove ECU
  acceptance. Never retry an uncertain block.
- Before submission, reject queued messages or queue-read errors. An empty queue
  is a contamination check, not a freshness proof.
- Preserve the established CAN IDs, protocol, filters, payload, API timeout and
  command order. ISO15765 messages are application data with a CAN-ID prefix,
  not raw CAN/ISO-TP frames. Flow-control and transmit notifications are not ECU
  application acknowledgements.
- Accept only a complete application response from 0x7E8 matching the outstanding
  stage. B6's complete positive response is exactly F6, with no correlation field.
  Ownership comes from the serialized context, not invented bytes. A delayed or
  duplicated F6 within that context cannot be distinguished by payload alone.
- The host starts a monotonic response budget after successful submission. A
  successful read returned at/after that deadline cannot acknowledge the request.
  The original API status and bytes remain in the report. This check cannot
  preempt a driver DLL that blocks indefinitely; driver blocking is unqualified.
- Any failed/uncertain write, missing/malformed/unrelated response, unexpected
  queued traffic, changed generation, cancellation or ownership loss stops the
  transfer. No following block/exit/start is submitted. Retire the lease rather
  than starting another candidate in the same context.
- The provider's context/generation/retirement callbacks are noexcept; it must
  provide exclusive access throughout run(). The fence is not a general thread
  synchronization primitive and does not authenticate received traffic.

## Startup and readiness

Startup submission, complete 71 acknowledgement and a responding BEEF protocol
are separate evidence. Preserve the original startup result even if a later ID
responds. The controlled offline fixture can exercise one BEEF probe after a
normal missing-71 timeout, yielding a distinct review-required result. A late
successful 71 beyond the deadline, transport failure or lost ownership does not
authorize that probe. No live fallback is enabled. Missing BEEF after acknowledged
startup remains uncertain; BEEF after missing 71 does not prove delivery of 71,
authenticate a kernel binary, or authorize erase/write/test-write.

## Qualification still needed

J2534 API success/count, ISO-TP handling and filtering do not guarantee ECU
application correlation. Actual driver timeout behavior, receive delivery order,
notification handling, close/reconnect queue lifetime and outstanding traffic must
be qualified for the installed OBDX driver/firmware. The device-only diagnostic
utility should be used before any separately authorized ECU interaction. No
arbitrary flush, delay, voltage threshold or automatic recovery has been added.

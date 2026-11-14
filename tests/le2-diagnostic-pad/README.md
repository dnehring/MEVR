<!-- SPDX-License-Identifier: MIT -->
# Bounded diagnostic pad source checks

The pure source reserves one request. Publication starts a fixed 5000 ms
activation window, while `expiresMs` remains zero. The first eligible,
nonconflicting pad-0 poll strictly before that deadline starts the requested
hold (at most 1000 ms). Later active polls do not renew the hold.

At or after the activation deadline, an unstarted request emits neutral only
and records `StartTimedOut`. An active hold reaching its own deadline records
`Expired`. Cancellation, a physical conflict/error or clock regression prevents
activation and preserves required neutral drain. The first safe neutral output
starts a fixed 250 ms window; every safe eligible poll during the window receives
the same neutral packet. `DrainPending` remains outstanding until a safe final
neutral output at or after the deadline. New requests cannot replace this drain.
Elapsed wall time alone cannot claim release or gameplay.
`neutralFirstMs`, `neutralLastMs` and `neutralDeadlineMs` expose the window;
`neutralMs` remains the latest output time. A clock regression cannot complete
the window or move its deadline earlier. Near the clock's representable limit,
the deadline saturates instead of wrapping; only that artificial boundary can
shorten the representable window. Physical conflicts/errors remain passthrough
and cannot count as neutral output, even after the deadline.
Publication reserves integer headroom for the complete activation window plus
the requested hold. No queue, automatic rearm or shared clock reset is added.

The suite covers the observed 312 ms / 150 ms and 1422 ms / 1000 ms scheduling
delays with synthetic times, last admissible activation, timeout equality,
active expiry, overflow boundaries, physical conflicts, cancellation, packet
identity and concurrent snapshots/polls. The existing-bridge suite repeats the
two scheduling cases through the real bridge using stubbed platform services.
It also models separate caller/export buffers and a disconnected original that
leaves buffers untouched: a discovery poll cannot exhaust the single release
opportunity before the gameplay caller polls. This is a bounded delivery window,
not a guarantee that any particular engine consumer processed a release.
These checks establish output policy, not actual game consumption or effects.

```powershell
cmake -S tests/le2-diagnostic-pad -B local-tools/pad-tests
cmake --build local-tools/pad-tests --config Release
ctest --test-dir local-tools/pad-tests -C Release --output-on-failure
cmake --build local-tools/pad-tests --config Debug
ctest --test-dir local-tools/pad-tests -C Debug --output-on-failure
```

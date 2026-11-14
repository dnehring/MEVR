<!-- SPDX-License-Identifier: MIT -->
# Bounded diagnostic pad source checks

The pure source reserves one request. Publication starts a fixed 5000 ms
activation window, while `expiresMs` remains zero. The first eligible,
nonconflicting pad-0 poll strictly before that deadline starts the requested
hold (at most 1000 ms). Later active polls do not renew the hold.

At or after the activation deadline, an unstarted request emits neutral only
and records `StartTimedOut`. An active hold reaching its own deadline records
`Expired`. Cancellation, a physical conflict/error or clock regression prevents
activation and preserves required neutral drain. Only a safe eligible poll can
complete that drain; elapsed wall time alone cannot claim release or gameplay.
Publication reserves integer headroom for the complete activation window plus
the requested hold. No queue, automatic rearm or shared clock reset is added.

The suite covers the observed 312 ms / 150 ms and 1422 ms / 1000 ms scheduling
delays with synthetic times, last admissible activation, timeout equality,
active expiry, overflow boundaries, physical conflicts, cancellation, packet
identity and concurrent snapshots/polls. The existing-bridge suite repeats the
two scheduling cases through the real bridge using stubbed platform services.
These checks establish output policy, not actual game consumption or effects.

```powershell
cmake -S tests/le2-diagnostic-pad -B local-tools/pad-tests
cmake --build local-tools/pad-tests --config Release
ctest --test-dir local-tools/pad-tests -C Release --output-on-failure
cmake --build local-tools/pad-tests --config Debug
ctest --test-dir local-tools/pad-tests -C Debug --output-on-failure
```

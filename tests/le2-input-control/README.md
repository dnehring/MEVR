<!-- SPDX-License-Identifier: MIT -->
# LE2 input control orchestration checks

This target compiles the actual production `Le2InputControl.inc` with fake bridge
and context services. The fake bridge delegates lifetime and release behavior to
the real `DiagnosticPad::Source`. It does not install hooks or load an engine.

Checks cover explicit installation, default/party category admission, context
replacement during installation and subsequent ticks, deadline refresh after
installation, pending-release refusal, monotonic expiry/regression/overflow,
physical conflict, fresh explicit runs, disable/destruction, and honest telemetry.
An expired UTC request cannot publish. UTC is an admission deadline; the pulse's
bounded hold uses the bridge's independent monotonic clock.

```powershell
cmake -S tests/le2-input-control -B local-tools/input-control-tests
cmake --build local-tools/input-control-tests --config Release
ctest --test-dir local-tools/input-control-tests -C Release --output-on-failure
cmake --build local-tools/input-control-tests --config Debug
ctest --test-dir local-tools/input-control-tests -C Debug --output-on-failure
```

The sampler and bridge are service boundaries here. Their own SDK and real-bridge
stub suites test guarded reads, caller provenance, hooks and callback races.
These checks do not prove game consumption, movement, VR routing, endpoint disk
ownership validation, or real XInput delivery. No game or native process starts.

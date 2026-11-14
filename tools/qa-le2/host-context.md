<!-- SPDX-License-Identifier: MIT -->
# Native lifecycle host-context guard

`session.py` refuses native `prepare`, `request-load`, `bind-process`, and
`reset` from a known sandbox-scoped or ambiguous Windows context. The observed
failure was a redirected `LOCALAPPDATA` under
`AppData/Local/Packages/sandbox.{GUID}/AC` and an empty shell inventory while a
desktop game remained active. Such an inventory must not authorize staging,
restoration, or ownership of a separate sandbox lock directory.

Before default-provider lifecycle work, the runner rejects that package path,
an AppContainer token, or a token with restricting SIDs. It compares environment
`LOCALAPPDATA` with Windows `FOLDERID_LocalAppData` using
`KF_FLAG_NO_PACKAGE_REDIRECTION`. Errors and mismatches refuse the operation.
The process-inventory child must return a valid JSON envelope identifying the
same LocalAppData root; missing output is an error, not an empty inventory.
Existing runs must identify `MEVR/qa-cafeteria.lock` beneath this verified root.
No folder creation flag is used by the Windows lookup.

On refusal, use the approved desktop host context and review the existing run
and its ownership before continuing. The tool does not repair environment
variables, elevate, move locks, migrate a run, or restore through a different
coordination root. A run originally created under another root requires explicit
recovery review; these checks deliberately do not make that recovery decision.

## API for native adapters

- `require_native_host()` returns the verified LocalAppData `Path` or raises
  `SessionError`. This reads token/folder evidence only; it neither inventories
  processes nor writes files.
- `require_native_context(context)` verifies the frozen `lockPath` against that
  host and returns its `MEVR` coordination directory. Call after `load_context`
  and before native actions. Existing lock ownership checks still apply.
- `default_locks()` and `windows_processes()` retain their signatures and call
  the host guard. A successful empty process list still requires ownership
  coordination before acting.
- The Python API's explicitly injected fake process provider and private lock
  directory remain available for isolated offline tests. There is no CLI bypass.
  Never use fake providers or wrapper functions to perform real lifecycle work.

## Limits and verification

This is a narrow fail-closed check for detectable host-scope problems, not a
proof of complete process visibility, interactive desktop identity, authorization,
or absence of a running game. `IsTokenRestricted` checks restricting SIDs; it
does not identify every possible restriction, container, VM, session separation,
or process-hiding mechanism. Existing live ownership and game-slot checks remain
necessary. An explicit custom process provider is trusted test injection, not a
security boundary against arbitrary Python callers.

`python tools/qa-le2/test-session.py` covers the observed redirected-path failure
before probing or writes, token/folder ambiguity, malformed and mismatched child
inventories, wrong frozen coordination roots, and all four fake-provider lifecycle
operations under a simulated sandbox environment. Tests use temporary fake
installations. The Windows API helper was also read-only checked on the current
desktop host; this did not run a game or establish native acceptance.

Windows API references:
[known-folder flags](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-known_folder_flag),
[folder lookup](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath),
[token information](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class),
[restricted-token limits](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-istokenrestricted).

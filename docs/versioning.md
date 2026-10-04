# Versioning

Every winject component reports a version `vX.Y.Z`.

| Part | Meaning | Bump when |
|------|---------|-----------|
| `X.Y` | **Protocol version.** Shared by manager and all radios. Covers every winject wire format: radio m-plane, radio d-plane, manager m-plane | Any observable wire change: command, key, reply, error code, or d-plane format. Bump `X` for a redesign, `Y` for anything else |
| `Z` | **Implementation patch** (per component) | Bug fixes, performance, logging, or docs that leave the wire unchanged |

Two components are **compatible exactly when their `X.Y` are equal.** `Z` is never compared.

## This repo

- **Source of truth:** `project(winject-radio-realtek VERSION …)` in the top-level `CMakeLists.txt`.
- **Generated header:** `build/include/Version.h` defines `WINJECT_VERSION_STRING` (for example `v1.0.0`).
- **Runtime:** `version` on the m-plane (`RealtekDeviceBackend::version()`).

## Version discovery (frozen)

The exchange used to learn a peer's protocol never changes, even when `X.Y` bumps.

| Frozen | Exact form |
|--------|------------|
| Transport | UDP/IPv4; one request per datagram; replies to the request's source address and port |
| Request | `version` (alias `ver`), no arguments, optionally prefixed `cmd:<u8> ` |
| Reply | Starts with `OK version` (or `OK:<u8> version` when tagged), then `key=value` tokens. `ver=vX.Y.Z` and `proto=X.Y` are always present; ignore unknown keys |
| Correlation | `cmd:<u8>` / `OK:<u8>` / `NOK:<u8>` tags |
| Unknown command | `NOK ENOSYS` (`NOK:<u8> ENOSYS` when tagged). Treat as pre-`v1.0` and incompatible |

Procedure for a client (for example winject-manager):

1. Send `version` as the **first** request.
2. `OK version …` → compare `proto=` with the client's own `X.Y`. Equal → compatible. Different → incompatible.
3. `NOK ENOSYS` → incompatible (no `version` command).
4. No reply / timeout → unknown; retry. Never assume compatible.

## Release checklist

1. Wire change → bump `X.Y` in **all three** repos together and reset `Z` to 0. Confirm nothing frozen changed.
2. Otherwise bump `Z` in this repo only.
3. Tag `vX.Y.Z` in git.

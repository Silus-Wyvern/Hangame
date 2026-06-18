# Hangame-mode ASI

A standalone ASI that puts psobb.exe (TethVer12513) into Hangame login mode,
driven by the [psobb.io launcher](https://github.com/Silus-Wyvern/psobb.io-launcher).
Loaded by patch.dll's built-in ASI loader.

## What this is

Hangame login mode can't use the launcher's normal registry credential path.
Per [newserv #401](https://github.com/fuzziqersoftware/newserv/issues/401), in
Hangame mode the client doesn't read credentials from the registry, and it
won't connect past the patch screen unless a client-side version check is
bypassed. This ASI applies the required in-memory patches; the launcher hands
it the credentials via a small handoff file.

## Files

- `hangame.cpp` — `load()` export, ini parse, credential hold, the four hooks.
- `hook.hpp`    — standalone `Utils::hook::return_value` / `write` helpers, so
                  this builds with no dependency on the psobb.io native code.

## How it works

1. The launcher writes `hangame.ini` next to psobb.exe (only for Hangame-mode
   profiles), then starts psobb.exe.
2. patch.dll's ASI loader scans `patches/` and the game root for `.asi` files
   and calls each one's exported `void __stdcall load(void)`.
3. This ASI's `load()` -> `hangame::apply()`:
   - reads `hangame.ini`, then **deletes it** (tight on-disk window for the
     plaintext password),
   - if both credentials were present, applies the four hooks; otherwise no-ops.

Because it no-ops when `hangame.ini` is absent, the same `.asi` can sit in
`patches/` permanently and only activates on Hangame launches. Standard
(registry) launches are completely unaffected.

## Build

Visual Studio, C++ empty project. **Win32 (x86)** — the client is 32-bit, so
this MUST be x86, not x64. Build in **Release**.

| Setting | Location | Value |
|---|---|---|
| Configuration Type | General | Dynamic Library (.dll) |
| Output File | Linker > General | `$(OutDir)$(TargetName).asi` |
| Runtime Library | C/C++ > Code Generation | Multi-threaded (`/MT`) |
| Randomized Base Address | Linker > Advanced | No (`/DYNAMICBASE:NO`) |
| Platform | (top of properties) | Win32 / x86 |

The undecorated `load` export is produced by a linker pragma in `hangame.cpp`:

```cpp
#pragma comment(linker, "/EXPORT:load=_load@0")
```

`__stdcall` decorates `load` to `_load@0`; the pragma re-exports it under the
plain name `load` so the loader resolves it via `GetProcAddress(h, "load")`.
(No `.def` file is used.)

Verify the export after building:

```
dumpbin /exports Release\Hangame.asi
```

You should see `load = _load@0` in the export table. If you only see
`_load@0`, the loader won't find it — the pragma didn't take.

Place the built `Hangame.asi` in the game's `patches/` folder (or game root).

### Why these settings

`/MT` (static runtime) avoids a dependency on the VC++ redistributable inside
the game process. `/DYNAMICBASE:NO` is set on this module as belt-and-
suspenders; the hooks themselves write to fixed absolute addresses in
**psobb.exe** (0x0082D2F8, 0x0048210D, 0x0082D300, 0x0082D308), not in this
module, so they assume the Tethealla client loads at its preferred base (it
isn't packed/relocated). These addresses are **TethVer12513-specific** — a
different client build would need them recomputed.

## Hook layer (standalone)

`hangame.cpp` includes the bundled `hook.hpp`, which implements just the two
helpers used:

- `Utils::hook::return_value(addr, int)` — patches the function at `addr` to
  `mov eax, imm32; ret`.
- `Utils::hook::return_value(addr, const char*)` — same, returning a pointer.
  The pointer is baked in, so the buffer must outlive the patched function —
  which is why `hangame.cpp` holds the credentials in `static` storage.
- `Utils::hook::write(addr, { bytes })` — VirtualProtect, overwrite, restore,
  flush instruction cache.

This means **no dependency on the psobb.io native codebase** to build. To
switch to the real `Utils::hook` layer, change the include in `hangame.cpp` to
that header and delete `hook.hpp`; the call sites are unchanged as long as the
signatures match. `hook.hpp` is **x86-only**.

> If the native `return_value`/`write` have different semantics (e.g. a
> trampoline instead of a hard `mov/ret` stub), swap to that header. For the
> four uses here, the simple stub form is what's intended.

## Credential format (enforced launcher-side)

- username ends in `@HG`, max 11 chars
- password numeric, 1–8 digits

The ASI doesn't re-validate; it trusts the launcher. Add the same checks in
`read_handoff` before applying hooks if you want defense in depth.

## Coexistence with other ASIs

If another ASI (e.g. Omnispawn) is also in `patches/`, both get `load()`-
called. They touch unrelated addresses, so order shouldn't matter — but the
loader doesn't guarantee load order, so don't build in cross-ASI assumptions.

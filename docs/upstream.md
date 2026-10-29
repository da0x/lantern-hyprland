# Gaps in Hyprland's IPC

Lantern talks to the running compositor only through Hyprland's IPC:

```
Hyprland  →  IPC (request socket, event socket)  →  Lantern
```

Lantern is independent of the Hyprland project. When Lantern needs something
IPC does not give, it works around the gap itself, in one place: Lantern's IPC layer.

This file lists those gaps, so the workarounds stay small and visible, and so anyone who
wants to fix them in Hyprland has the details. Each entry says what is wrong, where in
Hyprland's source, and what a fix could look like. References are to Hyprland `main` at
`19fb395d` (2026-10-04), where IPC lives in `src/ipc/s1/` (requests) and `src/ipc/s2/`
(events).

## Gaps, smallest fix first

### `getoption` returns invalid JSON for some string values

`dispatchGetOption` (`src/ipc/s1/Commands.cpp:1671`) writes a Lua-config string option
into the JSON without escaping it. The hyprlang branch two lines above escapes it. A
value with a `"` or `\` produces a reply no JSON parser accepts. The `custom`, `css`,
`gradient` and `font_weight` branches (`:1673-1683`) and the echoed option name have the
same problem.

Fix: pass each through `escapeJSONStrings`. Output changes only where it is broken today.
There is no `getoption` test yet; one belongs in `hyprtester/src/tests/main/hyprctl.cpp`.

### `binds` reports `submap_universal` as a string

`src/ipc/s1/Commands.cpp:1121` emits `"submap_universal": "false"`. Every other flag in
the same object is a JSON boolean. An earlier change to the `binds` JSON fixed other
fields of this kind but missed this one.

Fix: drop the quotes. This changes the type, so it needs a line in the change notes. The
existing `hyprctlBindsJson` test can check the type.

### `binds` cannot say what a Lua binding does, or where it came from

For an `hl.dsp.*` binding the `dispatcher` field now reads `HL.Dispatcher(exec_cmd)`, but
the arguments are lost: `exec_cmd("kitty")` and `exec_cmd("firefox")` look the same.
`arg` is an internal Lua registry number. A function binding reads `function: 0x…`.

No IPC reply says which file and line set a binding, option or rule.
`Internal::getSourceInfo` (`src/config/lua/bindings/LuaBindingsInternal.cpp:405`)
already computes `file:line`, but it is used only in error messages and never stored.

Fix, adding fields only:
- a `source` field (`"/path/hyprland.lua:42"`) on bindings, stored in `SBindMetadata`
  (`src/keybinds/Bind.hpp:39`);
- an `args` field carrying a dispatcher's arguments;
- the same `source` field later for rules.

This is the change Lantern would gain most from, since without it every Lua user's
keybindings have to be read by running their config.

### `hyprctl -j` corrupts escaped semicolons in a batch

The compositor now accepts `\;` inside a `[[BATCH]]` (`src/ipc/s1/S1.cpp:128-150`), so
Lua code containing `;` can be batched. But `hyprctl`'s `batchRequest`
(`hyprctl/src/main.cpp:351`) prefixes every command with `j/` by replacing `;\s*`, which
also matches an escaped `\;`. Small, and only affects JSON batches.

### The event socket

- data is cut at 1024 bytes, which can split a UTF-8 character (`src/ipc/s2/S2.cpp:20`);
- newlines in data become spaces (`:21`);
- payloads are comma-joined, though titles can contain commas;
- a client 64 events behind is disconnected (`src/ipc/s2/Unix.cpp:187-197`);
- window addresses have no `0x` in events but do in JSON replies.

A structured (JSON) event stream would fix most of these, but it is a design discussion
with the maintainers, not a quick change. Making the queue limit configurable is a
smaller first step.

### Larger, touching config semantics

- Hyprland keeps only the current value of an option and whether anything set it. It does
  not keep the value the config file set, so a client cannot show "changed at runtime"
  or undo one change.
- `Hyprland --verify-config` runs the config for real, including `hl.exec_cmd` and `io`
  calls at the top level. A side-effect-free check would let editors validate safely.
- Errors in `hl.config({...})` point at the line of the call, not the key. Lua tables do
  not carry per-key line numbers, so this one is hard.

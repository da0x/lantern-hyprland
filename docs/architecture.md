# Architecture

How Lantern is put together, and why. The rules every change must follow are in
[`AGENTS.md`](../AGENTS.md); the facts about Hyprland this rests on are in
[`research.md`](research.md).

## The shape

```
Hyprland  ←  IPC (request socket, event socket)  →  Lantern
                                                     ├─ reads the config in its own sandbox
                                                     └─ writes only ~/.config/lantern-hyprland/
```

Lantern reaches the running compositor only through Hyprland's IPC sockets. It reads
the user's config by running it in a sandbox where nothing takes effect, and writes it only
after the user reviews the change. Its own files live in one folder.

## Layers

Each layer is a folder, a C++ namespace and a static library named
`lantern-hyprland-<layer>`. A layer may use the ones above it in this list, never the ones
below.

| Layer | What it does | Depends on |
|---|---|---|
| `foundation` | Strong types, flag sets, UTF-8 cleanup, formatters | the standard library |
| `lua` | The one wrapper around the Lua 5.5 C API: owned state, allocator with a limit, instruction hook, and a trampoline so Lua's `longjmp` never skips a C++ destructor | Lua |
| `configuration` | The recording interpreter, key combinations, bindings, conflicts, option assignments, finding the active config, line diffs, writing reviewed changes with a backup and undo, the autostart lines, and in-place edits of hl.config values and hyprlang keys | foundation, lua |
| `desktop` | The desktop around Hyprland: programs on the `PATH` and running ones, installed fonts, the distribution, the clipboard through `wl-copy`, Waybar's needs, config and styles, wallpapers with hyprpaper's config and awww's commands, and the desktop-wide theme | foundation, configuration, fontconfig |
| `ipc` | Instances, the read-only request allow-list, the request client, reply and event parsing, the event connection, the running instance's launch | foundation, configuration, nlohmann-json |
| `compositor` | Monitors, workspaces and windows, built from a snapshot and kept current by events | ipc |
| `activation` | The live layer: allowed areas, effects, applying and removing them, the theme restore record | ipc, configuration, GIO (GSettings) |
| `application` | The session that ties it together: workers, results, merging recorded and live bindings, the option catalog, outside programs' On and Off, Lantern's own settings, running short commands, the desktop portal's file chooser (sd-bus) | all of the above |
| `ui` | Pages on hyprtoolkit: keybindings, bar, wallpaper, theme, options, live tree, event log, console, areas and Activate | application, desktop, hyprtoolkit |

`foundation`, `lua` and `configuration` use no UI library; the build fails if one is
linked. hyprtoolkit's `CMyClass` naming stays inside `ui`.

## Threading

- hyprtoolkit's main loop owns the UI. The event socket is non-blocking and watched with
  `addFd`; every wakeup drains it completely, because Hyprland disconnects a client that
  falls 64 events behind.
- Blocking work (requests, running the config reader) runs on `std::jthread` workers.
  Results go into a queue, and an `eventfd` watched by the main loop wakes it to take them.
  Nothing on the main loop waits.
- After a reconnect: subscribe to events, buffer them, take a full snapshot, replay the
  buffer. Applying an event is idempotent, so overlap is harmless.
- Nothing polls. An event may trigger one targeted query; `configreloaded` re-reads the
  config.

## Reading a config

The config is run in Lantern's own Lua 5.5 state, built from an allow-list:

- base, string, table, math, utf8 and coroutine libraries only;
- its own `require`, resolving modules the way Hyprland does, never loading C modules;
- read-only file access to regular files inside the config folder, following links;
- `os.getenv` and the time functions; every write, process start, `exit` and `setlocale`
  blocked and recorded;
- limits on instructions, time, memory, calls and value sizes;
- a hand-written `hl` stand-in that records each call with its file and line stack,
  returns descriptions for `hl.dsp.*`, runs `define_submap` bodies the way Hyprland does,
  and stores `on` and `timer` callbacks without running them.

The output is a recording, from which bindings, conflicts and option assignments are
derived. It is checked against what the running Hyprland reports.

## Activation

Activate applies the effects of every **allowed** area to the running session; Deactivate
puts everything back. Areas start disallowed, each has its own switch, and nothing
activates on its own.

| Area | How it is applied | How it is undone |
|---|---|---|
| Options and rules | `eval` under Lua, `keyword` under hyprlang | reload Hyprland, which drops every runtime change |
| Keybindings | the same, after showing collisions with the user's own | the same reload |
| Outside programs | start programs the user installed | stop exactly the processes Lantern started |
| Desktop theme | GSettings, after writing each previous value to a restore record | write the previous values back, delete the record; also on the next start after a crash |

Only the activation layer may send requests that change the desktop;
everything else, the IPC console included, goes through the read-only allow-list.

The effects themselves are kept in `~/.config/lantern-hyprland/effects.lua`, as plain
`hl.*` calls the user can read and copy into their own config.

## What Lantern writes

Only `~/.config/lantern-hyprland/`: `settings.conf` (hyprlang format), `effects.lua`, and
the theme restore record while the theme area is active. The app can delete the folder.

## Libraries

All packaged by distributions: hyprtoolkit, GLib's GIO for GSettings (already used by
hyprtoolkit), Lua 5.5, nlohmann-json, tree-sitter with its Lua grammar (for editing), and
GoogleTest for tests. The build follows `AGENTS.md`'s *Packageable as is*.

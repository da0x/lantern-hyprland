# Research

What Hyprland gives us to build on, what we have confirmed, and what is still open.
Each section is a question, answered with the evidence for it. Source references are to
Hyprland at tag `v0.56.2` (commit `efb5099`), relative to the repository root.

Checked on 2026-10-06 against Hyprland 0.56.2 on Arch-based Linux, with Qt 6.11, GCC 16.2
and Clang 22.

## What this research changed

1. **Keybindings cannot be read from IPC under a Lua config.** Every Lua binding shows up
   in `hyprctl binds` as `dispatcher: "__lua"` with a meaningless number as its argument.
   A keybinding browser for Lua users has to read the config itself. Hyprland `main` has
   since improved this partly (the dispatcher's name is shown, not its arguments); see
   `docs/upstream.md`.
2. **Reading the config means running it, safely.** The shipped example builds its
   bindings from variables and a `for` loop, so a parser alone cannot list them. We will
   run the config in our own sandboxed Lua 5.5 with a recording stand-in for `hl`. See
   *How do we read a Lua config?*
3. **Hyprland's verifier is not safe to run casually.** `Hyprland --verify-config` runs the
   whole config, and that starts programs and writes files. Tested, see below.
4. **Runtime changes work through `eval`, not `keyword`,** and a reload drops them all,
   which gives "try before you write" a free revert.
5. **Monitor layout editing is well covered by other tools. Everything planned for 0.1 is
   not.** See *What already exists?*

## Which config formats does Hyprland read, and which wins?

Two: Lua (`hyprland.lua`) and the older hyprlang (`hyprland.conf`). The file extension
picks the parser (`src/config/ConfigManager.cpp:29-52`).

The config path is chosen in this order (`src/config/supplementary/jeremy/Jeremy.cpp:26-46`):

1. safe mode's `recoverycfg.lua`;
2. `--config FILE`;
3. the `HYPRLAND_CONFIG` environment variable;
4. `hyprland.lua` found on the XDG config path;
5. `hyprland.conf` found on the XDG config path;
6. otherwise a new `hyprland.lua` with the default config.

The search path is `$XDG_CONFIG_HOME` (only if absolute), `~/.config` (only if `HOME` is
absolute), each folder of `$XDG_CONFIG_DIRS`, then `/etc/xdg`, each with `hypr/` added
(hyprutils' `Path::findConfig`). A `--config` path is resolved against Hyprland's working
folder with links followed (`src/main.cpp:120-145`); the other paths are used as found.

**Hyprland chooses once, at startup.** The path is cached, and only safe mode ending or
`hyprctl reload full-reset` chooses again (`Jeremy.cpp:15`, `src/debug/HyprCtl.cpp:1267`).
So Lantern follows the same rule with what the running Hyprland was started with: its
process ID is the first line of `hyprland.lock` in the instance folder, followed by the
Wayland socket name (`src/Compositor.cpp:765-772`), and its command line, starting
environment and working folder are in `/proc/<pid>/`. Files created or removed since
startup can make the answer differ, which `configProvider` in `hyprctl -j status` (`lua`
or `hyprlang`) partly catches. After a crash, `--safe-mode` runs `recoverycfg.lua` from the
instance folder until the user loads their config from Hyprland's dialog, and nothing
reports which of the two is running.

So **a `hyprland.lua` anywhere on the search path beats any `hyprland.conf`.** Lantern
must never create a `hyprland.lua` next to a working `hyprland.conf` by accident. Doing
that switches the user's desktop to a different config at the next reload.

`hyprctl -j status` reports which parser is running (`"configProvider": "hyprlang"` or
the Lua one), so Lantern can tell without guessing.

There is no `.conf` to `.lua` migration tool in Hyprland. Many users, including the
development machine, still run `.conf`.

## How does `require` work in a Lua config?

- `package.path` starts with `<config directory>/?.lua;<config directory>/?/init.lua`
  (`src/config/lua/ConfigManager.cpp:534-546`).
- Paths starting with `/`, `./`, `../` or `~/` resolve relative to the main config
  (`:77-111`). A glob such as `require("./conf.d/*.lua")` loads every match and returns an
  array (`:132-164`, `:215-264`).
- Every required file is recorded and watched. A change to any of them triggers a reload
  unless `misc:disable_autoreload` is set (`:561-598`,
  `src/config/shared/inotify/ConfigWatcher.cpp:32-57`).

So **a config is a set of files, not one file.** Writing any of them reloads Hyprland.
Lantern must write each file once, atomically, and expect a reload to follow.

## Which Lua, and is it sandboxed?

Lua **5.5** (`CMakeLists.txt:291`; the binary links `liblua.so.5.5`). Not sandboxed: the
full standard library is loaded, including `os` and `io`. Only `debug.sethook` and
`debug.gethook` are removed (`src/config/lua/ConfigManager.cpp:520-529`).

Time limits: loading the config 1500 ms, `eval` 250 ms, keybinding callbacks 100 ms,
event handlers and timers 50 ms (`ConfigManager.hpp:107-114`).

## What is the shape of the `hl` API?

Hyprland ships LuaLS type annotations at `/usr/share/hypr/stubs/hl.meta.lua`. The `HL.API`
class (lines 820-868) lists every call. Options are set with nested tables:

```lua
hl.config({
    general = {
        gaps_in  = 5,
        gaps_out = 20,
        col = { active_border = { colors = { "rgba(33ccffee)", "rgba(00ff99ee)" }, angle = 45 } },
    },
})
hl.bind(mainMod .. " + Q", hl.dsp.exec_cmd(terminal))
hl.bind("XF86AudioMute", hl.dsp.exec_cmd("wpctl ..."), { locked = true, repeating = true })
```

Flat dotted keys (`["general.gaps_out"] = 20`) also work. An option's IPC name maps to
Lua by turning `:` into `.` and `-` into `_` (`src/config/lua/ConfigManager.cpp:1136-1141`).

**Calls that take plain values** (can be edited in place): `config`, `device`, `monitor`,
`curve`, `animation`, `env`, `window_rule`, `layer_rule`, `workspace_rule`, `permission`,
`plugin.load`, `unbind`.

**Calls that can take functions** (read-only to Lantern): `bind` (its action is an
opaque `hl.dsp.*` value or a function), `on`, `timer`, `define_submap`, `gesture`,
`layout.register`, `dispatch`. There is no `exec_once`; startup programs go in
`hl.on("hyprland.start", function () ... end)`.

How often each call appears in the shipped example: `bind` 30, `animation` 17,
`config` 7, `curve` 6, `window_rule` 5, `permission` 3, `exec_cmd` 3, and the rest once
or twice.

**Unconfirmed:** whether `permission` takes positional strings (as the example writes it)
or a table (as the stubs say).

## How do we read a Lua config?

A parser alone is not enough. Even Hyprland's own example writes bindings like this:

```lua
local mainMod = "SUPER"
hl.bind(mainMod .. " + Q", hl.dsp.exec_cmd(terminal))
for i = 1, 10 do
    hl.bind(mainMod .. " + " .. i % 10, hl.dsp.focus({ workspace = i }))
end
```

Nothing in the text says "SUPER + 1".

**Proposed: a recording interpreter.** Run the user's config in our own Lua 5.5 state, in
which:

- `hl` is a stand-in that records every call: the function, its arguments as plain
  values, and the file and line it came from (from `debug.getinfo`);
- `hl.dsp.*` returns descriptions of the actions instead of real dispatchers;
- `os`, `io` and other side effects are removed or stubbed, so nothing runs and nothing
  is written;
- `require` resolves the way Hyprland's does, so every file is read;
- an instruction-count limit stops a runaway loop.

The result is the full list of bindings, options, rules and monitors, each with its
source location. It handles variables, string building and loops, with no static
analysis. This needs checking in Phase 1: whether it agrees with what Hyprland itself
registers, by comparing the recorded bindings with `hyprctl binds` on a test instance.

**For editing**, tree-sitter keeps every byte. A recorded call whose arguments are
literals in the source can be edited by replacing those bytes. A value that came from a
variable or a loop is shown read-only, pointing at the line that produced it.

## What does a real, distributed config look like?

Checked on 2026-10-18 against the ML4W dotfiles (the `com.ml4w.dotfiles.stable` package
for Hyprland 0.56), installed on the development machine. A good stress test, because a
popular dotfiles distribution is what many users actually run.

- **76 Lua files.** `hyprland.lua` only `require`s others, about 20 of them in `conf/`,
  each with a folder of alternatives (`conf/animations/`, `conf/decorations/` and so on).
- **The files loaded depend on a variable.** `conf/keybinding.lua` sets
  `local name = "default.lua"` and calls `load_variant(name, "keybindings")`, a global
  function from `functions.lua` that builds the module name and calls `require`. A
  reader that only follows literal `require("...")` calls misses most of the config.
  The recording interpreter follows it, because it runs the code.
- **The config reads files at load time.** `hyprland.lua` uses `io.open` to check whether
  `custom.lua` exists, and the keybinding file parses `input.lua` with `io.open` and
  `f:read` to pick workspace keys for AZERTY layouts. Four files use `io` or `os`. So the
  interpreter's sandbox cannot simply remove `io`: it needs read-only file access inside
  the config folder, and `os.getenv`, with writes and process starts stubbed out.
- **Some files are generated.** `colors.lua` is rewritten by a theme tool (matugen) when
  the wallpaper changes. Lantern should show such values read-only, or its edits will
  be overwritten. How to detect a generated file is open: a header comment, a known tool,
  or the file changing without Lantern.
- **It loads HyprMod's file too.** The last line is `require("hyprland-gui")`, HyprMod's
  sidecar file, shipped empty. A distribution has already adopted the sidecar approach.
- **Every binding has a `description`.** That is the only part of a Lua binding the
  running Hyprland reports back, so well-described configs show up readably even without
  the interpreter.
- **The whole config folder is a link.** `~/.config/hypr` points into
  `~/.mydotfiles/com.ml4w.dotfiles.stable/.config/hypr`. Lantern has to follow it when
  reading and write through it, never replace the link with a real folder.

## How do we edit a Lua file without losing anything?

**Use tree-sitter with the tree-sitter-lua grammar.** The canonical grammar is
https://github.com/tree-sitter-grammars/tree-sitter-lua (MIT, release 0.5.0 of
2026-02-26). It covers Lua 5.5, including `global` declarations, `<const>` and
`<close>`, `//` and `goto`. Comments are nodes, and whitespace is the gap between byte
ranges, so replacing a byte range changes nothing else. The C API (`tree_sitter/api.h`)
is easy to wrap in C++. Both `tree-sitter` (0.26.9) and `tree-sitter-lua` (0.5.0) are in
Arch's `extra` repository with pkg-config files.

**Rejected:**
- full-moon: Rust, and no Lua 5.5;
- LuaJIT: Lua 5.1 syntax;
- Lua's own parser: drops comments and positions;
- a hand-written parser: one more thing to maintain.

After every edit, parse the result again and refuse to write if the tree has any
`ERROR` or `MISSING` node.

## How do we check a config before Hyprland loads it?

`Hyprland --verify-config --config FILE` runs the config without starting a compositor.
It prints `config ok` and exits 0, or prints one error per line and exits 1
(`src/Compositor.cpp:271-278`, `src/main.cpp:289-292`). It takes about 16 ms.

What it catches, tested:
- syntax errors, with file and line;
- wrong value types, such as `gaps_out = "banana"`;
- unknown option names;
- errors inside `require`d files, reported against that file;
- a missing `require`d file.

It reports **every** bad value, not only the first. But **the line it gives is the line
where the `hl.config(` call starts, not the line of the bad key.** Lantern has to map
each error to its key using the option name in the message.

**It runs the config for real.** Tested with a config containing
`hl.exec_cmd("touch …")` and `io.open(…, "w")` at the top level: both files were
created. Only `hl.on("hyprland.start", …)` callbacks are skipped. So Lantern must not
run `--verify-config` as a quiet background check. It starts the user's programs every
time.

**Open:** run it inside a sandbox (bubblewrap with no network and a throwaway home), or
check with the recording interpreter first and run Hyprland's verifier only as the last
step before a write.

## What does IPC give us?

Two Unix sockets per instance, in `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/`:

- **`.socket.sock`, requests.** Send `flags/command`, where the flags include `j` for
  JSON (`src/debug/HyprCtl.cpp:2069-2097`). The server waits up to 5 seconds for the
  request. `[[BATCH]]a;b` runs several commands and joins the replies with three newlines
  (`:1309-1333`). It splits on every `;` outside brackets, **including inside Lua code**,
  so never batch an `eval`.
- **`.socket2.sock`, events.** One `name>>data` line per event. Data is cut at 1024 bytes
  and newlines in it become spaces. **A client that falls 64 events behind is
  disconnected** (`src/managers/EventManager.cpp:126-177`), so the reader must never
  block on the UI thread. It must reconnect and re-query the full state when it drops.

Window addresses are hex without `0x` in events and with `0x` in JSON.

### Events for the live tree

| Event | Data |
|---|---|
| `monitoraddedv2`, `monitorremovedv2` | `id,name,description` |
| `createworkspacev2`, `destroyworkspacev2` | `id,name` |
| `renameworkspace` | `id,name` |
| `changeworkspaceid` | `old,new` |
| `moveworkspacev2` | `id,name,monitor` |
| `workspacev2` | `id,name` |
| `activespecialv2` | `id,name,monitor` |
| `focusedmonv2` | `monitor,workspace id` |
| `openwindow` | `address,workspace name,class,title` |
| `closewindow` | `address` |
| `movewindowv2` | `address,workspace id,workspace name` |
| `windowtitlev2` | `address,title` |
| `activewindowv2` | `address` |
| `changefloatingmode` | `address,0 or 1` |
| `fullscreen` | `0 or 1` |
| `pin` | `address,0 or 1` |
| `togglegroup`, `moveintogroup`, `moveoutofgroup`, `lockgroups` | group changes |
| `configreloaded` | (empty) |

Other events for the event log: `openlayer`, `closelayer`, `activelayout`, `submap`,
`screencastv2`, `urgent`, `minimized`, `bell`, `kill`, `custom`.

Titles can contain commas, so parse `openwindow` and `windowtitlev2` from the left,
splitting only as many fields as the event has before the title.

### Request replies

- **`binds`**: `modmask`, `key`, `keycode`, `submap`, `submap_universal` (a string, `"false"`),
  `dispatcher`, `arg`, `description`, `has_description`, and the flags `locked`,
  `mouse`, `release`, `repeat`, `longPress`, `non_consuming`, `auto_consuming`,
  `catch_all`, `allow_input_capture`. **Under Lua every binding has `dispatcher: "__lua"`**
  and only its `description` (if the user wrote one) says what it does
  (`src/config/lua/bindings/LuaBindingsToplevel.cpp:148-205`). Under `.conf` the dispatcher
  and argument are real.
- **`descriptions`**: `name`, `description`, `default`, `current`, `min`, `max`, `map`. 353 options.
- **`getoption <name>`**: the value under a key named for its type (`int`, `bool`, `float`,
  `vec2`, `str` or `custom`) and `set`, which is true once anything has set it. Lua string
  values are not JSON-escaped (`HyprCtl.cpp:1716`), so parse defensively.
- **`monitors all`**: identity (`id`, `name`, `description`, `make`, `model`, `serial`),
  mode (`width`, `height`, `refreshRate`, `availableModes`), placement (`x`, `y`,
  `scale`, `transform`, `mirrorOf`, `disabled`), the active and special workspace, and
  HDR, VRR and scanout details.
- **`workspaces`**: `id`, `name`, `monitor`, `monitorID`, `windows`, `hasfullscreen`,
  `lastwindow`, `lastwindowtitle`, `ispersistent`, `tiledLayout`.
- **`clients`**: `address`, `class`, `title`, `initialClass`, `initialTitle`, `pid`,
  `workspace`, `monitor`, `at`, `size`, `floating`, `fullscreen`, `pinned`, `grouped`,
  `tags`, `xwayland`, `stableId` and more.

**Nothing in IPC says which file and line set a binding or option.** Source locations
come only from reading the config.

## Can we change things at runtime without writing the file?

Yes, under both formats, with different commands:

- Under **Lua**, `hyprctl keyword` is refused ("keyword can't work with non-legacy
  parsers. Use eval.", `HyprCtl.cpp:1160-1163`). `eval` runs Lua in the same state as the
  config, so `hl.config({ general = { gaps_out = 16 } })` takes effect at once
  (`src/config/lua/ConfigManager.cpp:880-953`).
- Under **`.conf`**, `keyword general:gaps_out 16` works as before.

**A reload drops every runtime change.** It builds a fresh Lua state, resets every option
to its default and runs the files again (`src/config/lua/ConfigManager.cpp:714-738`). So
reverting everything tried in a session is one reload.

**Hyprland does not keep the previous value.** `set` becomes true for any change,
including one from `eval`, and nothing records what the file had set. Lantern reads
the value with `getoption` before changing it, and keeps it for its own undo.

**Open, to test on a throwaway Lua instance (not a live desktop):** whether a reload
re-runs `hyprland.start` callbacks, and whether `eval` of `hl.monitor(...)` applies a
mode change right away.

## What already exists?

Checked through the GitHub API on 2026-10-06.

| Tool | Stack | Lua | How it writes |
|---|---|---|---|
| [HyprMod](https://github.com/BlueManCZ/hyprmod) | Python, GTK 4, about 1.1k stars | yes, with a migration wizard | its own `hyprland-gui.lua` plus one `require` line in the user's file |
| [hyprsettings](https://github.com/acropolis914/hyprsettings) | TypeScript web view | no | rebuilds `hyprland.conf`, keeps comments |
| [hyprKCS](https://github.com/kosa12/hyprKCS) | Rust, GTK 4 | no | edits `.conf` bind lines in place |
| [nwg-displays](https://github.com/nwg-piotr/nwg-displays) | Python, GTK | yes | its own monitors file |
| [hyprmoncfg](https://github.com/crmne/hyprmoncfg) | Go | yes | its own monitors file plus a `require` line |
| [HyprMon](https://github.com/erans/hyprmon) | Go TUI | yes | its own monitors file |
| [monique](https://github.com/ToRvaLDz/monique) | Python | yes | generated monitors file |
| [Hyprland Inspector](https://github.com/verdoxOP/hyprland-inspector) | Tauri | n/a | read-only live view and event stream, very new |

Archived or stale: ML4W Hyprland Settings, HyprPanel, hyprset, Hyprkeys, Cuttlefish,
anotherhadi/hyprsettings. hyprparser-py was archived because "0.55+ uses Lua".

What this means for our plan:
- **Keybinding browser with conflict and shadowing detection:** a gap for Lua. hyprKCS and
  hyprsettings detect conflicts only in `.conf` text.
- **Option browser from the running Hyprland's own descriptions:** a gap. HyprMod and
  hyprsettings ship their own option lists, which drift from the installed version.
- **Live tree and event log:** a gap. Nothing native, and one very new Tauri app.
- **Monitor layout editor:** well covered by five tools. Not worth leading with.
- **Editing `hyprland.lua` in place:** a clear gap. Every Lua-aware tool writes a file of
  its own and adds a `require` line. That is the fallback if in-place editing of a value
  is impossible.

HyprMod is the nearest project. What sets us apart: native, live-first, an option
catalog read from the installed Hyprland, and edits made in the user's own file.

## What can we learn from Noctalia?

[Noctalia](https://github.com/noctalia-dev/noctalia) (MIT, about 11k stars, in Arch's
`[extra]`) is a Wayland shell: bar, dock, launcher, notifications, lock screen, wallpaper
and a settings UI. Version 5 is native C++ on Wayland and OpenGL ES with no Qt or GTK; the
older version 4 was built on Quickshell. Checked on 2026-10-19 from its repository and
documentation at https://docs.noctalia.dev; no code was copied.

What it does well, and we follow:
- It talks to Hyprland's sockets directly, never through `hyprctl`: one long-lived event
  connection, and a short connection per request with a timeout
  (`src/compositors/hyprland/`).
- It asks `j/status` whether the config is Lua or legacy, and writes dispatches in the
  matching syntax.
- It refreshes its state fully on `configreloaded`.
- It sends no `keyword` and no `eval`, and never writes Hyprland's config. The user adds
  its autostart line and suggested bindings by hand, from its documentation.
- Its settings UI writes a separate override file, never the user's own settings file, and
  drops overrides equal to what is underneath.

Where it goes further than we will:
- Optional theme templates add includes to other programs' configs (`gtk.css`, terminal
  configs) and create configs where none exist. Turning a template off removes its include.
- It can set the desktop's light or dark preference (`color-scheme`) to match its theme.
- Uninstalling leaves `~/.local/state/noctalia`, `~/.cache/noctalia` and generated theme
  files to delete by hand, and its uninstall page does not list the generated files.
- Plugins are trusted code with no sandbox.

So the difference to highlight is cleanup: Lantern keeps everything it owns in
one folder and can remove it, and writes nothing into other programs' configs.

## The project is called "Lantern for Hyprland"

It was called Hyprland Studio before its first release. The hypr.land footer says: "The name "Hyprland"
and the logo are registered trademarks of Hyprland Development." It was added in December
2025. There is no published usage policy (`/trademark`, `/brand` and `/legal` return 404),
and the registration could not be confirmed.

A name that starts with Hyprland reads as the Hyprland project's own, and so does one that
starts with `hypr`, the prefix of hyprwm's tools. "Lantern for Hyprland" names Hyprland
only to say what it is for, and the README keeps the statement that this project is
independent and not endorsed.

The plain name is taken: Lantern, the censorship-circumvention tool, is in the AUR as
`lantern-bin` with a `lantern` command. So the package, the command, the window class and
the settings folder are all `lantern-hyprland`. No AUR package or GitHub repository was
found under that name.

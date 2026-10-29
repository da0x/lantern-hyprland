# AGENTS.md

How to work on Lantern. This file is for everyone who changes the code, people
and assistants alike, and it is kept current: if something here is wrong, fix it in the
same change.

## The rules that shape everything else

**The mission is to enhance Hyprland in the least intrusive way possible.** This rule
outranks every feature. Lantern must never leave a user asking "what did it do to my
system?":

- It never installs a package, never runs `sudo`, and never asks for a password.
- When an outside program would help (a status bar, a dock, a notification daemon), it
  prefers an existing, widely packaged open-source one, such as Waybar. It names the
  program, links its upstream, explains why, and shows the exact install command for the
  user to run themselves.
- It writes only files the user approved in a reviewed diff. It never replaces a symbolic
  link with a real file: it edits the file the link points at, and the review names that
  file. It never moves or deletes the user's files, and never adds autostart entries,
  shell startup lines or theme settings on its own: only as part of a change the user
  reviewed and applied, such as the Wallpaper page starting the chosen program at login.
  Copying an image the user picked into their wallpapers folder is the one write without
  a diff, because picking it is the request.
- Every change it makes goes into a readable log, with a way to undo it.
- It does not ship its own replacements for programs the Linux desktop already has.
- It never makes the final decision. It reports, explains the options and what each
  would change, and may recommend one; the user chooses. Nothing is fixed automatically,
  not even a keybinding conflict.
- It leaves nothing behind. What it writes into the user's config is plain Hyprland
  configuration: no markers, no sidecar files, no `require` lines of its own, so the config
  works the same once Lantern is gone. Everything it keeps for itself lives in
  `~/.config/lantern-hyprland/`, which the app can remove on request. Uninstalling the
  package and deleting that folder must leave the system exactly as the user's own
  choices made it.

**The user's config file is the source of truth.** Lantern never stores Hyprland
settings anywhere else, never keeps a private copy of the config, and never asks the user
to migrate to anything. It keeps three things in `~/.config/lantern-hyprland/`: its own UI
settings (window size, last page, which areas the user allowed), a record in `backups/` of
each change it wrote, with copies of the files it replaced, so the change can be undone,
and, only while the desktop-theme area is active, a restore record of the theme values it
changed. Anything
else it would write needs an entry in the README's *What it reads and writes* first.
Someone who uninstalls it must be left with a config that works and reads the way they
wrote it.

**Activation is a live layer, and every area needs the user's permission.** Activating
applies Lantern's effects to the running Hyprland and desktop; deactivating puts
everything back exactly. Each area (Hyprland options and rules, keybindings, outside
programs, desktop theme) has its own "allow" switch on its page, off until the user turns
it on. Nothing is activated automatically, at startup or otherwise.
- Options, rules and keybindings are applied through IPC only, never written to the
  config. Deactivating reloads Hyprland, which drops every runtime change, so the desktop
  is again exactly what the user's files say.
- Outside programs are only ones the user installed; activating starts them, deactivating
  stops exactly the processes Lantern started.
- The one exception is a program's own On and Off switch, on its page, which the user
  presses: Off stops every running copy of it, whoever started it, and On starts it the way
  Hyprland would, detached from Lantern. Neither changes what starts at login. After Lantern
  writes that program's files, it restarts the program so it reads them, but only
  while it is on, and if it then does not start, the files are put back.
- The desktop theme lives in dconf, which is on disk, so before changing a value Lantern
  writes its previous value to the restore record. Deactivating, or the next start
  after a crash, puts every value back and deletes the record.

**Edits are lossless.** A change replaces exactly the bytes of the value that changed.
Everything else in the file, including comments, blank lines, ordering, `require` calls,
and code Lantern does not understand, stays byte for byte identical. Reading a file
and writing it back with no edits must produce the same bytes. A test proves this for
every fixture.

**A Lua config is a program, not a data file.** Only `hl.*` calls whose arguments are
literals are editable. A value that is computed (from a local, a function, a loop) is
shown read-only, with the file and line it comes from. Do not try to evaluate or rewrite
user code.

**Live, reversible changes ship before ones that write files.** A change made through IPC
is undone by one reload, so it cannot break someone's desktop for longer than a click;
writing the config comes later. Requests that change the desktop are sent only by the
activation layer, for an area the user allowed; every other part of Lantern, the IPC
console included, sends read-only requests. Writing to disk always goes through a
diff the user reviews, a timestamped backup, an atomic write, a reload and a check, with
automatic rollback on failure.

**Never start the user's programs by accident.** `Hyprland --verify-config` runs the
whole config, including commands and file writes at its top level. Do not run it as a
background check. Reading a config for display goes through Lantern's own sandboxed
interpreter, which runs nothing. See `docs/research.md`.

**Never switch the user's config format.** A `hyprland.lua` on the config path wins over
any `hyprland.conf`, so creating one next to a working `.conf` changes which config
Hyprland loads at the next reload. Only a migration the user asked for may do that.

**The running compositor is reached only through IPC.** When IPC does not give
something Lantern needs, the workaround lives in the IPC layer and nowhere else, and
the gap is recorded in `docs/upstream.md`.

**Live screens update from events.** Compositor state comes from Hyprland's event
socket. Do not poll. If the socket drops, the screen says so and reconnects by itself,
because a dead connection otherwise looks exactly like a quiet compositor.

## Code

- **GPL-3.0-or-later.** Every source, header, CMake and script file starts with
  `SPDX-License-Identifier: GPL-3.0-or-later` in a comment, and CI checks it. Code from
  elsewhere comes in only under a GPL-compatible license, with its own notice kept
  (Hyprland's example config and stubs, for instance, are BSD-3-Clause).
- **Packageable as is.** A distribution, Arch Linux first, must be able to package any
  release with no patches. So the build respects the packager's flags: it works with
  `CMAKE_BUILD_TYPE=None`, adds no `-O` or `-march` of its own, and turns warnings into
  errors only when CI asks (`LANTERN_HYPRLAND_WARNINGS_AS_ERRORS`, off by default). It
  installs with `GNUInstallDirs` under any prefix and `DESTDIR`, downloads nothing during
  configure or build (no `FetchContent`), bundles no copy of a system library, runs its
  tests offline with `ctest`, and embeds no timestamps (`SOURCE_DATE_EPOCH` if one is ever
  needed). Releases are signed git tags that are never moved. The app ships a desktop
  file, AppStream metainfo and hicolor icons.
- **Mainstream libraries only.** hyprtoolkit (Hyprland's own Wayland-native toolkit,
  already present wherever Hyprland's own apps are), tree-sitter, Lua 5.5, nlohmann-json, fontconfig, sd-bus (libsystemd, for the desktop portal),
  and the C++ standard library: things distributions package. No personal or niche libraries, so Lantern builds from distribution
  packages alone and stays easy to package.
- **The latest C++ standard the shipping compilers support**, currently C++26. Modern
  code only: no raw `new` or `delete`, no C-style casts or arrays, no macros where a
  `constexpr` or a template will do. Use `std::expected` for errors, ranges, `std::format`,
  strong types instead of bare `int` and `std::string`, and value semantics. clang-tidy's
  `modernize-*` and `cppcoreguidelines-*` checks enforce this in CI.
- **Layers.** `configuration` (read, edit and diff config files) depends on no UI
  library. hyprtoolkit, the IPC sockets, Lua and the Lua syntax tree library are each
  wrapped once, so the rest of the code uses the project's own types and names
  (hyprtoolkit's `CMyClass` style stays inside its wrapper).
- **Names are full words**: `configuration`, not `config`; `application`, not `app`;
  `function`, not `fn`. A folder, its namespace and its concept share one name:
  `src/configuration/` is `namespace configuration`. Folders and files are lowercase,
  with hyphens in file and folder names.
- **Every feature comes with tests.** Configuration code is tested against real config
  files in `tests/fixtures/`.

## Commits

- One logical change per commit. Prefer "Add the keybinding model" then "Detect
  conflicting keybindings" over one large commit.
- Subjects are plain sentences that start with a capital verb: "Add the IPC client". No
  `feat(scope):` prefixes.
- No attribution or `Co-Authored-By` trailers.

## No personal data

Nothing committed may name a contributor's machine or its owner: no home folder, hostname,
email other than GitHub's noreply address, display or graphics card model, serial number,
or network or Bluetooth device name. The repository is public, and none of that helps
anyone else. Examples, tests and samples use made-up values: `/home/me`, `you@archlinux`,
an "Example 27" display. The *Tested on* table names a graphics maker and driver, never a
card.

The hooks in `.githooks` enforce it on every commit, reading what to look for from the
machine itself, so nothing personal is written down to compare against. Git runs nothing
on clone, so turn them on once per clone with `.githooks/setup`. They cannot read images:
before committing a screenshot, look at it for window titles, names and hardware.

## Where things are

- `docs/research.md`: what we know about Hyprland's config formats and IPC, and what is
  still open. Read it before touching the configuration or IPC code.
- `docs/upstream.md`: gaps in Hyprland's IPC, and how Lantern works around them.

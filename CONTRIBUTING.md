# Contributing to Lantern for Hyprland

Thank you for wanting to help. The most useful thing you can do is open an issue.

## Open an issue first

A bug, an idea, or a report of the setup you tested on: each one is an issue, and each
has a form that asks for what is needed. I read every one and usually make the change
myself, so you do not need to write code to help.

## Keep pull requests small

A pull request is welcome for a small change to an issue we have already agreed on: one
fix, a few files. Large pull requests, rewrites, reformatting and new dependencies will be
closed, however good they are. Open an issue to talk about the idea instead.

The reason is review. Lantern edits people's own config files, so every line has to be
read carefully before it ships, and a small change can be.

Before you open one:

- Read [`AGENTS.md`](AGENTS.md). Its rules outrank any feature: Lantern never installs
  anything, never runs `sudo`, and writes nothing the user did not review.
- Run `.githooks/setup` once. Its hooks stop a commit that names your machine: your home
  folder, hostname, email or hardware.
- Run `make test`, and say in the pull request what you tested on.

## Report what you tested on

Lantern is tested on one machine, so reports from others are how we learn where it works.
The **Tested on** issue form asks for your distribution, Hyprland and hyprtoolkit versions,
config format, graphics maker and driver, and Lantern version. Each report adds or updates
a row in the README's *Tested on* table. A report that everything worked is just as useful as one that
something broke.

## Reporting a bug

The **Bug** form asks for the same setup details. Start Lantern with
`lantern-hyprland --verbose` to see what it is doing, and after a crash,
`coredumpctl info lantern-hyprland` shows where it happened. Check both for anything
private before you paste them.

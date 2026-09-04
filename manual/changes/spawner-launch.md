---
title: Launch a configured match from the Spawner
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:spawn
  effect: added
credit: [ZivDero, Belonit, OpenTS contributors]
---

An external launcher can now start a match directly. With `-SPAWN` on the
command line and a `SPAWN.INI` in the game directory, the game reads that
file's `[Settings]`, `[Tunnel]`, and per-player sections and starts the
scenario it names without showing the menus. When that match ends the game
closes instead of returning to the menu, so the launcher can start the next
one.

The file is the spawner format the CnCNet-style clients write for Vinifera and
ts-patches. It names the map, the game options, every player's handle, side,
color, difficulty, starting alliances, and spawn point, and the local player;
a single-player campaign, a skirmish, or a multiplayer match may be requested.
A multiplayer match plays over the engine's own UDP transport, talking straight
to the players the file names or through a CnCNet tunnel when a `[Tunnel]`
section is present, and the lobby is skipped entirely.

Starting a game this way is opt-in: without `-SPAWN` the menus behave as they
always have, and a stray `SPAWN.INI` is ignored.

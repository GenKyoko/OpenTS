---
title: Skip the startup cinematics in a spawned session
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:spawn
  effect: changed
credit: [OpenTS contributors]
---

A match started with `-SPAWN` no longer plays the startup cinematics
first. The logo, the first-run and title sequences, and the Firestorm
title movie are skipped, so the game goes straight from its
initializations into the scenario `SPAWN.INI` names. An ordinary launch
still plays them, and the cinematics remain available from the menu.

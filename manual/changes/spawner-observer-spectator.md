---
title: Honor the spawner's spectator marker in skirmish
category: fix
release: 0.2.0
targets: []
credit: [OpenTS contributors]
---

The launcher's `IsSpectator` marker on a slot was parsed and stored but
never applied, so a spectator in a spawned skirmish played as a normal
side. A spawned human slot marked as a spectator now becomes an
observer: the scenario does not field a side for it (no starting units
are created for it and its spawn point is left free for the players),
the whole map stays revealed and the fog of
war never hides the fighting from the player, sensing a cloaked or
subterranean unit raises no radar ping or warning, and the house is
never defeated, is left out of the end-of-match survivor count and off
the score list, so the match resolves among the sides that actually
fight. Release builds and sessions without a spectator are unaffected;
the marker is still ignored outside skirmish.

---
title: Let the music keep its place across a focus loss
category: fix
release: 0.2.0
targets: []
credit: [OpenTS contributors]
---

Switching to another application no longer restarts the music. Losing
the window's focus stopped the sound and queued the playing theme to
begin again, so every return to the game started the track from its
first note. The mixer is silenced instead while the window is away, and
the theme picks up where it left off when the focus comes back.

---
title: Keep the game running while the window is unfocused
category: feature
release: 0.2.0
targets: []
credit: [OpenTS contributors]
---

The game no longer freezes when another window takes the input focus.
The original engine parked its whole simulation while unfocused, because
an exclusive full-screen display and sound device had to be handed back
before the desktop could be used. The simulation now keeps running in
every mode, and a windowed game keeps drawing as usual behind whatever
holds the focus.

What does pause the drawing is minimising the window, which leaves
nothing to paint into, and losing the focus in full screen, where the
display mode has been handed back to the desktop. The simulation carries
on underneath, and bringing the window back redraws it in full. The
sound still silences itself while the window is unfocused, no new effect
starts there, and the music keeps its place.

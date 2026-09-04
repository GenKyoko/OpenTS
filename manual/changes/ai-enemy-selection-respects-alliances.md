---
title: Let the computer attack houses outside its own alliances
category: fix
release: 0.2.0
targets: []
credit: [OpenTS contributors]
---

The computer picks the house it will fight by distance, and the pick never
excluded its own allies. In a match that begins with alliances between the
computer players, every grudge the computer built up landed on the ally next
door, its list of enemies never grew past nothing, and the teams on either
side of the alliance line never started attacking. The pick now considers
only houses that are neither allied nor spectating.

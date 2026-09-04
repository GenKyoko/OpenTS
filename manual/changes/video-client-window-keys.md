---
title: Honor the video window keys ts-patches launchers write
category: feature
release: 0.2.0
targets:
- type: key
  id: Video.Windowed
  effect: added
- type: key
  id: Video.WindowedScreenWidth
  effect: added
- type: key
  id: Video.WindowedScreenHeight
  effect: added
- type: key
  id: NoWindowFrame
  effect: added
credit: [OpenTS contributors]
---

A CnCNet-style launcher can now control how the game opens its window
through the same keys it writes into `sun.ini` for ts-patches. A
`Video.Windowed=yes` under `[Video]` opens the game in a window for that
run, `Video.WindowedScreenWidth` and `Video.WindowedScreenHeight` size
that window, and `NoWindowFrame=yes` removes its border and title bar.

The reads are compatibility only. A window size the native `WindowWidth`
and `WindowHeight` already name wins over the launcher's, and the window
request and the frameless choice belong to the run that made them, so
neither is written back over the player's stored preference. The
DirectDraw-only keys ts-patches reads around these, such as the
`[Win8Compat]` group, still have no meaning here: the game presents
through bgfx and never uses DirectDraw, so nothing reads them.

A launcher that drives its window through cnc-ddraw gets the same
treatment through `ddraw.ini`: when that file is present, its
`[ddraw] fullscreen`, `width` and `height`, and `border` entries decide
the window the same way the `[Video]` keys do, so a session the launcher
runs windowed stays windowed without a DirectDraw wrapper being involved.

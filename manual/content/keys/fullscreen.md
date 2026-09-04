---
key: Fullscreen
summary: Whether the game covers the whole screen instead of running in a resizable window.
when_omitted:
  kind: value
  value: "yes"
---

A full-screen game opens a borderless window the size of the desktop. A windowed game opens an ordinary framed window that can be moved, resized, and maximized. Neither one changes the desktop's own resolution: the game always renders at [`ScreenWidth`](/keys/screenwidth/) by [`ScreenHeight`](/keys/screenheight/) and that picture is scaled into whichever window it has, so alt-tabbing away and back does not disturb the rest of the desktop.

This setting is read before the window is created, well before the rest of `SUN.INI`, and it is written back whenever the game saves its options.

The [`-WIN`](/using/command-line/windowed/) command line option asks for a window regardless of what this setting says. It applies to that run only and is never written back, so a launcher can offer a window without disturbing the player's own preference.

A CnCNet-style launcher for the ts-patches client set asks the same way through `sun.ini`. It writes `Video.Windowed=yes` under `[Video]` before starting the game; the game opens in a window for that run and, like `-WIN`, does not write the request back. When the launcher also writes `Video.WindowedScreenWidth` and `Video.WindowedScreenHeight`, they take the place of [`WindowWidth`](/keys/windowwidth/) and [`WindowHeight`](/keys/windowheight/) for that window.

[`WindowWidth`](/keys/windowwidth/) and [`WindowHeight`](/keys/windowheight/) size the window when this setting is off. They are ignored while the game is full screen.

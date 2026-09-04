---
title: Close the main window as a quick exit
category: feature
release: 0.2.0
targets: []
credit: [OpenTS contributors]
---

Closing the main window is a request to end the run. A session the
launcher started with `-SPAWN` answers it mid-match by sending the same
exit the options menu's abort sends, so the match resolves through the
event system -- the other players see the player leave and the scores
are settled -- and the network and the libraries wind down before the
clean exit. Any other close is a quick exit at once. The process reports
a successful exit either way, which is what a launcher that starts the
next match expects.

The title-bar close button and Alt-F4 now take this same request; they
were ignored before. The shutdown sweep still belongs to the menu's own
exit path and is never run out from under a live window, so closing
cannot tear down state the game is still inside. No public interface,
setting, or file format changes.

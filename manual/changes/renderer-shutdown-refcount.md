---
title: Let the game leave without a crash report
category: fix
release: 0.2.0
targets: []
credit: [OpenTS contributors]
---

Ending a run no longer produces a crash report. The renderer counts the
references its objects carry and reports any it does not expect, and a
layer injected into the window -- an overlay, say -- legitimately holds
one of its own, so the report arrived on every exit of a debug build and
the engine treated it as unrecoverable, ending the process with an error.
A report raised while the engine is shutting the renderer down is now
written to the debug log instead, and the run ends normally. A renderer
error raised while starting up or while playing is still fatal.

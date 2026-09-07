---
title: Add OpenTS.ini and OpenTS.mix data override loading
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:ini-dir
  effect: added
- type: command
  id: launch:mix-dir
  effect: added
- type: key
  id: INIDir
  scope: client settings
  effect: added
- type: key
  id: MIXDir
  scope: client settings
  effect: added
credit: [TODO: name the author]
---

The engine reads two new files from the game directory: `OpenTS.ini`, its own configuration, and `OpenTS.mix`, an override archive. Files packed in `OpenTS.mix` take precedence over every other MIX archive, because the archive is registered ahead of the patch and expand mixes. `OpenTS.ini` is read before any other configuration and currently carries two settings in its `[General]` section: `INIDir=` and `MIXDir=` name the fallback folders (semicolon separated) the game searches for INI files and MIX archives the game directory does not have. Each setting supplies only its own kind of file.

The game directory keeps the highest priority for loose files: a file the game directory has is always taken from there, and the fallback folders are only searched for files it does not have. MIX archives loaded from the game directory keep their usual precedence over the fallback folders. The `-INIDIR=` and `-MIXDIR=` launch options set the same search paths and override `OpenTS.ini` when both are given.

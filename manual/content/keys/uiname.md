---
key: UIName
label: Localized user interface name
see_also: [Name]
when_omitted:
  kind: value
  value: ""
---

Names the localization key that supplies the text shown for this object type in the user interface — selection descriptions, production entries, and every other place the type's name is displayed. The tag applies to every type built on the object type base: aircraft, buildings, infantry, units, and the lesser types beside them.

The value is looked up through the localization tables — the JSON chains listed in UI.INI's `[Localization]` section, plus the map's own chain — with the language pass from SUN.INI's `[Localization] Language` setting applied first. A key no table provides is shown as `MISSING:<key>`, mirroring every other localization lookup.

When the tag is absent or empty, the type keeps its literal [`Name`](/keys/name) text, unchanged.

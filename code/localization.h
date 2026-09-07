/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/
#pragma once

/***********************************************************************************************
 * Localization system.
 *
 * Localization content lives in JSON files loaded through jsonio (CCFileClass backed).
 * Two chains of documents are maintained: the fixed chain, loaded once at startup from
 * the file list in UI.INI's [Localization] section, and the map chain, loaded when a
 * scenario starts from the [Localization] section of the map file itself and unloaded
 * when another scenario starts. A key is looked up in the map chain first (later files
 * of a chain win), then the fixed chain; a key that no document provides is answered
 * with "MISSING:<key>", mirroring the Fetch_String style.
 *
 * Fetch_String (the Language.dll text) is untouched; Localize is the dedicated entry
 * point for JSON-sourced text, and Localize_Wide is its UTF-16 variant for the TTF
 * wide text drawing path (Wide_Text_Print).
 *
 * JSON file format (the agreed localization format):
 *
 *   - Encoding: UTF-8 text, with or without BOM (the BOM is stripped before parse).
 *   - Root: must be a JSON object; a file with any other root is skipped whole.
 *   - Values: must be JSON strings; any other type is treated as not provided and
 *     the lookup falls through to later files (and finally to the MISSING answer).
 *     The empty string is a legal value.
 *   - Keys, flat style (recommended):
 *         "mission.briefing": "Text"
 *   - Keys, nested style (also accepted): a dotted key drills down through objects,
 *         "mission": { "briefing": "Text" }        // Localize("mission.briefing")
 *     A whole-key match always wins over the drill-down.
 *   - Comments: jsoncpp syntax, so "// ..." and "/* ... *"/ are allowed and dropped.
 *   - Character content: raw UTF-8 or "\uXXXX" escapes, both decoded by the parser;
 *     Localize_Wide converts the answer to UTF-16 for the wide text path.
 *   - Loading: "Files = a.json, b.json" lists, resolved through the game file
 *     system and loaded in order; later files win when the same key repeats, and
 *     the map chain wins over the fixed chain.
 *   - Language: the game language region code comes from SUN.INI's
 *     [Localization] Language tag (read by OptionsClass::Load_Settings, saved
 *     back by Save_Settings), for example "zh_cn". The [Options] Translation
 *     tag cannot be used -- the CnCNet client owns it and rewrites it on every
 *     launch. Lookups then try the language variant first -- "<key>.<region>"
 *     as a flat key, or "<region>" as the innermost child of a dotted key --
 *     across the whole chain, and only fall back to the default, unsuffixed
 *     entry when no language variant exists. An empty code disables the
 *     language pass.
 ***********************************************************************************************/

/*
** Reads UI.INI's [Localization] file list and loads the fixed chain. Called once
** during game initialization; safe to call again (the chain is rebuilt).
*/
void Init_Localization(void);

/*
** Starts a new map chain: the current map chain is unloaded, then the [Localization]
** section of the given map file (a scenario .map or campaign file in INI form) lists
** the JSON files to load, comma separated. A map without the section just leaves the
** map chain empty.
*/
void Load_Map_Localization(char const * map_filename);

/*
** Unloads the map chain. The fixed chain stays resident.
*/
void Unload_Map_Localization(void);

/*
** Selects the region code used by the language pass of Localize ("zh_cn" etc.).
** An empty string (or NULL) disables the language pass, so lookups only see the
** default, unsuffixed entries. See the file format notes above.
*/
void Set_Localization_Language(char const * language);

/*
** Returns the currently selected region code ("" when none). The pointer stays
** valid until the next Set_Localization_Language call.
*/
char const * Get_Localization_Language(void);

/*
** Looks the key up in the map chain, then the fixed chain, and returns the text.
** The returned pointer stays valid until the next localization call. A key that no
** document provides is answered with "MISSING:<key>".
*/
char const * Localize(char const * key);

/*
** Wide text variant for the TTF drawing path: Localize followed by a UTF-8 to
** UTF-16 conversion (invalid sequences fall back to a byte-wise widening). The
** returned pointer stays valid until the next localization call. Feed it straight
** into Wide_Text_Print.
*/
wchar_t const * Localize_Wide(char const * key);

/*
** Legacy compatibility exit: converts an old TXT_* resource id (language/language.h)
** into its key and runs the normal string lookup. Only call sites that receive a
** runtime id from an older interface use this; new code passes the key string
** directly to Localize.
*/
char const * Localize(int txt_id);

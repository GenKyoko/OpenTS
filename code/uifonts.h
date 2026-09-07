/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

#pragma once

class FontClass;

/***********************************************************************************************
 * UI.INI TrueType font registry.
 *
 * UI.INI carries a TTF registry: each entry of the [TTFFonts] section names one TrueType
 * file relative to the game directory, and the [UI] DefaultFont key lists registry names
 * as a comma separated fallback chain for the wide (Unicode) text path -- the first face
 * of the chain that carries a glyph for a character draws it.
 ***********************************************************************************************/

/*
** Reads UI.INI (when present) and sets up the wide text path's font chain from the
** [TTFFonts] registry and the [UI] DefaultFont list. Safe to call more than once;
** missing files and entries are skipped.
*/
void Init_UI_Fonts(void);

/*
** Returns the TTF replacement of a legacy bitmap font, or null when UI.INI carries no
** [FontReplacements] entry for the given legacy font name (6PT_GRAD and friends) or the
** configured face cannot be opened. The pixel height comes from the replaced font so
** line metrics stay identical. The returned FontClass is cached and stays valid for the
** rest of the process; call sites treat it exactly like any other font.
*/
FontClass * Fetch_TTF_Font_Replacement(char const * legacy_name, int pixel_height);

/*
** Draws the wide text path's test sample (Chinese and English mixed) into the lower
** left corner of the logical surface. A no-op when there is no logical surface.
*/
void Paint_Wide_Text_Sample(void);

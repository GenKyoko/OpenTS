/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#pragma once

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
** Draws the wide text path's test sample (Chinese and English mixed) into the lower
** left corner of the logical surface. A no-op when there is no logical surface.
*/
void Paint_Wide_Text_Sample(void);

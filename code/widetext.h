/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

#pragma once

#include "dialog.h"

#include "color.hh"
#include "dialog.hh"
#include "point.h"

#include <cstdint>
#include <string>
#include <vector>

class Surface;
class ColorScheme;
template<class T> class TRect;
typedef TRect<int> Rect;

/***********************************************************************************************
 * Wide (UTF-16) text shaping and printing.
 *
 * The classic text path (Simple_Text_Print and friends) draws through the embedded bitmap
 * fonts, which only carry 8-bit glyph sets. The wide text path is the alternative for text
 * that has to be able to reach the whole Unicode repertoire: it shapes UTF-16 text into a
 * glyph run and rasterizes the glyphs through the operating system's font engine.
 *
 * Shaping has two selectable backends:
 *
 *   - When the build defines OPENTS_WITH_HARFBUZZ the run is shaped with HarfBuzz, which
 *     resolves ligatures, joining forms and glyph substitutions into real font glyph ids.
 *     This requires a HarfBuzz build with the GDI font backend (hb-gdi), which is the
 *     default when building HarfBuzz on Windows.
 *
 *   - Without HarfBuzz the shaper passes every UTF-16 code point through unchanged and the
 *     rasterizer measures and draws each one individually. Latin text lays out identically
 *     to the HarfBuzz path; complex scripts fall back to per code point drawing.
 *
 * Glyphs are rasterized with GDI (GetGlyphOutlineW) as 0..64 coverage maps and drawn into
 * the paletted surface through the scheme's converter, so both 8 and 16 bit surfaces work.
 ***********************************************************************************************/

/*
** One shaped glyph. When HarfBuzz shaped the run, Glyph is the font's glyph id and
** GlyphIndex is true; otherwise Glyph is the source code point. The horizontal advance
** and the vertical placement come from the rasterizer at draw time, keeping the metrics
** and the pixel data sourced from the same font engine.
*/
struct WideGlyph {
	int Cluster;		// Index of the UTF-16 unit that produced this glyph.
	uint32_t Glyph;		// Font glyph id (shaped) or source code point (pass-through).
	bool GlyphIndex;	// True when Glyph names a font glyph rather than a code point.
	int StartFace;		// First chain face to try when rasterizing this glyph.
};

/*
** Shapes UTF-16 text into the glyph run that Wide_Text_Print consumes. Returns an empty
** run for a null or empty text.
*/
std::vector<WideGlyph> Shape_Text(wchar_t const * text, TextPrintType flag);

/*
** Measures the pixel width the given text occupies when drawn on the wide text path.
** This is the sum of the rasterizer's per glyph advances, so it matches what printing
** the text produces.
*/
int Wide_Text_Pixel_Width(wchar_t const * text, TextPrintType flag);

/*
** The pixel height of the system font that backs the wide text path for a print style.
** Mirrors the point sizes of the embedded fonts.
*/
int Wide_Text_Pixel_Height(TextPrintType flag);

/*
** Font chain selection for the wide text path.
**
** The path walks the chain of TrueType files: every character is drawn by the first
** face in the chain that carries a glyph for it, which is how a single run mixes, say,
** a Latin face with a CJK one. An empty chain falls back to the Windows system fonts.
*/
void Wide_Text_Set_Font_Chain(std::vector<std::string> const & ttf_paths);

/*
** Opens one extra FreeType face over a TTF file, for the TtfFontClass replacements of the
** legacy bitmap fonts. The face is loaded through the game file system (game directory or
** mixfiles) and lives independently of the wide text chain. Returns a handle to pass to
** Wide_Text_Release_Face, or null when the file is missing or cannot be opened.
*/
void * Wide_Text_Acquire_Face(char const * ttf_path);

/*
** Releases a face acquired through Wide_Text_Acquire_Face. Null is ignored.
*/
void Wide_Text_Release_Face(void * face);

/*
** Prints UTF-16 text onto the surface with the same styling conventions as
** Simple_Text_Print: TPF_CENTER and TPF_RIGHT align the run, the shadow flags pick the
** shadow style, and TPF_BRIGHT_COLOR lifts the foreground to the scheme's bright color.
**
** Glyphs are drawn on a monospaced grid -- a half width cell for ordinary text and a
** double width cell for CJK -- with each glyph centered in its cell, and the glyph
** edges are antialiased through the scheme's color ramp.
**
** The fore argument is a palette index used directly as the foreground color; pass a
** negative value to take the scheme's normal color together with the ramp antialiasing.
** The back argument fills the glyph background when it is a positive palette index;
** zero or negative draws on the transparent surface.
*/
Point2D Wide_Text_Print(wchar_t const * text, Surface & surface, Rect const & rect, Point2D const & pt, ColorScheme * scheme, int back, TextPrintType flag, int fore);

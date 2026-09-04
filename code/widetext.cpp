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

/***********************************************************************************************
 * Wide (UTF-16) text shaping and printing.
 *
 * See widetext.h for the overview. This file holds the two shaping backends (HarfBuzz when
 * compiled in, code point pass-through otherwise), the FreeType glyph rasterizer with its
 * cache, and the drawing loop that writes the glyphs into the game's paletted surfaces.
 ***********************************************************************************************/

#include "always.h"

#include "widetext.h"

#include "convert.h"
#include "dbgprint.h"
#include "gadget.h"
#include "lightcon.h"
#include "scheme.h"
#include "surface.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <windows.h>

#include <map>
#include <string>
#include <utility>

#ifdef OPENTS_WITH_HARFBUZZ
#include <hb-ft.h>
#endif

namespace {

	/*
	** FreeType renders glyphs as 8 bit coverage maps running from 0 to 255. The
	** monospaced drawing turns a coverage into either the palette color walking the
	** scheme's ramp (antialiased foreground) or compares it against a hard threshold
	** (shadows).
	*/
	int const GRAY_LEVELS = 256;
	int const GRAY_THRESHOLD = 128;

	/*
	** The rasterizer holds one FreeType face per entry of the active font chain. The
	** chain defaults to Windows system fonts -- a western face for ordinary text and a
	** CJK face for code points a western font cannot map -- and is replaced by the TTF
	** files registered through the UI.INI registry. Which face draws a code point is
	** decided per glyph: the first face of the chain that carries a glyph for it wins.
	*/
	FT_Library FreeTypeLibrary = NULL;
	bool FreeTypeReady = false;

	struct ChainFace {
		FT_Face Face;
		int PixelSize;
	};
	std::vector<ChainFace> Chain;
	int CachedHeight = 0;
	int CachedAscent = 0;

	int FaceGeneration = 0;
	int CachedGeneration = -1;
	bool RasterizerLogged = false;

	/*
	** One rasterized glyph: a gray coverage map plus the metrics the drawing loop needs.
	** The Gray buffer holds one byte per pixel without padding, FreeType rendered.
	*/
	struct GlyphBitmap {
		int Width = 0;
		int Height = 0;
		int Top = 0;        // Rows between the font ascent and the bitmap's top row.
		int Advance = 0;    // Horizontal pen advance for the glyph in pixels.
		bool Valid = false;
		std::vector<unsigned char> Gray;
	};

	/*
	** Rasterized glyph cache. Cleared whenever the selected face changes, since the
	** cached bitmaps belong to the face they were drawn with.
	*/
	std::map<uint64_t, GlyphBitmap> GlyphCache;

	/*
	** The monospaced cell widths (half and full width) per font size, cleared together
	** with the glyph cache on a face change.
	*/
	std::map<std::pair<int, bool>, int> FixedCache;

	bool Ensure_FreeType(void)
	{
		if (!FreeTypeReady) {
			FreeTypeReady = (FT_Init_FreeType(&FreeTypeLibrary) == 0);
			if (!FreeTypeReady) {
				DebugString("WideText: FreeType initialization failed\n");
			}
		}
		return FreeTypeReady;
	}

	/*
	** Brings every chain face to the requested pixel size. FreeType sizes are a per
	** face property, and text of several sizes may be drawn within one run.
	*/
	void Acquire_Chain(int pixel_height)
	{
		for (ChainFace & entry : Chain) {
			if (entry.PixelSize != pixel_height) {
				FT_Set_Pixel_Sizes(entry.Face, 0, pixel_height);
				entry.PixelSize = pixel_height;
			}
		}
		if (!Chain.empty() && Chain[0].Face->size != NULL) {
			CachedAscent = Chain[0].Face->size->metrics.ascender >> 6;
		}
		CachedHeight = pixel_height;
		CachedGeneration = FaceGeneration;
	}

	/*
	** Copies a rendered glyph slot into the cached bitmap record. FreeType coverage
	** values run from 0 to 255, one byte per pixel without row padding.
	*/
	bool Fill_From_Slot(FT_GlyphSlot slot, GlyphBitmap & bitmap)
	{
		if (slot == NULL || slot->format != FT_GLYPH_FORMAT_BITMAP) {
			return(false);
		}
		bitmap.Width = (int)slot->bitmap.width;
		bitmap.Height = (int)slot->bitmap.rows;
		bitmap.Top = slot->bitmap_top;
		bitmap.Advance = (int)((slot->advance.x + 32) >> 6);
		bitmap.Valid = true;
		if (bitmap.Width > 0 && bitmap.Height > 0) {
			bitmap.Gray.resize(bitmap.Width * bitmap.Height);
			int pitch = slot->bitmap.pitch;
			for (int row = 0; row < bitmap.Height; ++row) {
				unsigned char const * source = slot->bitmap.buffer + row * pitch;
				memcpy(&bitmap.Gray[row * bitmap.Width], source, bitmap.Width);
			}
		} else {
			bitmap.Gray.clear();
		}
		return(true);
	}

	bool Is_CJK(wchar_t ch)
	{
		return (ch >= 0x2E80 && ch <= 0x9FFF) ||
			(ch >= 0xA960 && ch <= 0xA97F) ||
			(ch >= 0xAC00 && ch <= 0xD7FF) ||
			(ch >= 0xF900 && ch <= 0xFAFF) ||
			(ch >= 0xFF00 && ch <= 0xFFEF);
	}

	/*
	** Rasterizes one glyph of the first chain face by glyph id -- the ids HarfBuzz
	** produces belong to the face its shaping ran against. Cached per glyph id and
	** font size, since the same text tends to be drawn every frame.
	*/
	GlyphBitmap const & Rasterize_GlyphID(int pixel_height, uint32_t glyph_id)
	{
		uint64_t key = (1ULL << 56) | ((uint64_t)glyph_id << 24) | ((uint64_t)pixel_height << 8);
		auto found = GlyphCache.find(key);
		if (found != GlyphCache.end()) {
			return found->second;
		}

		Acquire_Chain(pixel_height);

		GlyphBitmap bitmap;
		if (!Chain.empty() && Chain[0].Face != NULL) {
			FT_Face face = Chain[0].Face;
			if (Chain[0].PixelSize != pixel_height) {
				FT_Set_Pixel_Sizes(face, 0, pixel_height);
				Chain[0].PixelSize = pixel_height;
			}
			if (FT_Load_Glyph(face, (FT_UInt)glyph_id, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) == 0) {
				Fill_From_Slot(face->glyph, bitmap);
			}
		}
		auto inserted = GlyphCache.emplace(key, std::move(bitmap));
		return inserted.first->second;
	}

	/*
	** Rasterizes one code point through the face chain, starting at the given chain
	** position: the first face carrying a glyph for the code point wins, and its chain
	** position is reported back so the fixed cell width can be measured against the
	** same face. A null bitmap with face_used -1 means no face of the chain could map
	** the code point.
	*/
	GlyphBitmap const & Rasterize_Chain(int pixel_height, wchar_t ch, int start_face, int & face_used)
	{
		if (start_face < 0) {
			start_face = 0;
		}

		Acquire_Chain(pixel_height);
		for (int face_index = start_face; face_index < (int)Chain.size(); ++face_index) {
			FT_Face face = Chain[face_index].Face;
			if (face == NULL) {
				continue;
			}
			if (Chain[face_index].PixelSize != pixel_height) {
				FT_Set_Pixel_Sizes(face, 0, pixel_height);
				Chain[face_index].PixelSize = pixel_height;
			}

			/*
			** A zero glyph index means the face carries no glyph for the code point,
			** which sends the character to the next face of the chain.
			*/
			FT_UInt glyph = FT_Get_Char_Index(face, (FT_ULong)ch);
			if (glyph == 0) {
				continue;
			}
			if (FT_Load_Glyph(face, glyph, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
				continue;
			}

			GlyphBitmap bitmap;
			if (Fill_From_Slot(face->glyph, bitmap)) {
				face_used = face_index;
				auto inserted = GlyphCache.emplace(
					((uint64_t)face_index << 40) | ((uint64_t)(uint16_t)ch << 24) | ((uint64_t)pixel_height << 8),
					std::move(bitmap));
				return inserted.first->second;
			}
		}

		face_used = -1;
		static GlyphBitmap const missing;
		return(missing);
	}

	/*
	** The wide text path draws every glyph on a monospaced grid, matching the fixed
	** cell look of the embedded bitmap fonts: a half width cell for ordinary text and
	** a double width cell for CJK, so mixed lines stay aligned without overlap. The
	** cell widths come from representative characters of the selected face ('M' and
	** the CJK ideograph for "middle"), falling back to a size derived estimate when
	** the face cannot map them.
	*/
	int Fixed_Advance(int pixel_height, bool wide)
	{
		auto found = FixedCache.find({pixel_height, wide});
		if (found != FixedCache.end()) {
			return found->second;
		}

		/*
		** Measure the representative character through the chain, so the cell width
		** comes from the face that actually renders the ordinary text.
		*/
		int used = 0;
		GlyphBitmap const & base = Rasterize_Chain(pixel_height, L'M', 0, used);
		GlyphBitmap const & full = Rasterize_Chain(pixel_height, 0x4E2D, 0, used);

		int half = (base.Valid && base.Advance > 0) ? base.Advance : pixel_height / 2 + 2;
		int fullwidth = (full.Valid && full.Advance > 0) ? full.Advance : half * 2;
		if (fullwidth < half) {
			fullwidth = half;
		}

		int step = wide ? fullwidth : half;
		FixedCache[{pixel_height, wide}] = step;
		return step;
	}

	/*
	** Writes one pixel of the given palette color into a locked surface row.
	*/
	void Write_Pixel(unsigned char * row, int x, int bbp, int color, ConvertClass const & converter)
	{
		if (bbp == 1) {
			row[x] = (unsigned char)converter.Convert_Pixel(color);
		} else {
			((unsigned short *)row)[x] = (unsigned short)converter.Convert_Pixel(color);
		}
	}

	/*
	** Blits one glyph coverage map onto the locked surface, offset by the shadow or
	** foreground placement, with per pixel clipping against the print rectangle.
	**
	** When ramp is non-null the coverage walks the scheme's sixteen entry color ramp
	** (palette indexes 16..31, brightest first) for antialiased edges; when it is null
	** the plain hard threshold with the single given color is used, as for shadows.
	*/
	void Blit_Glyph(
		GlyphBitmap const & bitmap,
		unsigned char * buffer,
		int stride,
		int bbp,
		int x,
		int y,
		Rect const & clip,
		Rect const & surferect,
		int color,
		int const * ramp,
		ConvertClass const & converter)
	{
		if (!bitmap.Valid || bitmap.Gray.empty()) {
			return;
		}
		/*
		** FreeType coverage maps are stored unpadded -- one Width sized row right
		** behind the other -- unlike the GDI bitmaps which were dword aligned.
		*/
		int rowstride = bitmap.Width;
		for (int row = 0; row < bitmap.Height; ++row) {
			int dy = y + row;
			if (dy < clip.Y || dy >= clip.Y + clip.Height) {
				continue;
			}
			if (dy < 0 || dy >= surferect.Height) {
				continue;
			}
			unsigned char const * source = &bitmap.Gray[row * rowstride];
			unsigned char * destrow = buffer + (size_t)dy * stride;
			for (int col = 0; col < bitmap.Width; ++col) {
				int dx = x + col;
				if (dx < clip.X || dx >= clip.X + clip.Width) {
					continue;
				}
				if (dx < 0 || dx >= surferect.Width) {
					continue;
				}
				int coverage = source[col];
				if (coverage <= 0) {
					continue;
				}
				if (ramp != NULL) {
					/*
					** Antialiased pass: the coverage walks the ramp from the full
					** foreground color down to the darkest ramp entry, softening the
					** glyph edges. Coverage below an eighth of full draws nothing.
					*/
					if (coverage < GRAY_LEVELS / 8) {
						continue;
					}
					int band = coverage * 16 / GRAY_LEVELS;
					if (band > 15) {
						band = 15;
					}
					Write_Pixel(destrow, dx, bbp, ramp[15 - band], converter);
				} else {
					if (coverage >= GRAY_THRESHOLD) {
						Write_Pixel(destrow, dx, bbp, color, converter);
					}
				}
			}
		}
	}

	/*
	** Fills the background cell a glyph sits on, mirroring the background fill of the
	** embedded font path.
	*/
	void Fill_Glyph_Background(
		GlyphBitmap const & bitmap,
		unsigned char * buffer,
		int stride,
		int bbp,
		int x,
		int y,
		Rect const & clip,
		Rect const & surferect,
		int color,
		ConvertClass const & converter)
	{
		if (!bitmap.Valid || bitmap.Width <= 0 || bitmap.Height <= 0) {
			return;
		}
		int fillw = bitmap.Width;
		int fillh = bitmap.Height;
		int fillx = x;
		int filly = y;
		if (fillx < clip.X) {
			fillw -= clip.X - fillx;
			fillx = clip.X;
		}
		if (filly < clip.Y) {
			fillh -= clip.Y - filly;
			filly = clip.Y;
		}
		if (fillx + fillw > clip.X + clip.Width) {
			fillw = clip.X + clip.Width - fillx;
		}
		if (filly + fillh > clip.Y + clip.Height) {
			fillh = clip.Y + clip.Height - filly;
		}
		if (fillw <= 0 || fillh <= 0) {
			return;
		}
		int pixel = converter.Convert_Pixel(color);
		for (int row = 0; row < fillh; ++row) {
			unsigned char * destrow = buffer + (size_t)(filly + row) * stride;
			for (int col = 0; col < fillw; ++col) {
				int dx = fillx + col;
				if (bbp == 1) {
					destrow[dx] = (unsigned char)pixel;
				} else {
					((unsigned short *)destrow)[dx] = (unsigned short)pixel;
				}
			}
		}
	}

#ifdef OPENTS_WITH_HARFBUZZ

	/*
	** HarfBuzz shaping backend. The run is shaped against the first face of the chain
	** through the FreeType font backend, so the resulting glyph ids rasterize through
	** the same face. Code points the first face cannot map (glyph id zero) fall back
	** to code point drawing, which walks the remaining faces of the chain at draw time.
	*/
	void Shape_With_HarfBuzz(wchar_t const * text, int pixel_height, std::vector<WideGlyph> & run)
	{
		Acquire_Chain(pixel_height);
		if (Chain.empty() || Chain[0].Face == NULL) {
			return;
		}

		hb_font_t * font = hb_ft_font_create(Chain[0].Face, NULL);
		if (font == NULL) {
			return;
		}

		hb_buffer_t * buffer = hb_buffer_create();
		hb_buffer_add_utf16(buffer, (const uint16_t *)text, -1, 0, -1);
		hb_buffer_guess_segment_properties(buffer);
		hb_shape(font, buffer, NULL, 0);

		unsigned int count = 0;
		hb_glyph_info_t * infos = hb_buffer_get_glyph_infos(buffer, &count);
		for (unsigned int index = 0; index < count; ++index) {
			WideGlyph glyph;
			glyph.Cluster = (int)infos[index].cluster;
			glyph.Glyph = infos[index].codepoint;
			glyph.GlyphIndex = (infos[index].codepoint != 0);
			glyph.StartFace = glyph.GlyphIndex ? 0 : 1;
			run.push_back(glyph);
		}

		hb_buffer_destroy(buffer);
		hb_font_destroy(font);
	}

#endif    // OPENTS_WITH_HARFBUZZ

}    // namespace


/***********************************************************************************************
 * Shape_Text -- Shapes UTF-16 text into a glyph run.                                          *
 *                                                                                             *
 *    With HarfBuzz compiled in this runs the full shaping pipeline and produces font glyph    *
 *    ids; without it every code point is passed through as itself. Either way the drawing     *
 *    loop consumes the run identically.                                       *
 *                                                                                             *
 * INPUT:   text -- The UTF-16 text to shape.                                                  *
 *          flag -- The print style whose point size selects the backing font.                 *
 *                                                                                             *
 * OUTPUT:  Returns the shaped glyph run; empty for a null or empty text.                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/04/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
std::vector<WideGlyph> Shape_Text(wchar_t const * text, TextPrintType flag)
{
	std::vector<WideGlyph> run;
	if (text == NULL) {
		return(run);
	}

	int pixel_height = Wide_Text_Pixel_Height(flag);

#ifdef OPENTS_WITH_HARFBUZZ
	Shape_With_HarfBuzz(text, pixel_height, run);
	if (!run.empty()) {
		return(run);
	}
#endif

	/*
	** Pass-through backend: every UTF-16 unit becomes one glyph carrying itself as a
	** code point. Surrogate pairs surface as their individual units and draw as the
	** rasterizer's fallback, which keeps the run trivially correct for the BMP text
	** this path exists for.
	*/
	int length = (int)wcslen(text);
	run.reserve(length);
	for (int index = 0; index < length; ++index) {
		WideGlyph glyph;
		glyph.Cluster = index;
		glyph.Glyph = (uint32_t)(uint16_t)text[index];
		glyph.GlyphIndex = false;
		glyph.StartFace = 0;
		run.push_back(glyph);
	}
	return(run);
}


/***********************************************************************************************
 * Wide_Text_Set_Font_Chain -- Replaces the wide text path's font chain.                       *
 *                                                                                             *
 *    Every entry of the chain is one TTF file path; characters are drawn by the first        *
 *    face of the chain that carries a glyph for them. An empty chain restores the            *
 *    Windows system fonts. Changing the chain invalidates the rasterized glyph cache.       *
 *                                                                                             *
 * INPUT:   ttf_paths -- The TTF file paths of the chain, ordered by priority.                *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/04/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void Wide_Text_Set_Font_Chain(std::vector<std::string> const & ttf_paths)
{
	if (!Ensure_FreeType()) {
		return;
	}

	for (ChainFace & entry : Chain) {
		if (entry.Face != NULL) {
			FT_Done_Face(entry.Face);
		}
	}
	Chain.clear();

	for (std::string const & path : ttf_paths) {
		FT_Face face = NULL;
		if (FT_New_Face(FreeTypeLibrary, path.c_str(), 0, &face) == 0 && face != NULL) {
			ChainFace entry;
			entry.Face = face;
			entry.PixelSize = -1;
			Chain.push_back(entry);
		} else {
			DebugString("WideText: failed to load font '%s'\n", path.c_str());
		}
	}

	if (Chain.empty()) {
		/*
		** No usable font was configured, so probe the Windows font directory for the
		** system default chain -- a western face plus a CJK face.
		*/
		char windir[MAX_PATH];
		UINT len = GetWindowsDirectoryA(windir, sizeof(windir));
		if (len > 0 && len < sizeof(windir)) {
			std::string base = windir;
			base += "\\Fonts\\";
			char const * candidates[] = { "arial.ttf", "msyh.ttc", "simhei.ttf", "simsun.ttc" };
			for (char const * candidate : candidates) {
				std::string path = base + candidate;
				FT_Face face = NULL;
				if (FT_New_Face(FreeTypeLibrary, path.c_str(), 0, &face) == 0 && face != NULL) {
					ChainFace entry;
					entry.Face = face;
					entry.PixelSize = -1;
					Chain.push_back(entry);
					DebugString("WideText: default face '%s' loaded\n", path.c_str());
				}
			}
		}
	}

	FaceGeneration++;
	GlyphCache.clear();
	FixedCache.clear();
	RasterizerLogged = false;
}


/***********************************************************************************************
 * (reserved)                                                                                  *
 *                                                                                             *
 * INPUT:   none                                                                              *
 *                                                                                             *
 * OUTPUT:  none                                                                              *
 *                                                                                             *
 * WARNINGS:   none                                                                           *
 *                                                                                             *
 * HISTORY:                                                                                   *
 *   09/04/2026 OpenTS : Created.                                                             *
 *=============================================================================================*/
void Wide_Text_Clear_Font_Faces_obsolete(void)
{
}


/***********************************************************************************************
 * (reserved)                                                                                  *
 *                                                                                             *
 * INPUT:   face -- The family name to append.                                                *
 *                                                                                             *
 * OUTPUT:  none                                                                              *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                   *
 *   09/04/2026 OpenTS : Created.                                                             *
 *=============================================================================================*/
void Wide_Text_Add_Font_Face_obsolete(wchar_t const * face)
{
}


/***********************************************************************************************
 * Wide_Text_Pixel_Height -- Fetches the backing system font height for a print style.        *
 *                                                                                             *
 *    Mirrors the point sizes of the embedded fonts so wide text matches the size of the      *
 *    classic text drawn beside it.                                                           *
 *                                                                                             *
 * INPUT:   flag -- The print style.                                                          *
 *                                                                                             *
 * OUTPUT:  Returns the pixel height to rasterize the system font at.                         *
 *                                                                                             *
 * WARNINGS:   none                                                                           *
 *                                                                                             *
 * HISTORY:                                                                                   *
 *   09/04/2026 OpenTS : Created.                                                             *
 *=============================================================================================*/
int Wide_Text_Pixel_Height(TextPrintType flag)
{
	switch (flag & (TextPrintType)0x000F) {
		case TPF_6POINT:
		case TPF_6PT_GRAD:
		case TPF_3POINT:
			return(12);

		case TPF_METAL12:
			return(20);

		default:
			return(16);
	}
}


/***********************************************************************************************
 * Wide_Text_Pixel_Width -- Measures the text on the wide text path.                           *
 *                                                                                             *
 *    Sums the monospaced cell widths over the shaped run, which is exactly the width the     *
 *    printer produces.                                                                       *
 *                                                                                             *
 * INPUT:   text -- The UTF-16 text to measure.                                               *
 *          flag -- The print style.                                                          *
 *                                                                                             *
 * OUTPUT:  Returns the width in pixels.                                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                           *
 *                                                                                             *
 * HISTORY:                                                                                   *
 *   09/04/2026 OpenTS : Created.                                                             *
 *=============================================================================================*/
int Wide_Text_Pixel_Width(wchar_t const * text, TextPrintType flag)
{
	int width = 0;
	int pixel_height = Wide_Text_Pixel_Height(flag);
	std::vector<WideGlyph> run = Shape_Text(text, flag);
	for (size_t index = 0; index < run.size(); ++index) {
		wchar_t ch = (text != NULL) ? text[run[index].Cluster] : (wchar_t)run[index].Glyph;
		width += Fixed_Advance(pixel_height, Is_CJK(ch));
	}
	return(width);
}


/***********************************************************************************************
 * Wide_Text_Print -- Prints UTF-16 text onto the surface.                                     *
 *                                                                                             *
 *    The wide alternative to Simple_Text_Print: shapes the text, aligns it, then draws       *
 *    the glyph run with the scheme's colors and the requested shadow style.                  *
 *                                                                                             *
 * INPUT:   text    -- The UTF-16 text to display.                                            *
 *          surface -- The surface to display the text upon.                                  *
 *          rect    -- The clipping rectangle that both clips and biases the print position.  *
 *          pt      -- The draw position for the upper left of the first glyph cell.          *
 *          scheme  -- The color scheme; the default gadget scheme is used when null.         *
 *          back    -- Background palette index, or negative for a transparent background.    *
 *          flag    -- The print style (alignment, shadow, brightness).                       *
 *          fore    -- Foreground palette index, or negative for the scheme's normal color.   *
 *                                                                                             *
 * OUTPUT:  Returns the point where the next print should begin to continue this line.        *
 *                                                                                             *
 * WARNINGS:   none                                                                           *
 *                                                                                             *
 * HISTORY:                                                                                   *
 *   09/04/2026 OpenTS : Created.                                                             *
 *=============================================================================================*/
Point2D Wide_Text_Print(wchar_t const * text, Surface & surface, Rect const & rect, Point2D const & pt, ColorScheme * scheme, int back, TextPrintType flag, int fore)
{
	Point2D point = pt;

	if (scheme == NULL) {
		scheme = Fetch_Scheme_By_Name(DEFAULT_GADGET_SCHEME);
	}

	int pixel_height = Wide_Text_Pixel_Height(flag);
	std::vector<WideGlyph> run = Shape_Text(text, flag);

	/*
	** Measure the run on the monospaced grid and settle the pen start position for the
	** requested alignment.
	*/
	int total = 0;
	int valid = 0;
	for (size_t index = 0; index < run.size(); ++index) {
		wchar_t ch = (text != NULL) ? text[run[index].Cluster] : (wchar_t)run[index].Glyph;
		total += Fixed_Advance(pixel_height, Is_CJK(ch));
		if (run[index].GlyphIndex) {
			if (Rasterize_GlyphID(pixel_height, run[index].Glyph).Valid) {
				valid++;
			}
		} else {
			int used = 0;
			if (Rasterize_Chain(pixel_height, ch, run[index].StartFace, used).Valid) {
				valid++;
			}
		}
	}
	if (!RasterizerLogged) {
		DebugString("WideText: text drawn, glyphs %d, rasterized %d, chain %d\n",
			(int)run.size(), valid, (int)Chain.size());
		RasterizerLogged = true;
	}
	switch (flag & (TPF_CENTER|TPF_RIGHT)) {
		case TPF_CENTER:
			point.X -= total >> 1;
			break;

		case TPF_RIGHT:
			point.X -= total;
			break;

		default:
			break;
	}

	if (run.empty()) {
		return(point);
	}

	/*
	** Settle the colors: the shadow style picks the outline color, the foreground comes
	** from the explicit palette index or the scheme, and the background fills only when
	** the caller asked for one.
	*/
	int shadowcolor = -1;
	switch (flag & (TPF_NOSHADOW|TPF_DROPSHADOW|TPF_FULLSHADOW|TPF_LIGHTSHADOW)) {
		case TPF_DROPSHADOW:
			shadowcolor = BLACK;
			break;

		case TPF_FULLSHADOW:
			shadowcolor = BLACK;
			break;

		case TPF_LIGHTSHADOW:
			shadowcolor = ((14 * 16) + 7) + 1;
			break;

		default:
			break;
	}

	int forecolor = scheme->Color;
	if (flag & TPF_BRIGHT_COLOR) {
		forecolor = scheme->Bright;
	}
	if (fore >= 0) {
		forecolor = fore;
	}

	/*
	** Antialiased edges: the foreground walks the scheme's sixteen entry color ramp
	** (palette indexes 16..31, entry 16 being the full foreground color and the rest
	** shading down toward the background), so glyph edges pick a shade matching their
	** coverage. Only the default foreground uses the ramp; an explicitly chosen color
	** has no ramp to walk and falls back to the hard threshold.
	*/
	int ramp[16];
	int const * rampptr = NULL;
	if (fore < 0) {
		for (int index = 0; index < 16; ++index) {
			ramp[index] = 16 + index;
		}
		rampptr = &ramp[0];
	}

	Rect srect = surface.Get_Rect();
	Rect cliprect = rect;
	if (cliprect.X < 0 || cliprect.Y < 0 || cliprect.Width <= 0 || cliprect.Height <= 0) {
		cliprect = srect;
	}

	void * buffer = surface.Lock();
	if (buffer == NULL) {
		return(point);
	}
	int bbp = surface.Bytes_Per_Pixel();
	int stride = surface.Stride();
	ConvertClass const & converter = *scheme->Converter;

	/*
	** Draw the shadow passes first, then the foreground over them. The full shadow
	** outlines the glyph on all eight neighbors; the drop shadow sits one step down
	** and to the right.
	*/
	struct { int dx; int dy; } offsets[8];
	int offsetcount = 0;
	if (shadowcolor >= 0) {
		if ((flag & (TPF_NOSHADOW|TPF_DROPSHADOW|TPF_FULLSHADOW|TPF_LIGHTSHADOW)) == TPF_FULLSHADOW) {
			int const dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
			int const dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
			for (int index = 0; index < 8; ++index) {
				offsets[index].dx = dxs[index];
				offsets[index].dy = dys[index];
			}
			offsetcount = 8;
		} else {
			offsets[0].dx = 1;
			offsets[0].dy = 1;
			offsetcount = 1;
		}
	}

	int xpos = point.X;
	int ascent = 0;
	{
		Acquire_Chain(pixel_height);
		ascent = CachedAscent;
	}

	for (int pass = 0; pass < 2; ++pass) {
		int color;
		if (pass == 0) {
			if (shadowcolor < 0) {
				continue;
			}
			color = shadowcolor;
		} else {
			color = forecolor;
		}

		int penx = xpos;
		for (size_t index = 0; index < run.size(); ++index) {
			wchar_t ch = (text != NULL) ? text[run[index].Cluster] : (wchar_t)run[index].Glyph;
			int step = Fixed_Advance(pixel_height, Is_CJK(ch));
			GlyphBitmap const * bitmap = NULL;
			if (run[index].GlyphIndex) {
				bitmap = &Rasterize_GlyphID(pixel_height, run[index].Glyph);
			} else {
				int used = 0;
				bitmap = &Rasterize_Chain(pixel_height, ch, run[index].StartFace, used);
			}
			if (bitmap->Valid) {
				/*
				** Center the glyph inside its fixed cell, so proportional faces
				** render with an even rhythm.
				*/
				int x = penx + (step - bitmap->Width) / 2;
				int y = point.Y + ascent - bitmap->Top;
				if (pass == 0) {
					for (int offset = 0; offset < offsetcount; ++offset) {
						Blit_Glyph(*bitmap, (unsigned char *)buffer, stride, bbp,
							x + offsets[offset].dx, y + offsets[offset].dy, cliprect, srect, color, NULL, converter);
					}
				} else {
					if (back > 0) {
						Fill_Glyph_Background(*bitmap, (unsigned char *)buffer, stride, bbp,
							x, y, cliprect, srect, back, converter);
					}
					Blit_Glyph(*bitmap, (unsigned char *)buffer, stride, bbp, x, y, cliprect, srect, color, rampptr, converter);
				}
			}
			penx += step;
		}
	}

	surface.Unlock();
	return(point);
}

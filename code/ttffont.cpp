/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

/***********************************************************************************************
 * TtfFontClass implementation. See ttffont.h for the overview.
 ***********************************************************************************************/

#include "always.h"

#include "ttffont.h"

#include "_convert.h"
#include "convert.h"
#include "dbgprint.h"
#include "widetext.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace {

	/*
	** Coverage at or above this threshold draws the foreground color; below it the
	** pixel stays transparent. A hard threshold reproduces the classic bitmap look.
	** The antialiased path instead walks the scheme's ramp over the full coverage
	** range, so the threshold only applies to the shadow passes.
	*/
	int const GRAY_LEVELS = 256;
	int const GRAY_THRESHOLD = 128;

}    // namespace


/***********************************************************************************************
 * TtfFontClass::TtfFontClass -- Constructor.                                                   *
 *                                                                                             *
 *    Brings every face of the chain to the requested pixel height and records the ascent      *
 *    the drawing uses to place glyphs on the line. The ascent comes from the first face,      *
 *    which is the one ordinary text renders with.                                             *
 *                                                                                             *
 * INPUT:   ft_faces     -- FreeType face handles from Wide_Text_Acquire_Face, in fallback     *
 *                          order; owned by this instance from here on.                        *
 *          pixel_height -- Rasterization height in pixels.                                    *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
TtfFontClass::TtfFontClass(std::vector<void *> const & ft_faces, int pixel_height) :
	PixelHeight(pixel_height > 0 ? pixel_height : 12),
	Ascent(0),
	FontXSpacing(0),
	FontYSpacing(0),
	Animated(false),
	MsPerChar(120),
	FlashMs(150)
{
	for (void * handle : ft_faces) {
		if (handle != NULL) {
			Faces.push_back(handle);
			FT_Set_Pixel_Sizes((FT_Face)handle, 0, PixelHeight);
		}
	}

	FT_Face face = (!Faces.empty()) ? (FT_Face)Faces[0] : NULL;
	if (face != NULL && face->size != NULL) {
		Ascent = face->size->metrics.ascender >> 6;
	}
	if (Ascent <= 0 || Ascent > PixelHeight) {
		Ascent = PixelHeight * 3 / 4;
	}
}


/***********************************************************************************************
 * TtfFontClass::~TtfFontClass -- Destructor.                                                   *
 *                                                                                             *
 *    Releases the faces of the chain. The file buffers they read from stay with the           *
 *    acquisition side for the process lifetime.                                               *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
TtfFontClass::~TtfFontClass(void)
{
	for (void * handle : Faces) {
		FT_Done_Face((FT_Face)handle);
	}
}


/***********************************************************************************************
 * TtfFontClass::Decode_Next -- Decodes the next UTF-8 code point.                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
unsigned int TtfFontClass::Decode_Next(char const * & p)
{
	unsigned char c = (unsigned char)(*p++);
	if (c < 0x80) {
		return(c);
	}

	int extra = 0;
	unsigned int codepoint = 0;
	if ((c & 0xE0) == 0xC0) {
		extra = 1;
		codepoint = c & 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		extra = 2;
		codepoint = c & 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		extra = 3;
		codepoint = c & 0x07;
	} else {
		/*
		** A stray byte that is not part of any sequence degrades to its Latin-1
		** value, so single byte text keeps rendering.
		*/
		return(c);
	}

	for (int index = 0; index < extra; ++index) {
		unsigned char continuation = (unsigned char)(*p);
		if ((continuation & 0xC0) != 0x80) {
			return(c);
		}
		++p;
		codepoint = (codepoint << 6) | (continuation & 0x3F);
	}
	return(codepoint);
}


/***********************************************************************************************
 * TtfFontClass::Rasterize -- Rasterizes one code point, cached.                                *
 *                                                                                             *
 *    The faces are tried in chain order: the first one that carries a glyph for the code      *
 *    point and rasterizes it wins. Code points no face can map come back as an invalid        *
 *    glyph with a placeholder advance, so strings still lay out and the caller draws          *
 *    nothing for them.                                                                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
TtfFontClass::Glyph const & TtfFontClass::Rasterize(unsigned int codepoint) const
{
	auto found = Glyphs.find(codepoint);
	if (found != Glyphs.end()) {
		return(found->second);
	}

	Glyph glyph;
	if (codepoint >= ' ') {
		for (void * handle : Faces) {
			FT_Face face = (FT_Face)handle;
			FT_UInt index = FT_Get_Char_Index(face, codepoint);
			if (index == 0) {
				continue;
			}
			if (FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0
				|| face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
				continue;
			}
			FT_GlyphSlot slot = face->glyph;
			glyph.Valid = true;
			glyph.Advance = (int)((slot->advance.x + 32) >> 6);
			glyph.Width = (int)slot->bitmap.width;
			glyph.Height = (int)slot->bitmap.rows;
			glyph.Top = slot->bitmap_top;
			glyph.Gray.resize(glyph.Width * glyph.Height);
			int pitch = slot->bitmap.pitch;
			for (int row = 0; row < glyph.Height; ++row) {
				memcpy(&glyph.Gray[row * glyph.Width], slot->bitmap.buffer + row * pitch, glyph.Width);
			}
			break;
		}
	}
	if (!glyph.Valid) {
		glyph.Advance = PixelHeight / 3 + 1;
	}

	auto inserted = Glyphs.emplace(codepoint, std::move(glyph));
	return(inserted.first->second);
}


/***********************************************************************************************
 * TtfFontClass::Code_Point_Width -- Fetch the pixel width of one code point.                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::Code_Point_Width(unsigned int codepoint) const
{
	return(Rasterize(codepoint).Advance + FontXSpacing);
}


/***********************************************************************************************
 * TtfFontClass::Blit_Glyph -- Writes one glyph coverage map into the surface.                  *
 *                                                                                             *
 *    Pixels at or above the coverage threshold take the remap slot's color; everything else   *
 *    stays untouched, which is what makes the background fill show through.                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void TtfFontClass::Blit_Glyph(Glyph const & glyph, unsigned char * buffer, int stride, int bbp, int x, int y, Rect const & cliprect, Rect const & surferect, int slotcolor, int const * ramp, ConvertClass const & converter)
{
	if (!glyph.Valid || glyph.Gray.empty()) {
		return;
	}
	if (ramp == NULL && slotcolor == 0) {
		return;
	}
	for (int row = 0; row < glyph.Height; ++row) {
		int dy = y + row;
		if (dy < cliprect.Y || dy >= cliprect.Y + cliprect.Height) {
			continue;
		}
		if (dy < 0 || dy >= surferect.Height) {
			continue;
		}
		unsigned char const * source = &glyph.Gray[row * glyph.Width];
		unsigned char * destrow = buffer + (size_t)dy * stride;
		for (int col = 0; col < glyph.Width; ++col) {
			int dx = x + col;
			if (dx < cliprect.X || dx >= cliprect.X + cliprect.Width) {
				continue;
			}
			if (dx < 0 || dx >= surferect.Width) {
				continue;
			}
			int coverage = source[col];
			if (coverage <= 0) {
				continue;
			}
			int pixel;
			if (ramp != NULL) {
				/*
				** Antialiased pass: the coverage walks the scheme's ramp from the
				** full foreground color down to its darkest entry, softening the
				** glyph edges. Coverage below an eighth of full draws nothing.
				*/
				if (coverage < GRAY_LEVELS / 8) {
					continue;
				}
				int band = coverage * 16 / GRAY_LEVELS;
				if (band > 15) {
					band = 15;
				}
				pixel = converter.Convert_Pixel(ramp[15 - band]);
			} else {
				/*
				** Hard threshold pass, for the shadow passes and for callers
				** without a scheme.
				*/
				if (coverage < GRAY_THRESHOLD) {
					continue;
				}
				pixel = converter.Convert_Pixel(slotcolor);
			}
			if (bbp == 1) {
				destrow[dx] = (unsigned char)pixel;
			} else {
				((unsigned short *)destrow)[dx] = (unsigned short)pixel;
			}
		}
	}
}


/***********************************************************************************************
 * TtfFontClass::Char_Pixel_Width -- Fetch the pixel width of the character specified.          *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::Char_Pixel_Width(char c) const
{
	if ((unsigned char)c < ' ') return(0);
	return(Code_Point_Width((unsigned char)c));
}


/***********************************************************************************************
 * TtfFontClass::String_Pixel_Width -- Determines the width of the string in pixels.            *
 *                                                                                             *
 *    The string is decoded as UTF-8; line breaks stay byte level, since no multi byte          *
 *    sequence can contain them.                                                *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::String_Pixel_Width(char const * string) const
{
	if (string == NULL) return(0);

	int largest = 0;
	int width = 0;
	while (*string) {
		if (*string == '\r' || *string == '\n') {
			string++;
			largest = std::max(largest, width);
			width = 0;
		} else {
			width += Code_Point_Width(Decode_Next(string));
		}
	}
	largest = std::max(largest, width);
	return(largest);
}


/***********************************************************************************************
 * TtfFontClass::String_Pixel_Bounds -- Calculate the bounding box for the specified string.     *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void TtfFontClass::String_Pixel_Bounds(const char * string, Rect & bounds) const
{
	bounds.X = 0;
	bounds.Y = 0;
	bounds.Width = 0;
	bounds.Height = 0;

	if (string == NULL) {
		return;
	}

	int width = 0;
	int height = Get_Height();
	while (*string != 0) {
		if ((*string == '\r') || (*string == '\n')) {
			string++;
			height += Get_Height();
			bounds.Width = std::max(bounds.Width, width);
			width = 0;
		} else {
			width += Code_Point_Width(Decode_Next(string));
		}
	}

	bounds.Width = std::max(bounds.Width, width);
	bounds.Height = height;
}


/***********************************************************************************************
 * TtfFontClass::Get_Width -- Width of the nominal character.                                   *
 *                                                                                             *
 *    The widest of a small set of representative characters stands in for the bitmap          *
 *    fonts' fixed cell width.                                                                 *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::Get_Width(void) const
{
	int raw = Rasterize('M').Advance;
	if (raw <= 0) {
		raw = PixelHeight / 2 + 1;
	}
	return(raw + ((FontXSpacing > 0) ? FontXSpacing : 0));
}


/***********************************************************************************************
 * TtfFontClass::Get_Height -- Height of the font, normalized by the Y spacing.                 *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::Get_Height(void) const
{
	return(PixelHeight + ((FontYSpacing > 0) ? FontYSpacing : 0));
}


/***********************************************************************************************
 * TtfFontClass::Set_XSpacing -- Set the X spacing override value.                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::Set_XSpacing(int x)
{
	int old = FontXSpacing;
	FontXSpacing = x;
	return(old);
}


/***********************************************************************************************
 * TtfFontClass::Set_YSpacing -- Set the vertical (Y) spacing override value.                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
int TtfFontClass::Set_YSpacing(int y)
{
	int old = FontYSpacing;
	FontYSpacing = y;
	return(old);
}


/***********************************************************************************************
 * TtfFontClass::Begin_Text_Animation -- Arms the typewriter animation.                         *
 *                                                                                             *
 *    The very next Print call starts the clock for the text it was given; every later Print    *
 *    of the same text advances the reveal, and a different text restarts it. The animation     *
 *    is over as soon as the last character has appeared.                                       *
 *                                                                                             *
 * INPUT:   ms_per_char -- Milliseconds between the appearances of consecutive characters.      *
 *          flash_ms    -- How long a freshly appeared character flickers for.                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/06/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void TtfFontClass::Begin_Text_Animation(unsigned ms_per_char, unsigned flash_ms)
{
	Animated = true;
	MsPerChar = (ms_per_char > 0) ? ms_per_char : 120;
	FlashMs = flash_ms;
	AnimStarts.clear();
}


/***********************************************************************************************
 * TtfFontClass::Is_Text_Animation_Running -- Reports whether a typewriter run is still going.   *
 *                                                                                             *
 *    False before the text was printed since Begin_Text_Animation, and once its last           *
 *    character has appeared. The text is its own clock, so several runs go on at once.         *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/06/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
bool TtfFontClass::Is_Text_Animation_Running(char const * text) const
{
	if (text == NULL || !Animated) {
		return(false);
	}
	auto found = AnimStarts.find(text);
	if (found == AnimStarts.end()) {
		return(false);
	}
	unsigned elapsed = GetTickCount() - found->second;
	unsigned total = 0;
	for (char const * p = text; *p != '\0'; ) {
		Decode_Next(p);
		total++;
	}
	return(elapsed / MsPerChar < total);
}


/***********************************************************************************************
 * TtfFontClass::Text_Animation_Visible -- The reveal front of the text's typewriter run.        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/06/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
unsigned TtfFontClass::Text_Animation_Visible(char const * text) const
{
	if (text == NULL || !Animated) {
		return(0);
	}
	auto found = AnimStarts.find(text);
	if (found == AnimStarts.end()) {
		return(0);
	}
	unsigned elapsed = GetTickCount() - found->second;
	unsigned visible = elapsed / MsPerChar;
	unsigned total = 0;
	for (char const * p = text; *p != '\0'; ) {
		Decode_Next(p);
		total++;
	}
	return((visible < total) ? visible : total);
}


/***********************************************************************************************
 * TtfFontClass::Finish_Text_Animation -- Marks a typewriter run as complete.                    *
 *                                                                                             *
 *    The text's reveal clock is wound forward past its length, so its next Print draws the     *
 *    whole text at once.                                                       *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/06/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void TtfFontClass::Finish_Text_Animation(char const * text)
{
	if (text == NULL) {
		return;
	}
	unsigned total = 0;
	for (char const * p = text; *p != '\0'; ) {
		Decode_Next(p);
		total++;
	}
	AnimStarts[text] = GetTickCount() - (total + 2) * MsPerChar;
}


/***********************************************************************************************
 * TtfFontClass::Print -- Print text to the surface specified.                                  *
 *                                                                                             *
 *    Mirrors WWFontClass::Print: locked surface access, the same clipping and line breaking    *
 *    rules, and the classic remap slot conventions. The TrueType glyph replaces the bitmap,    *
 *    thresholded to a hard edge, with the shadow passes drawn by offsetting the glyph like    *
 *    the legacy outline pixels did.                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
Point2D TtfFontClass::Print(char const * string, Surface & surface, Rect const & cliprect, Point2D const & drawpoint, ConvertClass const & convertref, unsigned char const * remap, ColorScheme const * scheme) const
{
	if (string == NULL) return(drawpoint);

	static unsigned char const fontpalette[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	if (!remap) {
		remap = &fontpalette[0];
	}

	Point2D point = drawpoint;
	int xpos = Bias_To(point, cliprect).X;
	int ypos = Bias_To(point, cliprect).Y;
	int lineheight = Get_Height();

	if (xpos >= cliprect.X + cliprect.Width || ypos >= cliprect.Y + cliprect.Height) {
		return(drawpoint);
	}

	/*
	** Pick the foreground slot: slot 1 is the classic choice, but the gradient fonts
	** fill slots 4..15 and leave slot 1 at the background color, so fall forward to
	** the highest slot that differs from the background.
	*/
	int foreslot = 1;
	if (remap[1] == remap[0]) {
		for (int slot = 15; slot >= 2; --slot) {
			if (remap[slot] != remap[0]) {
				foreslot = slot;
				break;
			}
		}
	}

	/*
	** The shadow slots carry the shadow color when a shadow style was requested and
	** the background color otherwise; a slot equal to the background draws nothing.
	*/
	bool dropshadow = (remap[2] != remap[0]);
	bool fullshadow = (remap[3] != remap[0]);

	/*
	** Antialiased foreground: the scheme's sixteen entry ramp (palette indexes
	** 16..31, entry 16 being the full foreground color) shades the glyph edges,
	** the same treatment the wide text path uses. Without a scheme the foreground
	** falls back to the classic hard threshold against the remap slot.
	*/
	int ramp[16];
	int const * rampptr = NULL;
	if (scheme != NULL) {
		for (int index = 0; index < 16; ++index) {
			ramp[index] = 16 + index;
		}
		rampptr = ramp;
	}

	void * buffer = surface.Lock();
	if (buffer == NULL) {
		return(drawpoint);
	}
	int bbp = surface.Bytes_Per_Pixel();
	int stride = surface.Stride();
	Rect srect = surface.Get_Rect();
	int startx = xpos;

	/*
	** Typewriter animation: the reveal advances on the wall clock, each animated
	** text carrying its own clock. Only the first visible code points of the
	** string are drawn, and the freshly appeared one flickers once (hidden for
	** half the flash window, then shown again) before it settles.
	*/
	unsigned elapsed = 0;
	unsigned visible = 0;
	bool animate = false;
	if (Animated && string != NULL && *string != '\0') {
		unsigned now = GetTickCount();
		auto found = AnimStarts.find(string);
		if (found == AnimStarts.end()) {
			found = AnimStarts.emplace(string, now).first;
		}
		elapsed = now - found->second;
		visible = elapsed / MsPerChar;
		animate = true;
	}
	unsigned drawn = 0;

	while (*string != '\0') {
		unsigned char c = (unsigned char)(*string);

		if (c == '\r') {
			string++;
			xpos = startx;
			ypos += lineheight;
			continue;
		}
		if (c == '\n') {
			string++;
			xpos = cliprect.X;
			ypos += lineheight;
			continue;
		}
		if (c < ' ') {
			string++;
			continue;
		}

		if (animate && drawn >= visible) {
			/*
			** Everything past the reveal front stays invisible for now.
			*/
			break;
		}

		unsigned int codepoint = Decode_Next(string);
		Glyph const & glyph = Rasterize(codepoint);
		int advance = glyph.Advance + FontXSpacing;

		/*
		** Fill the background of the character cell if the background is not
		** transparent.
		*/
		if (remap[0] != 0 && advance > 0) {
			Rect cell(xpos, ypos, advance, lineheight);
			cell = Intersect(cell, cliprect);
			if (cell.Is_Valid()) {
				surface.Fill_Rect(cell, convertref.Convert_Pixel(remap[0]));
			}
		}

		if (glyph.Valid) {
			/*
			** The freshly appeared character flickers once: hidden for the first
			** half of the flash window, shown for the second half.
			*/
			bool flicker_hide = false;
			if (animate && visible > 0 && drawn + 1 == visible) {
				unsigned appear = (visible - 1) * MsPerChar;
				if (elapsed >= appear && elapsed - appear < FlashMs) {
					unsigned age = elapsed - appear;
					unsigned half = (FlashMs > 2) ? FlashMs / 2 : 1;
					flicker_hide = ((age / half) % 2) == 1;
				}
			}

			if (!flicker_hide) {
				int x = xpos;
				int y = ypos + Ascent - glyph.Top;

				/*
				** Shadow passes first: the full shadow outlines the glyph on all eight
				** neighbors, the drop shadow sits one step down and to the right.
				*/
				if (fullshadow) {
					static int const dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
					static int const dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
					for (int index = 0; index < 8; ++index) {
						Blit_Glyph(glyph, (unsigned char *)buffer, stride, bbp,
							x + dxs[index], y + dys[index], cliprect, srect, remap[3], NULL, convertref);
					}
				} else if (dropshadow) {
					Blit_Glyph(glyph, (unsigned char *)buffer, stride, bbp,
						x + 1, y + 1, cliprect, srect, remap[2], NULL, convertref);
				}

				/*
				** The foreground itself, antialiased through the scheme's ramp.
				*/
				Blit_Glyph(glyph, (unsigned char *)buffer, stride, bbp,
					x, y, cliprect, srect, remap[foreslot], rampptr, convertref);
			}
		}

		xpos += advance;
		drawn++;
	}

	point = Point2D(xpos - cliprect.X, ypos - cliprect.Y);
	surface.Unlock();
	return(point);
}

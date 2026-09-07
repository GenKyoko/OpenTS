/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

#pragma once

#include "font.h"
#include "surface.h"

#include <map>
#include <string>
#include <vector>

/***********************************************************************************************
 * TtfFontClass -- a FontClass replacement backed by a TrueType face.
 *
 * The classic text path (Fancy_Text_Print, Simple_Text_Print, Conquer_Clip_Text_Print) draws
 * through the FontClass interface, which the embedded bitmap fonts (WWFontClass) implement.
 * TtfFontClass implements the same interface over a FreeType face, so a UI.INI
 * [FontReplacements] entry can substitute any legacy font (6PT_GRAD and friends) with a
 * registered TrueType face (the [TTFFonts] registry names it) without touching a single
 * call site.
 *
 * The faces are opened and handed over by the replacement manager (Wide_Text_Acquire_Face);
 * this class holds a whole chain of them, tried in order until one carries the glyph -- the
 * same convention the wide text path's [UI] DefaultFont chain follows, so a Latin face and
 * a CJK face can team up inside one replacement. Text arriving through the classic
 * interface is decoded as UTF-8 -- which is what Localize produces -- so localized Chinese
 * renders just as well as the Latin UI text, while plain ASCII passes through unchanged.
 * Glyphs are cached per code point.
 *
 * Colors follow the classic font remap conventions of Simple_Text_Print: slot 0 fills the
 * background, slot 1 (or, for the gradient fonts, the highest slot that differs from the
 * background) is the foreground, and slots 2 and 3 carry the drop and full shadow colors.
 * The gray coverage of the TrueType glyphs is thresholded, which reproduces the hard edged
 * look of the embedded bitmap fonts; the shadow passes are drawn by offsetting the glyph
 * bitmap just like the outline pixels of the legacy fonts did.
 ***********************************************************************************************/

class ConvertClass;
class ColorScheme;

class TtfFontClass : public FontClass
{
	public:
		/*
		** ft_faces are FreeType FT_Face handles acquired through Wide_Text_Acquire_Face,
		** tried in order until one carries the glyph; pixel_height is the rasterization
		** height, normally the replaced font's height.
		*/
		TtfFontClass(std::vector<void *> const & ft_faces, int pixel_height);
		virtual ~TtfFontClass(void) override;

		virtual int Char_Pixel_Width(char c) const override;
		virtual int String_Pixel_Width(char const * string) const override;
		virtual void String_Pixel_Bounds(const char * string, Rect & bounds) const override;
		virtual int Get_Width(void) const override;
		virtual int Get_Height(void) const override;
		virtual Point2D Print(char const * string, Surface & surface, Rect const & cliprect, Point2D const & point, ConvertClass const & converter, unsigned char const * remap=NULL, ColorScheme const * scheme=NULL) const override;

		virtual int Set_XSpacing(int x) override;
		virtual int Set_YSpacing(int y) override;

		/*
		** Typewriter animation, in the spirit of the score screen's ShapeFont effect:
		** after Begin_Text_Animation, every Print reveals its text one code point at
		** a time, left to right, and each freshly appeared character flickers once
		** (briefly hidden, then shown again) before settling. The animation advances
		** on the wall clock, so printing every frame just works, and several texts
		** animate at once -- each carries its own clock, keyed by its text. An
		** animation ends on its own once the last character has appeared.
		*/
		void Begin_Text_Animation(unsigned ms_per_char=120, unsigned flash_ms=150);

		/*
		** Reports whether the typewriter animation of the given text is still going:
		** false before the text was printed since Begin_Text_Animation, and once its
		** last character has appeared.
		*/
		bool Is_Text_Animation_Running(char const * text) const;

		/*
		** The count of code points of the text the typewriter animation has revealed
		** so far, capped at the text's length. Callers use it to pace typing sounds
		** against the reveal.
		*/
		unsigned Text_Animation_Visible(char const * text) const;

		/*
		** Marks the text's typewriter run as complete, so its next Print draws the
		** whole text at once. Used for text that must appear fully lit from the
		** start.
		*/
		void Finish_Text_Animation(char const * text);

	private:

		/*
		** One rasterized character: the gray coverage map plus the metrics the drawing
		** loop and the width queries need.
		*/
		struct Glyph {
			bool Valid;
			int Advance;
			int Width;
			int Height;
			int Top;        // Rows between the font ascent and the bitmap's top row.
			std::vector<unsigned char> Gray;
			Glyph(void) : Valid(false), Advance(0), Width(0), Height(0), Top(0) {}
		};

		Glyph const & Rasterize(unsigned int codepoint) const;

		/*
		** Decodes the next UTF-8 code point and advances the pointer past its bytes.
		** Invalid sequences degrade to the byte's Latin-1 value, so nothing is lost
		** when text slips through in a single byte encoding.
		*/
		static unsigned int Decode_Next(char const * & p);

		int Code_Point_Width(unsigned int codepoint) const;

		/*
		** Blits one glyph coverage map at the given position, clipped against both the
		** print rectangle and the surface. With a ramp the coverage walks the scheme's
		** sixteen entry color ramp for antialiased edges; without one the pixels at or
		** above the threshold take the remap slot's color, as for shadows.
		*/
		static void Blit_Glyph(Glyph const & glyph, unsigned char * buffer, int stride, int bbp, int x, int y, Rect const & cliprect, Rect const & surferect, int slotcolor, int const * ramp, ConvertClass const & converter);

		std::vector<void *> Faces;      // FreeType FT_Face chain; released in the destructor.
		int PixelHeight;                // Rasterization height and nominal line height.
		int Ascent;                     // Baseline offset from the top of the line.
		int FontXSpacing;
		int FontYSpacing;
		mutable std::map<unsigned int, Glyph> Glyphs;

		/*
		** Typewriter animation state. The animation is driven by the wall clock and
		** every animated text carries its own reveal clock, keyed by the text itself,
		** so several typewriter lines run at the same time without interfering.
		*/
		bool Animated;
		unsigned MsPerChar;
		unsigned FlashMs;
		mutable std::map<std::string, unsigned> AnimStarts;
};

/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The mouse pointer, as an SDL color cursor built from the game's own shapes.
//
// SDL composites it over the presented frame, so pointing the mouse costs nothing:
// the cursor never touches a game surface and moving it needs no new frame.

#include "always.h"

#include "wincursor.h"

#include "_convert.h"
#include "_xmouse.h"
#include "convert.h"
#include "globals.h"
#include "goptions.h"
#include "shapeset.h"
#include "video.h"
#include "win.h"
#include "xmouse.h"

#include <SDL3/SDL.h>

#include <cstring>


struct CursorCacheEntry
{
	ShapeSet const * Shape;
	int Frame;
	int HotX;
	int HotY;
	SDL_Cursor * Cursor;
};

// One cursor per shape frame the game actually asks for. MOUSE.SHP holds a few hundred
// of them, so the cache is emptied rather than grown when it fills.
static CursorCacheEntry _CursorCache[384];
static int _CursorCacheCount = 0;
static int _CacheScale = 0;

static ShapeSet const * _CurrentShape = NULL;
static int _CurrentFrame = 0;
static int _CurrentHotX = 0;
static int _CurrentHotY = 0;
static SDL_Cursor * _CurrentCursor = NULL;
static bool _CursorVisible = true;


/// <summary>
/// Works out how much larger than its shape the cursor should be drawn.
/// </summary>
/// <returns>int; A whole multiple between one and eight.</returns>
static int Cursor_Scale(void)
{
	if (Options.CursorScale < 0) {
		return(1);
	}

	if (Options.CursorScale > 0) {
		return(Options.CursorScale > 8 ? 8 : Options.CursorScale);
	}

	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	float smaller = scale.ScaleX < scale.ScaleY ? scale.ScaleX : scale.ScaleY;

	int result = (int)(smaller + 0.5f);
	if (result < 1) result = 1;
	if (result > 8) result = 8;
	return(result);
}


/// <summary>
/// Draws one shape frame into an SDL cursor.
/// The canvas covers the shape's whole frame rather than the trimmed part that holds
/// pixels, so the hotspot, which is measured from the frame's corner, still lands in
/// the right place. Palette entry zero is the transparent one.
/// </summary>
/// <returns>The cursor, or NULL if it could not be built.</returns>
static SDL_Cursor * Build_Cursor(ShapeSet const * shape, int frame, int hotx, int hoty, int scale)
{
	if (shape == NULL || MouseDrawer == NULL) {
		return(NULL);
	}

	Rect rect = shape->Get_Rect(frame);
	unsigned char const * data = (unsigned char const *)shape->Get_Data(frame);

	if (!rect.Is_Valid() || data == NULL) {
		return(NULL);
	}

	int width = shape->Get_Width() * scale;
	int height = shape->Get_Height() * scale;

	if (width <= 0 || height <= 0) {
		return(NULL);
	}

	SDL_Surface * surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
	if (surface == NULL) {
		return(NULL);
	}

	memset(surface->pixels, 0, surface->pitch * height);

	// The shapes are palette indices and the primary is 565, so the drawer's table is
	// what turns one into the other.
	unsigned short const * table = (unsigned short const *)MouseDrawer->Get_Translate_Table();

	for (int y = 0; y < rect.Height; y++) {
		for (int x = 0; x < rect.Width; x++) {

			unsigned char index = data[y * rect.Width + x];
			if (index == 0) {
				continue;
			}

			unsigned short pixel = table[index];
			unsigned long red = ((pixel >> 11) & 0x1F) << 3;
			unsigned long green = ((pixel >> 5) & 0x3F) << 2;
			unsigned long blue = (pixel & 0x1F) << 3;
			unsigned long argb = 0xFF000000UL | (red << 16) | (green << 8) | blue;

			for (int suby = 0; suby < scale; suby++) {
				unsigned long * row = (unsigned long *)((char *)surface->pixels + ((rect.Y + y) * scale + suby) * surface->pitch) + (rect.X + x) * scale;
				for (int subx = 0; subx < scale; subx++) {
					row[subx] = argb;
				}
			}
		}
	}

	int cursor_hotx = hotx * scale;
	int cursor_hoty = hoty * scale;
	if (cursor_hotx < 0) cursor_hotx = 0;
	if (cursor_hoty < 0) cursor_hoty = 0;
	if (cursor_hotx >= width) cursor_hotx = width - 1;
	if (cursor_hoty >= height) cursor_hoty = height - 1;

	SDL_Cursor * cursor = SDL_CreateColorCursor(surface, cursor_hotx, cursor_hoty);

	SDL_DestroySurface(surface);
	return(cursor);
}


/// <summary>
/// Destroys every cached cursor.
/// </summary>
static void Flush_Cursor_Cache(void)
{
	for (int index = 0; index < _CursorCacheCount; index++) {
		if (_CursorCache[index].Cursor != NULL) {
			SDL_DestroyCursor(_CursorCache[index].Cursor);
		}
	}

	_CursorCacheCount = 0;
	_CurrentCursor = NULL;
}


/// <summary>
/// Applies the current cursor and visibility to SDL.
/// A NULL cursor asks SDL for its default, which is what the game showed while a
/// dialog owned the mouse.
/// </summary>
static void Apply_Cursor(void)
{
	SDL_SetCursor(_CursorVisible && _CurrentCursor != NULL ? _CurrentCursor : NULL);
}


/*
 * SDL owns the pointer over the game's window, so its state decides whether the
 * cursor shows at all. The game however tracks visibility with the counting scheme
 * ShowCursor always offered -- hide three times, show twice, and the pointer stays
 * hidden -- so the counter here reproduces that scheme on top of SDL.
 */
static int _DisplayCount = 0;


int Win_Cursor_Show_OS(BOOL show)
{
	_DisplayCount += show ? 1 : -1;
	if (_DisplayCount < 0) {
		_DisplayCount = -1;
	}

	if (_DisplayCount > 0) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}

	return(_DisplayCount);
}


int Win_Cursor_Display_Count(void)
{
	return(_DisplayCount);
}


void Win_Cursor_Use_Default(void)
{
	SDL_SetCursor(NULL);
	SDL_ShowCursor();
}


/// <summary>
/// Selects the cursor for a shape frame, building it if it has not been seen before.
/// </summary>
/// <param name="shape">The shape set the frame belongs to.</param>
/// <param name="frame">Which frame of it to show.</param>
/// <param name="hotx">The point within the frame that does the pointing.</param>
/// <param name="hoty">The same, vertically.</param>
/// <param name="apply">Should the cursor be shown straight away?</param>
void Win_Cursor_Set(ShapeSet const * shape, int frame, int hotx, int hoty, bool apply)
{
	int scale = Cursor_Scale();

	if (scale != _CacheScale) {
		Flush_Cursor_Cache();
		_CacheScale = scale;
	}

	_CurrentShape = shape;
	_CurrentFrame = frame;
	_CurrentHotX = hotx;
	_CurrentHotY = hoty;

	SDL_Cursor * cursor = NULL;

	for (int index = 0; index < _CursorCacheCount; index++) {
		CursorCacheEntry & entry = _CursorCache[index];
		if (entry.Shape == shape && entry.Frame == frame) {
			if (entry.HotX != hotx || entry.HotY != hoty) {
				if (entry.Cursor != NULL) {
					SDL_DestroyCursor(entry.Cursor);
				}
				entry.Cursor = Build_Cursor(shape, frame, hotx, hoty, scale);
				entry.HotX = hotx;
				entry.HotY = hoty;
			}
			cursor = entry.Cursor;
			break;
		}
	}

	if (cursor == NULL) {
		if (_CursorCacheCount >= (int)(sizeof(_CursorCache) / sizeof(_CursorCache[0]))) {
			Flush_Cursor_Cache();
		}

		cursor = Build_Cursor(shape, frame, hotx, hoty, scale);

		if (cursor != NULL) {
			CursorCacheEntry & entry = _CursorCache[_CursorCacheCount++];
			entry.Shape = shape;
			entry.Frame = frame;
			entry.HotX = hotx;
			entry.HotY = hoty;
			entry.Cursor = cursor;
		}
	}

	_CurrentCursor = cursor;

	if (apply) {
		Apply_Cursor();
	}
}


/// <summary>
/// Shows or hides the pointer.
/// The state is applied whatever the capture is: SDL decides what reaches the screen,
/// so a visibility change that only sat in the game's bookkeeping would leave the
/// pointer stuck hidden or stuck shown.
/// </summary>
void Win_Cursor_Set_Visible(bool visible)
{
	_CursorVisible = visible;

	if (visible) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}

	Apply_Cursor();
}


/// <summary>
/// Puts the game's pointer back when the window is entered.
/// SDL keeps its own cursor current as the pointer moves between windows, so this
/// reasserts the game's choice rather than answering a WM_SETCURSOR.
/// </summary>
/// <returns>bool; Was the cursor the game's to choose? While a dialog has the mouse it
/// is not, and SDL keeps its own arrow.</returns>
bool Win_Cursor_Handle_Set_Cursor(void)
{
	if (MouseCursor == NULL || !MouseCursor->Is_Captured()) {
		return(false);
	}

	Apply_Cursor();
	return(true);
}


/// <summary>
/// Rebuilds the pointer if the window has been resized enough to want a different size.
/// </summary>
void Win_Cursor_Refresh(void)
{
	if (_CurrentShape != NULL && Cursor_Scale() != _CacheScale) {
		Win_Cursor_Set(_CurrentShape, _CurrentFrame, _CurrentHotX, _CurrentHotY,
			MouseCursor != NULL && MouseCursor->Is_Captured());
	}
}


/// <summary>
/// Releases every cursor the game built.
/// </summary>
void Win_Cursor_Shutdown(void)
{
	SDL_SetCursor(NULL);
	Flush_Cursor_Cache();
	_CurrentShape = NULL;
	_DisplayCount = 0;
}

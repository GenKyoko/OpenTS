/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The SDL3 platform layer: window creation, the event queue drain and the display
// queries that used to go through the Desktop Window API. See sdlplatform.h.

#include "always.h"

#include "sdlplatform.h"

#include "_xmouse.h"
#include "dbgprint.h"
#include "globals.h"
#include "goptions.h"
#include "misc.h"
#include "resource.h"
#include "video.h"
#include "win.h"
#include "wwmouse.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include <cstring>


static SDL_Window * _Window = NULL;
static Uint32 _MainWindowID = 0;


// The focus handlers the game's window procedure calls on WM_ACTIVATEAPP. They live
// in winstub.cpp; SDL's focus events are the activation messages now.
void Focus_Loss(void);
void Focus_Restore(void);


/*
 * Converts a Windows icon resource into an SDL surface so the taskbar and the title
 * bar keep the game's artwork. Legacy icons carry their transparency in a mask bitmap
 * rather than in an alpha channel, so the alpha is derived from the mask whenever the
 * color bitmap turns out to have none.
 */
static SDL_Surface * Icon_To_Surface(HICON icon)
{
	if (icon == NULL) {
		return(NULL);
	}

	ICONINFO info;
	if (!GetIconInfo(icon, &info)) {
		return(NULL);
	}

	SDL_Surface * surface = NULL;
	BITMAP bitmap;
	if (GetObject(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap) && bitmap.bmWidth > 0 && bitmap.bmHeight > 0) {
		int width = bitmap.bmWidth;
		int height = bitmap.bmHeight;

		BITMAPINFO info32;
		memset(&info32, 0, sizeof(info32));
		info32.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info32.bmiHeader.biWidth = width;
		info32.bmiHeader.biHeight = -height;
		info32.bmiHeader.biPlanes = 1;
		info32.bmiHeader.biBitCount = 32;

		surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
		if (surface != NULL) {
			HDC dc = GetDC(NULL);
			bool read = GetDIBits(dc, info.hbmColor, 0, height, surface->pixels, &info32, DIB_RGB_COLORS) == height;

			// A fully transparent alpha channel means the icon predates alpha
			// support, and the mask decides which pixels show.
			bool has_alpha = false;
			unsigned long * pixels = (unsigned long *)surface->pixels;
			for (int index = 0; index < width * height && !has_alpha; index++) {
				has_alpha = (pixels[index] & 0xFF000000UL) != 0;
			}

			if (read && !has_alpha && info.hbmMask != NULL) {
				int mask_stride = ((width + 31) / 32) * 4;
				char * mask = new char[mask_stride * height];

				/*
				 * A monochrome bitmap carries a two entry color table, which BITMAPINFO
				 * alone has no room for -- it holds one entry -- so the header is
				 * declared with the palette it will be written into.
				 */
				struct {
					BITMAPINFOHEADER Header;
					RGBQUAD Colors[2];
				} info1;

				memset(&info1, 0, sizeof(info1));
				info1.Header.biSize = sizeof(BITMAPINFOHEADER);
				info1.Header.biWidth = width;
				info1.Header.biHeight = -height;
				info1.Header.biPlanes = 1;
				info1.Header.biBitCount = 1;
				info1.Header.biCompression = BI_RGB;

				if (GetDIBits(dc, info.hbmMask, 0, height, mask, (BITMAPINFO *)&info1, DIB_RGB_COLORS) == height) {
					for (int y = 0; y < height; y++) {
						for (int x = 0; x < width; x++) {
							bool transparent = (mask[y * mask_stride + x / 8] >> (7 - (x % 8))) & 1;
							if (!transparent) {
								pixels[y * width + x] |= 0xFF000000UL;
							}
						}
					}
				}

				delete [] mask;
			}

			if (!read) {
				SDL_DestroySurface(surface);
				surface = NULL;
			}

			ReleaseDC(NULL, dc);
		}
	}

	if (info.hbmColor != NULL) {
		DeleteObject(info.hbmColor);
	}
	if (info.hbmMask != NULL) {
		DeleteObject(info.hbmMask);
	}

	return(surface);
}


/// <summary>
/// Opens the game window through SDL and points MainWindow at its native handle.
/// The windowed settings decide the frame; a framed window is grown by its borders so
/// the client area ends up exactly the requested size, the way the old creation code
/// adjusted the window rectangle by hand.
/// </summary>
/// <returns>bool; Was the window opened? A false return is fatal to the game.</returns>
bool SDL_Platform_Create_Window(int width, int height)
{
	DebugString("SDL: creating the game window\n");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		DebugString("SDL: init failed: %s\n", SDL_GetError());
		return(false);
	}

	/*
	 * The game has no screensaver of its own to fear, and the old window procedure
	 * rejected the screensaver broadcast outright. SDL asks the desktop to leave it
	 * alone for the lifetime of the process instead.
	 */
	SDL_DisableScreenSaver();

	SDL_WindowFlags flags = 0;
	int window_width = width;
	int window_height = height;

	if (WindowedMode) {
		/*
		 * The client area is the game resolution, exactly. The owner-draw dialogs are
		 * real child windows laid out in these coordinates and their content is drawn
		 * onto the game's surfaces at the same coordinates, so any other client size
		 * leaves every dialog displaced and mis-scaled. The window is therefore not
		 * resizable; a different resolution goes through Video_Set_Mode, which keeps
		 * the client in step.
		 */
		flags = Options.NoWindowFrame ? SDL_WINDOW_BORDERLESS : 0;
	} else {
		/*
		 * The desktop keeps its own resolution and the window simply covers it. The
		 * frame is scaled to fit at presentation time.
		 */
		flags = SDL_WINDOW_FULLSCREEN;

		SDL_DisplayID display = SDL_GetPrimaryDisplay();
		SDL_DisplayMode const * mode = SDL_GetDesktopDisplayMode(display);
		if (mode != NULL) {
			window_width = mode->w;
			window_height = mode->h;
		}
	}

	DebugString("SDL: window is %dx%d, flags %08X\n", window_width, window_height, (unsigned int)flags);

	_Window = SDL_CreateWindow("Tiberian Sun", window_width, window_height, flags);
	if (_Window == NULL) {
		DebugString("SDL: window creation failed: %s\n", SDL_GetError());
		return(false);
	}

	/*
	 * SDL3 names the client area the window size, so the requested frame lands in the
	 * client area exactly and no border compensation is wanted -- adding one grew the
	 * client past the screen and stretched everything in it.
	 */

	MainWindow = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(_Window),
		SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

	/*
	 * SDL styles its windows with WS_CLIPCHILDREN, which makes the Direct3D 9 bitblt
	 * present leave every child window's rectangle untouched. The owner-draw dialogs
	 * are exactly such children -- and they never paint their own pixels, so with the
	 * clip in place each dialog's area keeps showing whatever was on the screen before
	 * it opened. The presented frame has to cover them, so the style goes.
	 */
	if (MainWindow != NULL) {
		LONG_PTR style = GetWindowLongPtr(MainWindow, GWL_STYLE);
		SetWindowLongPtr(MainWindow, GWL_STYLE, style & ~WS_CLIPCHILDREN);
		SetWindowPos(MainWindow, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	DebugString("SDL: native window is %p\n", MainWindow);

	/*
	 * The taskbar and the title bar keep the game's own artwork.
	 */
	SDL_Surface * icon = Icon_To_Surface(LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_SUN)));
	if (icon != NULL) {
		SDL_SetWindowIcon(_Window, icon);
		SDL_DestroySurface(icon);
	}

	DebugString("SDL: window is ready\n");

	SDL_RaiseWindow(_Window);

	_MainWindowID = SDL_GetWindowID(_Window);

	return(MainWindow != NULL);
}


/// <summary>
/// Resizes the window's client area to match a new game resolution.
/// The owner-draw dialogs assume the client area and the game resolution are the same
/// size, so a resolution change has to carry the window with it.
/// </summary>
void SDL_Platform_Set_Client_Size(int width, int height)
{
	if (_Window != NULL && width > 0 && height > 0) {
		SDL_SetWindowSize(_Window, width, height);
	}
}


/// <summary>
/// Drains SDL's event queue.
/// SDL pumps the Windows message queue while it polls, and every message it pumps goes
/// through the hook in msgloop.cpp first, which is where the game's message logic
/// lives. The window lifecycle -- resizing, moving and exposure -- is answered here
/// rather than in the message procedure, because a modal move or size loop dispatches
/// messages without the pump ever seeing them, and the frame and the mouse bounds have
/// to keep up through those too. Leaving the events unread would grow the queue
/// without bound.
/// </summary>
void SDL_Pump_Game_Events(void)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {

		if (event.type != SDL_EVENT_WINDOW_FOCUS_GAINED &&
			event.type != SDL_EVENT_WINDOW_FOCUS_LOST &&
			event.type != SDL_EVENT_WINDOW_MOVED &&
			event.type != SDL_EVENT_WINDOW_RESIZED &&
			event.type != SDL_EVENT_WINDOW_EXPOSED &&
			event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
			continue;
		}

		/*
		 * Only the main window's events matter; the crash reporter owns its own.
		 */
		if (event.window.windowID != _MainWindowID) {
			continue;
		}

		switch (event.type) {
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				/*
				 * Focus is the game's notion of activation, which used to ride on
				 * WM_ACTIVATEAPP. The activation sent while the window was opened
				 * predates the message hook, so the SDL event is what turns the
				 * waiting-at-startup loop over.
				 */
				if (!GameInFocus) {
					GameInFocus = true;
					Focus_Restore();
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				if (GameInFocus) {
					GameInFocus = false;
					Focus_Loss();
				}
				break;

			case SDL_EVENT_WINDOW_RESIZED:
				if (event.window.data1 > 0 && event.window.data2 > 0) {
					/*
					 * The client size is whatever the launcher or the player made of the
					 * window; the frame is presented letterboxed into it, and the
					 * dialogs map their positions through the same scale, so an
					 * external resize needs no correction.
					 */
					Video_On_Resize(event.window.data1, event.window.data2);
					if (MouseCursor != NULL) {
						((WWMouseClass *)MouseCursor)->Calc_Confining_Rect();
					}
				}
				break;

			case SDL_EVENT_WINDOW_MOVED:
				if (WindowedMode && MouseCursor != NULL) {
					((WWMouseClass *)MouseCursor)->Calc_Confining_Rect();
				}
				break;

			case SDL_EVENT_WINDOW_EXPOSED:
				Video_Mark_Dirty();
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				/*
				 * Closing reaches the game as a WM_CLOSE through the hook, which is
				 * where the close semantics live.
				 */
				break;
		}
	}
}


/// <summary>
/// Fetches the refresh rate of the display the window sits on.
/// </summary>
/// <returns>int; The refresh rate in hertz, or zero when it cannot be asked.</returns>
int SDL_Display_Refresh_Rate(void)
{
	if (_Window == NULL) {
		return(0);
	}

	SDL_DisplayID display = SDL_GetDisplayForWindow(_Window);
	if (display == 0) {
		return(0);
	}

	SDL_DisplayMode const * mode = SDL_GetCurrentDisplayMode(display);
	if (mode == NULL) {
		return(0);
	}

	return((int)(mode->refresh_rate + 0.5f));
}


/// <summary>
/// Closes the window and shuts SDL down.
/// </summary>
void SDL_Platform_Shutdown(void)
{
	if (_Window != NULL) {
		SDL_DestroyWindow(_Window);
		_Window = NULL;
		MainWindow = NULL;
	}

	SDL_Quit();
}

/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The SDL side of the presenter. The frame arrives as 16 bit 565 pixels; this uploads
// it into a streaming texture, scales it into the window with the requested filter and
// puts it on the screen. This is the only translation unit that touches the renderer,
// which keeps SDL's headers away from the rest of the engine.

#include "always.h"

#include "renderbackend.h"

#include "dbgprint.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>


static SDL_Window * _Window = NULL;
static SDL_Renderer * _Renderer = NULL;
static SDL_Texture * _FrameTexture = NULL;
static SDL_Texture * _PrescaleTexture = NULL;
static SDL_PixelFormat _FrameFormat = SDL_PIXELFORMAT_UNKNOWN;

static bool _Initialized = false;
static char const * _RendererName = "none";

static int _FrameWidth = 0;
static int _FrameHeight = 0;
static int _PrescaleWidth = 0;
static int _PrescaleHeight = 0;
static int _WindowWidth = 0;
static int _WindowHeight = 0;

// True while the frame texture holds the game's own 565 layout. When the renderer
// cannot offer that format the frame is widened to 32 bits on the way in instead.
static unsigned int * _ConvertBuffer = NULL;
static unsigned int _ConvertTable[65536];


/// <summary>
/// Builds the table that widens a 565 pixel to the 32 bit color the fallback path uploads.
/// </summary>
static void Build_Convert_Table(void)
{
	for (int pixel = 0; pixel < 65536; pixel++) {
		unsigned int red = (unsigned int)(((pixel >> 11) & 0x1F) * 255 / 31);
		unsigned int green = (unsigned int)(((pixel >> 5) & 0x3F) * 255 / 63);
		unsigned int blue = (unsigned int)((pixel & 0x1F) * 255 / 31);

		_ConvertTable[pixel] = 0xFF000000 | (red << 16) | (green << 8) | blue;
	}
}


/// <summary>
/// Discards the intermediate texture the pixel art filter magnifies through.
/// </summary>
static void Destroy_Prescale_Texture(void)
{
	if (_PrescaleTexture != NULL) {
		SDL_DestroyTexture(_PrescaleTexture);
		_PrescaleTexture = NULL;
	}
	_PrescaleWidth = 0;
	_PrescaleHeight = 0;
}


/// <summary>
/// Makes sure the pixel art filter has an intermediate texture of the requested size.
/// </summary>
/// <returns>bool; Is a texture of that size ready to render into?</returns>
static bool Ensure_Prescale_Texture(int width, int height)
{
	if (_PrescaleTexture != NULL && _PrescaleWidth == width && _PrescaleHeight == height) {
		return(true);
	}

	Destroy_Prescale_Texture();

	if (width <= 0 || height <= 0) {
		return(false);
	}

	/*
	 * The intermediate texture is rendered into, so it has to be a 32 bit format --
	 * most graphics APIs refuse 16 bit render targets outright, and a target that
	 * cannot render to presents as a black frame.
	 */
	_PrescaleTexture = SDL_CreateTexture(_Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
	if (_PrescaleTexture == NULL) {
		DebugString("Renderer: prescale target creation failed: %s\n", SDL_GetError());
		return(false);
	}

	_PrescaleWidth = width;
	_PrescaleHeight = height;
	return(true);
}


/// <summary>
/// Starts the renderer on an existing window.
/// </summary>
/// <param name="window">The window the frame is presented into.</param>
/// <param name="windowwidth">The width of that window's client area.</param>
/// <param name="windowheight">The height of that window's client area.</param>
/// <param name="renderer">Which graphics API to ask for, or auto to let SDL decide.</param>
/// <param name="vsync">Should presents wait for the display's refresh?</param>
/// <returns>bool; Did the renderer start?</returns>
bool Backend_Init(HWND window, int windowwidth, int windowheight, BackendRenderer renderer, bool vsync)
{
	if (_Initialized) {
		return(true);
	}

	/*
	 * The window handle the game carries is the native one SDL handed out, so the
	 * matching SDL_Window comes back by looking through the ones SDL owns.
	 */
	SDL_Window * found = NULL;
	int windowcount = 0;
	SDL_Window ** windows = SDL_GetWindows(&windowcount);
	for (int index = 0; index < windowcount; index++) {
		HWND native = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(windows[index]),
			SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
		if (native == window) {
			found = windows[index];
			break;
		}
	}
	if (windows != NULL) {
		SDL_free(windows);
	}

	if (found == NULL) {
		DebugString("Renderer: the game window is not an SDL window\n");
		return(false);
	}

	_Window = found;

	/*
	 * SDL_GPU is the preferred presenter. The owner-draw dialogs ride over the frame
	 * as owned popup windows with presentation surfaces of their own, so a flip-model
	 * swapchain no longer erases them. Should SDL_GPU be unavailable on a machine, the
	 * renderer falls back to SDL's own choice and finally to the software renderer,
	 * which presents through GDI and always works.
	 */
	_Renderer = SDL_CreateRenderer(_Window, "gpu");
	if (_Renderer == NULL) {
		DebugString("Renderer: SDL_GPU is unavailable (%s), letting SDL choose\n", SDL_GetError());
		_Renderer = SDL_CreateRenderer(_Window, NULL);
	}
	if (_Renderer == NULL) {
		DebugString("Renderer: falling back to the software renderer: %s\n", SDL_GetError());
		_Renderer = SDL_CreateRenderer(_Window, "software");
	}

	if (_Renderer == NULL) {
		DebugString("Renderer: SDL could not create a renderer: %s\n", SDL_GetError());
		return(false);
	}

	SDL_SetRenderVSync(_Renderer, vsync ? 1 : 0);

	_WindowWidth = windowwidth;
	_WindowHeight = windowheight;
	_Initialized = true;
	_RendererName = SDL_GetRendererName(_Renderer);

	return(true);
}


/// <summary>
/// Shuts the renderer down and releases everything it created.
/// </summary>
void Backend_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	Destroy_Prescale_Texture();

	if (_FrameTexture != NULL) {
		SDL_DestroyTexture(_FrameTexture);
		_FrameTexture = NULL;
	}

	delete [] _ConvertBuffer;
	_ConvertBuffer = NULL;

	if (_Renderer != NULL) {
		SDL_DestroyRenderer(_Renderer);
		_Renderer = NULL;
	}

	_FrameWidth = 0;
	_FrameHeight = 0;
	_WindowWidth = 0;
	_WindowHeight = 0;
	_Initialized = false;
	_RendererName = "none";
}


/// <summary>
/// Points the renderer at a frame of the given size, replacing any earlier one.
/// </summary>
/// <returns>bool; Is a texture of that size ready to receive frames?</returns>
bool Backend_Set_Frame_Size(int width, int height)
{
	if (!_Initialized || width <= 0 || height <= 0) {
		return(false);
	}

	if (_FrameTexture != NULL && _FrameWidth == width && _FrameHeight == height) {
		return(true);
	}

	Destroy_Prescale_Texture();

	if (_FrameTexture != NULL) {
		SDL_DestroyTexture(_FrameTexture);
		_FrameTexture = NULL;
	}

	delete [] _ConvertBuffer;
	_ConvertBuffer = NULL;

	/*
	 * The game draws 565, which every desktop renderer offers as a streaming texture.
	 * Should one not, the frame is widened to 32 bits on the way in instead.
	 */
	_FrameFormat = SDL_PIXELFORMAT_RGB565;
	_FrameTexture = SDL_CreateTexture(_Renderer, _FrameFormat, SDL_TEXTUREACCESS_STREAMING, width, height);

	if (_FrameTexture == NULL) {
		DebugString("Renderer: 565 textures are unavailable (%s), widening frames to 32 bit\n", SDL_GetError());

		_FrameFormat = SDL_PIXELFORMAT_ARGB8888;
		_FrameTexture = SDL_CreateTexture(_Renderer, _FrameFormat, SDL_TEXTUREACCESS_STREAMING, width, height);
		if (_FrameTexture == NULL) {
			DebugString("Renderer: frame texture creation failed: %s\n", SDL_GetError());
			return(false);
		}

		if (_ConvertTable[0xFFFF] == 0) {
			Build_Convert_Table();
		}
		_ConvertBuffer = new unsigned int[width * height];
	}

	_FrameWidth = width;
	_FrameHeight = height;
	return(true);
}


/// <summary>
/// Tells the renderer the window's client area changed size.
/// SDL tracks the window's own size, so this only records it for the guard that skips
/// presents into a client area that does not exist.
/// </summary>
void Backend_On_Resize(int windowwidth, int windowheight)
{
	if (!_Initialized || windowwidth <= 0 || windowheight <= 0) {
		return;
	}

	_WindowWidth = windowwidth;
	_WindowHeight = windowheight;
}


/// <summary>
/// Uploads the frame and puts it on the screen.
/// </summary>
/// <param name="pixels">The frame's top left pixel, in 16 bit 565.</param>
/// <param name="pitch">The bytes between one row of that frame and the next.</param>
/// <param name="destx">Where the left edge of the frame lands in the window.</param>
/// <param name="desty">Where the top edge of the frame lands in the window.</param>
/// <param name="destwidth">How wide the frame is drawn.</param>
/// <param name="destheight">How tall the frame is drawn.</param>
/// <param name="mode">How the frame is filtered when it is drawn larger than it is.</param>
void Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode)
{
	if (!_Initialized || pixels == NULL || _FrameTexture == NULL) {
		return;
	}

	// A minimized window has no client area to present into.
	if (_WindowWidth <= 0 || _WindowHeight <= 0) {
		return;
	}

	if (_FrameFormat == SDL_PIXELFORMAT_RGB565) {
		SDL_UpdateTexture(_FrameTexture, NULL, pixels, pitch);
	} else if (_ConvertBuffer != NULL) {
		for (int y = 0; y < _FrameHeight; y++) {
			unsigned short const * source = (unsigned short const *)((char const *)pixels + y * pitch);
			unsigned int * dest = _ConvertBuffer + y * _FrameWidth;
			for (int x = 0; x < _FrameWidth; x++) {
				dest[x] = _ConvertTable[source[x]];
			}
		}
		SDL_UpdateTexture(_FrameTexture, NULL, _ConvertBuffer, _FrameWidth * 4);
	}

	SDL_SetTextureScaleMode(_FrameTexture,
		(mode == BACKEND_SCALE_LINEAR) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

	/*
	 * The pixel art filter keeps whole pixels whole. An exact multiple needs nothing
	 * but point sampling; anything else is magnified to the next whole multiple with
	 * point sampling and then shrunk to the window smoothly, which keeps edges sharp
	 * without the uneven pixel sizes that point sampling alone would give.
	 */
	SDL_Texture * source = _FrameTexture;

	if (mode == BACKEND_SCALE_PIXELART && destwidth > _FrameWidth && destheight > _FrameHeight &&
		((destwidth % _FrameWidth) != 0 || (destheight % _FrameHeight) != 0)) {

		int scale = (destwidth + _FrameWidth - 1) / _FrameWidth;
		int scaley = (destheight + _FrameHeight - 1) / _FrameHeight;
		if (scaley > scale) {
			scale = scaley;
		}

		if (Ensure_Prescale_Texture(_FrameWidth * scale, _FrameHeight * scale)) {
			SDL_SetRenderTarget(_Renderer, _PrescaleTexture);
			SDL_SetRenderDrawColor(_Renderer, 0, 0, 0, 255);
			SDL_RenderClear(_Renderer);

			SDL_SetTextureScaleMode(_FrameTexture, SDL_SCALEMODE_NEAREST);
			SDL_FRect magnified = { 0.0f, 0.0f, (float)_PrescaleWidth, (float)_PrescaleHeight };
			SDL_RenderTexture(_Renderer, _FrameTexture, NULL, &magnified);

			SDL_SetRenderTarget(_Renderer, NULL);
			SDL_SetTextureScaleMode(_PrescaleTexture, SDL_SCALEMODE_LINEAR);
			source = _PrescaleTexture;
		}
	}

	// Clearing the whole window is what paints the bars beside a frame that does not
	// share the window's shape.
	SDL_SetRenderDrawColor(_Renderer, 0, 0, 0, 255);
	SDL_RenderClear(_Renderer);

	SDL_FRect destination = { (float)destx, (float)desty, (float)destwidth, (float)destheight };
	SDL_RenderTexture(_Renderer, source, NULL, &destination);

	SDL_RenderPresent(_Renderer);
}


/// <summary>
/// Names the graphics API the renderer settled on.
/// </summary>
char const * Backend_Renderer_Name(void)
{
	return(_RendererName);
}

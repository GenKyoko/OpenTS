/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "win.h"

/*
 * The SDL3 platform layer. SDL owns the window and pumps its event queue; the game's
 * message logic keeps running through the Windows message hook the pump drives, so the
 * engine's message-driven surface (keyboard, mouse routing, dialogs, network messages)
 * behaves exactly as it did when the window was created by hand.
 *
 * SDL keeps a real Win32 window underneath on Windows, so the native handles the game
 * still hands out -- MainWindow to DirectSound and the renderer, child controls and
 * hot keys -- remain genuine and continue to work.
 */

// Initializes SDL, opens the game window and points MainWindow at its native handle.
// The size arguments name the frame the game renders; the client area is exactly that
// size, because the dialog system maps itself one to one onto it.
bool SDL_Platform_Create_Window(int width, int height);

// Resizes the window's client area to match a new game resolution.
void SDL_Platform_Set_Client_Size(int width, int height);

// Drains SDL's event queue. Everything the game reacts to arrives through the Windows
// message hook instead, so the events themselves are only discarded here.
void SDL_Pump_Game_Events(void);

// The refresh rate of the display the window sits on, or zero when it cannot be asked.
int SDL_Display_Refresh_Rate(void);

// Releases the window. The process is normally on its way out when this runs.
void SDL_Platform_Shutdown(void);

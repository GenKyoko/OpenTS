/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The mouse pointer, as a real Windows cursor built from the game's own shapes.
//
// Windows composites it over the presented frame, so pointing the mouse costs nothing:
// the cursor never touches a game surface and moving it needs no new frame.

#pragma once

#include "win.h"

class ShapeSet;


void Win_Cursor_Set(ShapeSet const * shape, int frame, int hotx, int hoty, bool apply);
void Win_Cursor_Set_Visible(bool visible);

/*
 * The operating system pointer's visibility, with the counting scheme ShowCursor
 * offered: hide more times than show and it stays hidden. SDL owns the pointer over
 * the game's window, so these drive SDL's state, and the count is what the game reads
 * back through Get_Mouse_State.
 */
int Win_Cursor_Show_OS(BOOL show);
int Win_Cursor_Display_Count(void);

// Hands the pointer back to the system arrow, which is what the player sees while a
// dialog or the launcher owns the mouse.
void Win_Cursor_Use_Default(void);

bool Win_Cursor_Handle_Set_Cursor(void);
void Win_Cursor_Refresh(void);
void Win_Cursor_Shutdown(void);

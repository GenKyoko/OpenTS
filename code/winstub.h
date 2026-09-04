/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
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

#include "win.h"

class Surface;
class PaletteClass;

void Create_Main_Window ( HINSTANCE instance , int command_show , int width , int height);

void Load_Title_Screen(char const * name, Surface * surface, PaletteClass * palette);

unsigned int Build_Number(void);

/*
 * A window close during a spawned match is answered with the exit event the
 * options menu's abort sends, so the match resolves through the event system
 * and the launcher session exits cleanly. Asking takes the request.
 */
bool Game_Close_Requested(void);
void Request_Game_Close(void);

/*
 * Reports whether drawing should hold off. A minimised window has nothing to
 * draw into, and a full-screen window that has lost the input focus has had
 * its display mode taken away. A windowed game keeps running and drawing
 * behind whatever holds the focus.
 */
bool Should_Skip_Drawing(void);

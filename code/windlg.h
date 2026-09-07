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


BOOL Get_Display_Rect(HWND window, LPRECT rect);

// The display rect mapped into the game surface's coordinate space: the client
// position is carried through the presentation letterbox and scale, so content
// drawn onto the game surfaces at this rect lands exactly over the window.
BOOL Get_Game_Rect(HWND window, LPRECT rect);

// Returns a heap copy of a dialog template whose style is patched from WS_CHILD to
// WS_POPUP, so CreateDialogIndirectParam brings the dialog into the world as a popup
// owned by the window it is created against -- a child cannot become a popup after
// creation, and a popup owns the presentation surface it composes with under every
// renderer. The caller frees the buffer with delete[] once the dialog has been
// created. NULL is returned when the resource is missing.
LPDLGTEMPLATE Fetch_Popup_Template(int id);

HWND WS_Create_Dialog(HINSTANCE instance, int id, HWND parent, DLGPROC proc, BOOL force_show);
bool WS_Destroy_Dialog(HWND window, int id);

HWND WS_Find_Dialog(int id);
BOOL WS_Has_Dialog(HWND window);

int WS_Wait_Dialog(HWND window, bool (*callback)(void), bool=false, bool place_on_top=true);

HWND WS_Top_Window(void);
int WS_Top_Window_ID(void);
extern HWND g_TopWindow;

int WS_Get_Saved_Value(int control_id, unsigned char * dest, int dest_size);
void WS_Clear_Saved_Values(void);

HWND WS_Next_Upper_Dialog(HWND window);
HWND WS_Next_Lower_Dialog(HWND window);

void Resize_Dialogs(HWND window);

HFONT WS_Get_Font(HDC hdc, const char * face_name, int decipt_width, int decipt_height, int attributes);

struct WSDialogStruct {
	/*
	 * This is the window handle of the dialog occupying this slot. It stays zero until the
	 * dialog has actually been created, so a template that failed to load claims no slot.
	 */
	HWND handle;

	/*
	 * This is the resource identifier of the template the dialog was built from. It is what
	 * lets a dialog be found again by name rather than by handle.
	 */
	int id;
};

extern WSDialogStruct g_Dialogs[64];
extern int g_DialogCount;
extern HWND g_TopWindow;
extern int g_TopWindowID;
extern int g_LastResponse;

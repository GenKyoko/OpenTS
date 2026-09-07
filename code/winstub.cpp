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

/* $Header: /CounterStrike/WINSTUB.CPP 3     3/13/97 2:06p Steve_tall $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : WINSTUB.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Steve Tall                                                   *
 *                                                                                             *
 *                   Start Date : 10/04/95                                                     *
 *                                                                                             *
 *                  Last Update : October 4th 1995 [ST]                                        *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Overview:                                                                                   *
 *   This file contains stubs for undefined externals when linked under Watcom for Win 95      *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 *                                                                                             *
 * Functions:                                                                                  *
 *   Assert_Failure -- display the line and source file where a failed assert occurred         *
 *   Check_For_Focus_Loss -- check for the end of the focus loss                               *
 *   Create_Main_Window -- opens the MainWindow for C&C                                        *
 *   Focus_Loss -- this function is called when a library function detects focus loss          *
 *   Memory_Error_Handler -- Handle a possibly fatal failure to allocate memory                *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "winstub.h"

#include "_keyboar.h"
#include "_map.h"
#include "_rect.h"
#include "_surface.h"
#include "_tooltip.h"
#include "ccfile.h"
#include "cctooltip.h"
#include "convert.h"
#include "dbgprint.h"
#include "draw.h"
#include "dsaudio.h"
#include "dsurface.h"
#include "except.h"
#include "globals.h"
#include "goptions.h"
#include "init.h"
#include "misc.h"
#include "movie.h"
#include "movies.h"
#include "msgroute.h"
#include "pcx.h"
#include "resource.h"
#include "sdlplatform.h"
#include "spawner.h"
#include "theme.h"
#include "video.h"
#include "win.h"
#include "wincursor.h"
#include "windlg.h"
#include "winfix.h"
#include "wsproto.h"
#include "wwmouse.h"
#include "mainopt.h"
#include "conquer.h"
#include "opents_version.h"

#include <algorithm>
#include <commctrl.h>
#include <imm.h>

int		ShowCommand;
HWND	MainWindow;
HWND	UnusedWindow;

HINSTANCE	ProgramInstance;
bool _MouseCaptured;
bool _MouseWheel;


//void output(short,short)
//{}

/*
 * Taken from later Windows SDK after what is shipped in VS6
 */

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  /// message that will be supported
#endif

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wParam)  ((short)HIWORD(wParam))
#endif
///////////////////////////////////////////////////////////

//unsigned long CCFocusMessage = WM_USER+50;	//Private message for receiving application focus
extern	void VQA_PauseAudio(void);
extern	void VQA_ResumeAudio(void);


/***********************************************************************************************
 * Focus_Loss -- this function is called when a library function detects focus loss            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/1/96 2:10PM ST : Created                                                               *
 *=============================================================================================*/

void Focus_Loss(void)
{
	DebugString("Focus_Loss()\n");
	Pause_Ingame_Movie(true);
	/*
	**	Silence the mixer but leave the samples in place, so the theme streaming
	**	from a file keeps its place and picks up where it left off instead of
	**	being stopped and started over.
	*/
	if (Audio_Available()) Audio.Stop_Primary_Sound_Buffer(false);

	/*
	**	Stop any map scrolling: the coast scroll ends here, the keyboard buffer is
	**	flushed so a held arrow key cannot keep scrolling the map while the window
	**	is in the background, and the edge scroll picks the change up on its next
	**	frame through the GameInFocus check.
	*/
	Map.Set_Scroll_Coasting_Allowed(false);
	if (Keyboard != NULL) {
		Keyboard->Clear();
	}

	if (MouseCursor) {
		_MouseCaptured = MouseCursor->Is_Captured();
		DebugString("Focus_Loss(): _MouseCaptured = %s\n", _MouseCaptured ? "true" : "false");
		MouseCursor->Release_Mouse();
	}
}


/// <summary>
/// Restores the game when it regains the input focus.
/// This routine is the counterpart to Focus_Loss. It starts the sound and the music
/// back up, recaptures the mouse if it was captured when focus was lost, and flags the
/// whole screen for redraw.
/// </summary>
void Focus_Restore(void)
{
	DebugString("Focus_Restore()\n");
	if (Audio_Available()) Audio.Start_Primary_Sound_Buffer(TRUE);
	DebugString("Focus_Restore(): _MouseCaptured = %s\n", _MouseCaptured ? "true" : "false");
	if (MouseCursor && _MouseCaptured == true && !Debug_Map) {
		MouseCursor->Capture_Mouse();
	}
	Heal_Dialog_Controls();
	Map.Flag_To_Redraw(GS_REDRAW_ALL);
	InvalidateRect(MainWindow, 0, 0);
	Pause_Ingame_Movie(false);
	if (WS_Top_Window()) {
		SetActiveWindow(WS_Top_Window());
		SetFocus(WS_Top_Window());
	}
}


extern bool InMovie;

/*
 * A close request in a spawned match is answered with the exit event the
 * options menu's abort sends, so the match resolves through the event system
 * and the session winds down to the launcher's clean exit. Asking takes the
 * request, so one close sends one exit.
 */
static bool _CloseRequested = false;

bool Game_Close_Requested(void)
{
	bool const requested = _CloseRequested;
	_CloseRequested = false;
	return(requested);
}

void Request_Game_Close(void)
{
	_CloseRequested = true;
}


/// <summary>
/// Reports whether drawing should hold off.
/// The game keeps running while another window holds the input focus, but a
/// minimised window has no drawable area, and a full-screen window that lost
/// the focus has had its display mode taken away, so painting is paused until
/// the window comes back.
/// </summary>
bool Should_Skip_Drawing(void)
{
	if (MainWindow == NULL) {
		return(false);
	}
	return(IsIconic(MainWindow) || (!WindowedMode && !GameInFocus));
}

/*
 * The close button is a request to end the run. A match the launcher
 * spawned winds down through the game loop, so the network and the
 * libraries shut down before the clean exit. Any other close takes the
 * quick exit without the shutdown sweep, which belongs to the menu exit
 * path and would tear down state a game closed mid-play is still inside;
 * the operating system reclaims the rest and the process still reports a
 * clean exit to whatever started it.
 */
static void Close_Request_Handler(void)
{
	DebugString("Close requested (spawner=%s, scenario=%s)\n",
				Spawner::Is_Active() ? "yes" : "no", ScenarioActive ? "yes" : "no");
	if (Spawner::Is_Active() && ScenarioActive) {
		Request_Game_Close();
	} else {
		ExitProcess(EXIT_SUCCESS);
	}
}

/// <summary>
/// Handles the Windows messages sent to the main game window.
/// This is the window procedure registered for the main window. It offers each
/// message to the network transport, the map and the keyboard handlers, deals with
/// the messages the game must react to itself -- focus changes, painting, tray
/// locking and shutdown -- and passes everything else back to Windows.
/// </summary>
/// <returns>Returns with the result Windows expects for the message handled.</returns>
LRESULT CALLBACK /*_export*/ Windows_Procedure(HWND hwnd, UINT message, UINT wParam, LONG lParam)
{

	/*
	 * The frame may be drawn scaled, so a click has to be matched against where the
	 * player sees the controls rather than where Windows finds them.
	 */
	{
		LPARAM translated_lparam;
		if (Route_Mouse_Message(hwnd, message, wParam, lParam, &translated_lparam)) {
			return(0);
		}
		lParam = (LONG)translated_lparam;
	}

	int	low_param = LOWORD(wParam);

	/*
	**	Pass on any messages intended for the winsock message handler.
	*/
	if ( PacketTransport ) {
		if ( message == (UINT) PacketTransport->Protocol_Event_Message() ) {
			if ( PacketTransport->Message_Handler (hwnd, message, wParam, lParam) ){
				return( DefWindowProc (hwnd, message, wParam, lParam) );
			}else{
				return(0);
			}
		}
	}

	Map.Message_Handler(hwnd, message, wParam, lParam);

	if (MainWindow) {
		GetMenu(MainWindow);
	}

	switch ( message ) {
		// Raised on request so that crash reporting can be exercised from inside window
		// procedure dispatch, which the operating system unwinds differently from a call.
		case WM_EXCEPTION_TEST:
			Exception_Wndproc_Test_Fault();
			return(0);

//		case WM_SYSKEYDOWN:
//			Mono_Printf("wparam=%08X lparam=%08X\n", (long)wParam, (long)lParam);
			// fall through

//		case WM_MOUSEMOVE:
//		case WM_KEYDOWN:
//		case WM_SYSKEYUP:
//		case WM_KEYUP:
//		case WM_LBUTTONDOWN:
//		case WM_LBUTTONUP:
//		case WM_LBUTTONDBLCLK:
//		case WM_MBUTTONDOWN:
//		case WM_MBUTTONUP:
//		case WM_MBUTTONDBLCLK:
//		case WM_RBUTTONDOWN:
//		case WM_RBUTTONUP:
//		case WM_RBUTTONDBLCLK:
//	 		Keyboard->Message_Handler(hwnd, message, wParam, lParam);
//			return(0);

		case WM_SHOWWINDOW:
			return(0);

		case WM_PAINT:
			if (GameInFocus == true || WindowedMode == true) {
				if (MouseCursor != NULL && VisibleSurface != NULL && HiddenSurface != NULL && CompositeSurface != NULL) {
					if (ScenarioActive == true) {
						Update_Visible_Surface(CompositeSurface);
						Map.Blit_Sidebar(true);
					} else if (Movie_Is_Playing() == true) {
						Movie_Update_Visible_Surface();
					} else {
						Update_Visible_Surface(HiddenSurface);
					}
				}
			}
			Video_Present_If_Dirty();
			ValidateRect(hwnd, NULL);
			break;

		case WM_ERASEBKGND:
			return(1);

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT && Win_Cursor_Handle_Set_Cursor()) {
				return(TRUE);
			}
			break;

		/*
		 * Window sizing and moving are answered from SDL's window events inside the
		 * pump, because a modal move or size loop dispatches messages without ever
		 * reaching this procedure, and the frame and the mouse bounds have to keep
		 * up through those too.
		 */
		case WM_DISPLAYCHANGE:
			Video_On_Display_Change();
			break;

		case WM_CLOSE:
			Close_Request_Handler();
			return(0);

			/*
			**	Windoze message says we have to shut down. Try and do it cleanly.
			*/
		case WM_DESTROY:
			if (ToolTips != NULL) {
				delete ToolTips;
				ToolTips = NULL;
			}
			MainWindow = 0;

			/*
			**	If we are shutting down gracefully than flag that the message loop has finished.
			**	If this is a forced shutdown (ReadyToQuit == 0) then try and close down everything
			**	before we exit.
			*/
			switch (ReadyToQuit) {
				default:
				case 1:
					ReadyToQuit = 2;
					break;

				case 0:
					break;

			}
			return(0);

		case WM_ACTIVATEAPP:
			if (hwnd == MainWindow && GameInFocus != (wParam != 0)) {
				GameInFocus = (wParam != 0);
				if (!GameInFocus) {
					Focus_Loss();
					DebugString("Focus lost\n");
				} else {
					Focus_Restore();
					DebugString("Focus gained\n");
				}
			}
			return(0);

		case WM_RBUTTONUP:
			Map.Set_Scroll_Coasting_Allowed(false);
			break;

		case WM_MOVING:
			return(On_WM_MOVING(hwnd, wParam, lParam));

		case WM_MOUSEWHEEL:
			if (!_MouseWheel) {
				_MouseWheel = true;
				if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) {
					Execute_Command("SidebarDown");
				} else {
					Execute_Command("SidebarUp");
				}
				_MouseWheel = false;
			}
			break;

		case WM_SYSCOMMAND:
			switch ( wParam ) {

				case SC_CLOSE:
					/*
					 * The title bar button and Alt-F4 arrive here; treat them
					 * as the close request they are.
					 */
					Close_Request_Handler();
					return(0);

				case SC_SCREENSAVE:
					/*
					**	Windoze is about to start the screen saver. If we just return without passing
					**	this message to DefWindowProc then the screen saver will not be allowed to start.
					*/
					return(0);
			}
			break;

	}

	/*
	**	Pass this message through to the keyboard handler. If the message
	**	was processed and requires no further action, then return with
	**	this information.
	*/
	if (Keyboard->Message_Handler(hwnd, message, wParam, lParam)) {
		return(0);
	}

	return(DefWindowProc (hwnd, message, wParam, lParam));
}


/// <summary>
/// Fetches the build number of this executable.
/// This routine is used by the network code to check that every machine joining a
/// game is running the same build.
/// </summary>
/// <returns>Returns with the packed project version this executable was built from.</returns>
unsigned int Build_Number(void)
{
	return(OPENTS_VERSION_PACKED);
}


/***********************************************************************************************
 * Create_Main_Window -- opens the MainWindow for C&C                                          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    instance -- handle to program instance                                            *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    10/10/95 4:08PM ST : Created                                                             *
 *=============================================================================================*/

#define WINDOW_NAME		"Tiberian Sun"


void Create_Main_Window ( HINSTANCE instance , int command_show , int width , int height )
{
	InitCommonControls();

	/*
	 * SDL owns the window now: it registers the window class, creates the real Win32
	 * window and pumps its messages, and MainWindow points at the native handle, which
	 * keeps DirectSound, the child controls, the hot key and the renderer working the
	 * way they always have. The game's message logic is fed from SDL's message hook
	 * inside msgloop.cpp.
	 */
	if (!SDL_Platform_Create_Window(width, height)) {
		MessageBox(NULL, "SDL could not create the game window.", WINDOW_NAME, MB_ICONERROR);
		ExitProcess(EXIT_FAILURE);
	}

	ShowCommand = command_show;

	/*
	 * The game has no Input Method Editor support of its own: an IME left in its native
	 * (Chinese etc.) mode swallows keystrokes into a composition window the game never
	 * sees. Switch the IME into its alphanumeric pass-through mode for the session and
	 * detach it from the main window so text input always reaches the game directly.
	 */
	HIMC himc = ImmGetContext(MainWindow);
	if (himc != NULL) {
		ImmSetOpenStatus(himc, FALSE);
		ImmSetConversionStatus(himc, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
		ImmReleaseContext(MainWindow, himc);
	}
	ImmAssociateContext(MainWindow, NULL);

	RegisterHotKey(MainWindow, 1, MOD_ALT|MOD_CONTROL|MOD_SHIFT, VK_M);

	/*
	 * The window exists by the time this returns, so the tooltip tracker is set up here
	 * rather than in the WM_CREATE branch the old window procedure used to see.
	 */
	ToolTips = new CCToolTip(MainWindow);
	if (ToolTips) {
		ToolTips->Set_Timer_Delay(500);
	}

	Audio.Audio_Focus_Loss_Function = Focus_Loss;

	DebugString("Main window is ready\n");
}


/// <summary>
/// Loads a title screen picture and centers it on the surface.
/// This routine is used by the startup and scenario loading sequences to put some
/// artwork on the screen while the game gets itself ready. A paletted picture is
/// drawn through a converter built from the palette supplied.
/// </summary>
/// <param name="name">The name of the picture file to load.</param>
/// <param name="surface">The surface to draw the title screen upon.</param>
/// <param name="palette">The palette to load the picture's colors into.</param>
void Load_Title_Screen(char const * name, Surface * surface, PaletteClass * palette)
{
	Surface *load_buffer;
	CCFileClass file(name);
	load_buffer = Read_PCX_File (file, palette);

	if (load_buffer) {
		Point2D point;
		int x = (surface->Get_Width() - load_buffer->Get_Width()) / 2;
		int y = (surface->Get_Height() - load_buffer->Get_Height()) / 2;
		if (palette && load_buffer->Bytes_Per_Pixel() == 1) {
			ConvertClass *drawer = new ConvertClass(*palette, *palette, *surface);
			Blit_Block(*surface, *drawer, *load_buffer, load_buffer->Get_Rect(), Point2D(x, y), surface->Get_Rect());
			delete drawer;
		} else {

			surface->Blit_From(surface->Get_Rect(), Rect(x, y, load_buffer->Get_Width(), load_buffer->Get_Height()), *load_buffer, load_buffer->Get_Rect(), load_buffer->Get_Rect());
		}
		delete load_buffer;
	}
}

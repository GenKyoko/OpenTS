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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/msgloop.cpp                            $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 2/05/02 1:17p                                               $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Add_Accelerator -- Adds a keyboard accelerator to the message handler.                    *
 *   Add_Modeless_Dialog -- Adds a modeless dialog box to the message handler.                 *
 *   Remove_Accelerator -- Removes an accelerator from the message processor.                  *
 *   Remove_Modeless_Dialog -- Removes the dialog box from the message tracking handler.       *
 *   Windows_Message_Handler -- Handles windows message.                                       *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "msgloop.h"

#include "_tooltip.h"
#include "cctooltip.h"
#include "sdlplatform.h"
#include "vector.h"
#include "video.h"
#include "winstub.h"

#include <SDL3/SDL_system.h>


/*
**	Tracks modeless dialog box messages by keeping a record of all active modeless dialog
**	box handles and then determining if the windows message applies to the dialog box. If it
**	does, then the default message handling should not be performed.
*/
DynamicVectorClass<HWND> _ModelessDialogs;


/*
**	Tracks windows accelerators with this structure.
*/
struct AcceleratorTracker {
	AcceleratorTracker(HWND window = NULL, HACCEL accelerator = NULL) : Accelerator(accelerator), Window(window) {}

	int operator == (AcceleratorTracker const & acc) const {return(Accelerator == acc.Accelerator && Window == acc.Window);}
	int operator != (AcceleratorTracker const & acc) const {return(!(*this == acc));}

	HACCEL Accelerator;
	HWND Window;
};
static DynamicVectorClass<AcceleratorTracker> _Accelerators;


/*
**	In those cases where message intercept needs to occur but not for purposes
**	of a modeless dialog box or a windows accelerator, then this is a function
**	pointer to than message intercept handler.
*/
bool (*Message_Intercept_Handler)(MSG &msg) = NULL;


LRESULT CALLBACK Windows_Procedure(HWND hwnd, UINT message, UINT wParam, LONG lParam);


static bool _MessageHookInstalled = false;


/// <summary>
/// Delivers every Windows message SDL pumps to the game's message logic.
/// SDL owns the message pump now, and it calls this hook for each message it takes
/// off the queue, before translation and dispatch. The filtering the old pump did --
/// tooltips, modeless dialogs, accelerators, the intercept handler -- happens here,
/// and a message one of them consumed returns false so it never dispatches.
/// The main window's game logic rides on this callback too: its messages used to
/// arrive at the registered window procedure, so they are handed there directly.
/// Everything else returns true and is dispatched to the window it names.
/// </summary>
/// <returns>bool; Should SDL carry the message on to translation and dispatch?</returns>
static bool SDLCALL Game_Message_Hook(void * userdata, MSG * msg)
{
	if (ToolTips != NULL) {
		ToolTips->Message_Handler(msg);
	}

	/*
	**	Pass the windows message through any modeless dialogs that may
	**	be active. If one of the dialogs processes the message, then
	**	it must not be processed any further.
	*/
	for (int index = 0; index < _ModelessDialogs.Count(); index++) {
		if (IsDialogMessage(_ModelessDialogs[index], msg)) {
			return(false);
		}
	}

	/*
	**	Pass the message through any loaded accelerators. If the message
	**	was processed by an accelerator, then it doesn't need to be
	**	processed any further.
	*/
	for (int aindex = 0; aindex < _Accelerators.Count(); aindex++) {
		if (TranslateAccelerator(_Accelerators[aindex].Window, _Accelerators[aindex].Accelerator, msg)) {
			return(false);
		}
	}

	/*
	**	If the message was not handled by any normal intercept handlers, then
	**	submit the message to a custom message handler if one has been provided.
	*/
	if (Message_Intercept_Handler != NULL && Message_Intercept_Handler(*msg)) {
		return(false);
	}

	/*
	**	The main window's messages are the game's own: mouse routing, the keyboard
	**	buffer, focus, painting and closing all run out of the window procedure.
	*/
	if (msg->hwnd == MainWindow && MainWindow != NULL) {
		Windows_Procedure(msg->hwnd, msg->message, (UINT)msg->wParam, (LONG)msg->lParam);
	}

	return(true);
}


/***********************************************************************************************
 * Windows_Message_Handler -- Handles windows message.                                         *
 *                                                                                             *
 *    This routine will take all messages that have accumulated in the message queue and       *
 *    dispatch them to their respective recipients. When the message queue has been emptied,   *
 *    then this routine will return. By using this routine, it is possible to have the main    *
 *    program run in the main thread and yet still have it behave like a normal program as     *
 *    far as message handling is concerned. To achieve this, this routine must be called on    *
 *    a semi-frequent basis (a few times a second is plenty).                                  *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/17/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
void Windows_Message_Handler(void)
{
	if (MainWindow == 0) return;

	/*
	**	SDL pumps the Windows message queue as it polls its own events, and every
	**	message it takes off the queue goes through the hook above, which feeds the
	**	game's message logic and the dialogs. The SDL events themselves carry nothing
	**	the game still needs, so polling only drains them.
	*/
	if (!_MessageHookInstalled) {
		SDL_SetWindowsMessageHook(Game_Message_Hook, NULL);
		_MessageHookInstalled = true;
	}

	SDL_Pump_Game_Events();

	/*
	 * The menus, the loading screens and the score screens all draw and then come back
	 * here rather than through the game's own present, so this is where their frames
	 * reach the screen.
	 */
	Video_Present_If_Dirty();
}


/***********************************************************************************************
 * Add_Modeless_Dialog -- Adds a modeless dialog box to the message handler.                   *
 *                                                                                             *
 *    When a modeless dialog box becomes active, the messages processed by the main message    *
 *    handler must be handled different. This routine is used to inform the message handler    *
 *    that a dialog box is active and messages must be fed to it as appropriate.               *
 *                                                                                             *
 * INPUT:   dialog   -- Handle to the modeless dialog box.                                     *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   The modeless dialog box must be removed from the tracking system by calling     *
 *             Remove_Modeless_Dialog. Failure to do so when the dialog is destroyed will      *
 *             result in undefined behavior.                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/17/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
void Add_Modeless_Dialog(HWND dialog)
{
	_ModelessDialogs.Add(dialog);
}


/***********************************************************************************************
 * Remove_Modeless_Dialog -- Removes the dialog box from the message tracking handler.         *
 *                                                                                             *
 *    This routine must be called when a modeless dialog is being removed.                     *
 *                                                                                             *
 * INPUT:   dialog   -- Handle to the modeless dialog that was previously submitted to         *
 *                      Add_Modeless_Dialog().                                                 *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   Failure to call this routine will result in undefined behavior when the dialog  *
 *             is destroyed.                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/17/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
void Remove_Modeless_Dialog(HWND dialog)
{
	_ModelessDialogs.Delete(dialog);
}


/// <summary>
/// Fetches a tracked modeless dialog by its window title.
/// This routine searches the dialogs submitted by Add_Modeless_Dialog for one whose caption
/// matches the name given. Use this routine when only the title of the dialog is known.
/// </summary>
/// <param name="name">The window title of the dialog to look for.</param>
/// <returns>Returns with the handle of the matching dialog, or NULL if no tracked dialog
/// carries that title.</returns>
HWND Get_Modeless_Dialog_From_Name(const char *name)
{
	static char _wname[100];

	for (int i = 0; i < _ModelessDialogs.Count(); i++) {
		GetWindowText(_ModelessDialogs[i], _wname, sizeof(_wname) - 1);
		if (!strcmp(_wname, name)) {
			return(_ModelessDialogs[i]);
		}
	}
	return(NULL);
}


/***********************************************************************************************
 * Add_Accelerator -- Adds a keyboard accelerator to the message handler.                      *
 *                                                                                             *
 *    This routine will add a keyboard accelerator to the tracking process for the message     *
 *    handler. If the incoming message is processed by an accelerator, then the normal         *
 *    processing must be altered. By using this routine, the proper behavior of accelerators   *
 *    is maintained.                                                                           *
 *                                                                                             *
 * INPUT:   window   -- The window that the accelerator belongs to. Each accelerator must be   *
 *                      assigned to a window.                                                  *
 *                                                                                             *
 *          accelerator -- The handler to the windows accelerator.                             *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   When the accelerator is no longer valid (or the controlling window as been      *
 *             destroyed), the Remove_Accelerator function must be called.                     *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/17/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
void Add_Accelerator(HWND window, HACCEL accelerator)
{
	_Accelerators.Add(AcceleratorTracker(window, accelerator));
}


/***********************************************************************************************
 * Remove_Accelerator -- Removes an accelerator from the message processor.                    *
 *                                                                                             *
 *    This routine must be called when the accelerator or the window it was attached to has    *
 *    been destroyed.                                                                          *
 *                                                                                             *
 * INPUT:   accelerator -- The accelerator to remove from the tracking system.                 *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   This routine presumes that the accelerator will not be shared between windows.  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/17/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
void Remove_Accelerator(HACCEL accelerator)
{
	for (int index = 0; index < _Accelerators.Count(); index++) {
		if (_Accelerators[index].Accelerator == accelerator) {
			_Accelerators.Delete_Index(index);
			break;
		}
	}
}

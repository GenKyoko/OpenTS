/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// C port of the retired smartdeform_.asm. The assembly supplied the ripple
// smoothing entry point Asm_Ripple_Deform_Points and the four DeformPoint*
// globals that smartdeform.cpp consumes. The translation reproduces the
// assembly control flow, including its quirks; the notes below call out where
// the assembly's Flags dword maps onto the struct's bool fields.

#include "always.h"

#include "smartdeform.h"


// smartdeform_.asm: RAMP_HALF_HEIGHT equ 12. Kept as a local so this file does
// not depend on the tile constants that smartdeform.cpp derives the same value
// from.
constexpr int RAMP_HALF_HEIGHT = 12;


extern "C" {

// .DATA from smartdeform_.asm; smartdeform.cpp treats these as externs.
int DeformPointHeight = 0;
int DeformPointWidth = 0;
int DeformPointXAdd = 0;
int DeformPointYAdd = 0;


bool Asm_Ripple_Deform_Points(int startpointx, int startpointy, int general_direction, bool forced)
{
	DeformPointStruct & start_point = DeformPoints[startpointy * DeformPointWidth + startpointx];
	int startheight = start_point.Height;

	// The assembly walked a 3x3 neighborhood with byte counters, reusing
	// FLAG_DONE (2) as the loop bound, so the offsets run -1..1 and the
	// center is skipped.
	for (int dy = -1; dy < 2; dy++) {
		for (int dx = -1; dx < 2; dx++) {
			if ((dx | dy) == 0) continue;

			int x = startpointx + dx;
			int y = startpointy + dy;
			DeformPointStruct & point = DeformPoints[y * DeformPointWidth + x];

			// FLAG_RIGID is the Rigid field. A rigid point stops the ripple
			// unless it is being forced through, in which case it is skipped.
			if (point.Rigid) {
				if (forced) continue;
				if (abs(point.Height - startheight) > RAMP_HALF_HEIGHT) {
					return(false);
				}
				continue;
			}

			if (abs(point.Height - startheight) <= RAMP_HALF_HEIGHT) continue;

			if (general_direction == 1) {
				if (point.Height < startheight) {
					point.Height = startheight - RAMP_HALF_HEIGHT;
					// FLAG_DONE was write-only in the assembly; the C++ flow
					// reads Done, so the adjusted-point mark lands there.
					point.Done = true;
				}
			} else {
				if (point.Height > startheight) {
					point.Height = startheight + RAMP_HALF_HEIGHT;
					point.Done = true;
				}
			}

			if (!Asm_Ripple_Deform_Points(x, y, general_direction, forced)) {
				if (!forced) {
					return(false);
				}
			}
		}
	}
	return(true);
}

}

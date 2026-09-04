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

// C port of the retired cliprect.asm. The assembly supplied the two rectangle
// clipping helpers declared in misc.h. The translation reproduces the
// assembly's arithmetic and quirks; each one is called out where it lives.
// The boundary math is done on the unsigned view of the 32-bit values so the
// wrap-around the assembly relies on stays defined on every architecture.

#include "always.h"


// Sign bit of a 32-bit value, the way `shld` read it (bit 31).
static int SignBit(unsigned int v)
{
	return (int)(v >> 31);
}

extern "C" {

// Cohen-Sutherland clipping of a rectangle against a window. Returns 0 when
// the rectangle is fully inside, -1 when it is fully outside (or clipped to a
// zero-size rectangle), and 1 when it was clipped; *x/*y/*dw/*dh are updated
// in the clipped case.
int __cdecl Clip_Rect(int * x, int * y, int * dw, int * dh, int width, int height)
{
	unsigned int const wx = (unsigned int)width;
	unsigned int const hy = (unsigned int)height;

	int x0 = *x;
	int y0 = *y;
	int x1 = (int)((unsigned int)x0 + (unsigned int)*dw);
	int y1 = (int)((unsigned int)y0 + (unsigned int)*dh);

	// Sutherland codes for the (x0,y0) and (x1,y1) corners, built exactly as
	// the assembly does: each bit is the sign of a boundary difference, then
	// bits 2 and 0 are complemented. The result reads left / right / bottom /
	// top overflow.
	int code0 = (SignBit((unsigned int)x0) << 3)
		| (SignBit((unsigned int)x0 - wx - 1u) << 2)
		| (SignBit((unsigned int)y0) << 1)
		| SignBit((unsigned int)y0 - hy - 1u);
	int code1 = (SignBit((unsigned int)x1) << 3)
		| (SignBit((unsigned int)x1 - wx - 1u) << 2)
		| (SignBit((unsigned int)y1) << 1)
		| SignBit((unsigned int)y1 - hy - 1u);
	code0 ^= 5;
	code1 ^= 5;

	if ((code0 & code1) != 0) {
		return -1;    // trivially rejected
	}
	if ((code0 | code1) == 0) {
		return 0;     // trivially accepted
	}

	if ((code0 & 8) != 0) {
		// Left edge: *x to 0, *dw grows by the (negative) original x0.
		*x = 0;
		*dw = (int)((unsigned int)*dw + (unsigned int)x0);
	}
	if ((code0 & 2) != 0) {
		// Bottom edge: same adjustment on y.
		*y = 0;
		*dh = (int)((unsigned int)*dh + (unsigned int)y0);
	}
	if ((code1 & 4) != 0) {
		// Right edge: *dw becomes width - *x, read after any left clip. A
		// non-positive result loses the rectangle's width, so the function
		// reports outside; *dw is still written, as the assembly did.
		int nw = (int)((unsigned int)width - (unsigned int)*x);
		*dw = nw;
		if (nw <= 0) {
			return -1;
		}
	}
	if ((code1 & 1) != 0) {
		// Top edge: same on height.
		int nh = (int)((unsigned int)height - (unsigned int)*y);
		*dh = nh;
		if (nh <= 0) {
			return -1;
		}
	}
	return 1;
}

// Confine_Rect shifts a rectangle into the window without resizing it; dw and
// dh are values, not pointers. Returns 1 when either axis was moved, else 0.
// The banner's warning stands: a rectangle wider or taller than the window
// gives a meaningless result.
int __cdecl Confine_Rect(int * x, int * y, int dw, int dh, int width, int height)
{
	int result = 0;

	// X axis: skip when the near edge is already positive and the far edge
	// fits; otherwise move to width - dw when the near edge is past zero, or
	// to 0 when it is not.
	{
		unsigned int const wx = (unsigned int)width;
		unsigned int const x0 = (unsigned int)*x;
		unsigned int esi = 0u - x0;                   // -x
		unsigned int edi = (x0 + (unsigned int)dw) - wx - 1u;   // x + dw - width - 1
		if ((int)(esi & edi) >= 0) {
			result = 1;
			if ((int)esi < 0) {                       // x > 0
				*x = (int)(x0 - (edi + 1u));          // width - dw
			} else {
				*x = 0;
			}
		}
	}
	// Y axis: the same shift, and it is the last write of the function.
	{
		unsigned int const hy = (unsigned int)height;
		unsigned int const y0 = (unsigned int)*y;
		unsigned int esi = 0u - y0;
		unsigned int edi = (y0 + (unsigned int)dh) - hy - 1u;
		if ((int)(esi & edi) >= 0) {
			result = 1;
			if ((int)esi < 0) {
				*y = (int)(y0 - (edi + 1u));
			} else {
				*y = 0;
			}
		}
	}
	return result;
}

}

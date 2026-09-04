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

// C port of the retired unvq_asm.asm: the assembly UnVQ1 full-frame block
// decoders selected by the VQA drawer and movie playback. Every routine reads
// a 16-bit block code, dispatches between a codebook copy and a solid-color
// fill, and walks the destination in block rows. The translation reproduces
// the assembly arithmetic byte for byte, including its quirks; each one is
// called out at the spot it lives so the reader can tell preserved behavior
// from a reconstruction artifact.

#include "always.h"

#include "_vqa.h"
#include "vqalib/unvq.h"

#include <cstdint>

// ---------------------------------------------------------------------------
// Shared block-row skeleton
// ---------------------------------------------------------------------------
//
// All ten functions run the same outer loop as the assembly:
//   - `entries` is the number of blocks (numrows * blocksperrow), which is
//     also the size in bytes of the low half of the pointer stream.
//   - The 16-bit block code is split across two pointer arrays: the low byte
//     comes from `pointers[pi]` and the high byte from `pointers[pi +
//     entries]`; esi only ever advanced through the low half. The pointer
//     buffer therefore holds `entries` low bytes followed by `entries` high
//     bytes.
//   - Each row consumes exactly `blocksperrow` codes (do-while, so a zero
//     count wraps to 2^32 iterations exactly as `dec ecx; jnz` did), then the
//     destination cursor advances by `rowoffset` until all `entries` codes
//     are consumed.
//
// ColorMode 0 (C0, 8-bit) routines treat the block code's high byte 0xFF as
// a one-color block whose color is the low byte; ColorMode 1 (C1, 16-bit)
// routines treat the sign bit of the code as the one-color marker and keep
// the 15-bit index in the low 15 bits.

// ===========================================================================
// ASM_UnVQ1_C1_TABLE -- 4x4 block, C1 via HicolorTable fill
// ===========================================================================
// One-color fill looks the 15-bit index up in HicolorTable; the resulting
// 16-bit pixel is written as a replicated pair. Multi-color blocks copy four
// 8-byte rows (4 x 16-bit pixels each) from a 32-byte codeword.

void __cdecl ASM_UnVQ1_C1_TABLE(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t rowoffset = bufwidth * 4u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if (idx & 0x8000u) {
				uint32_t pixel = HicolorTable[idx & 0x7FFFu];
				uint32_t fill = (pixel << 16) | pixel;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + 4) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
				*(uint32_t *)(dst + bufwidth + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 2u) = fill;
				*(uint32_t *)(dst + bufwidth * 2u + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 3u) = fill;
				*(uint32_t *)(dst + bufwidth * 3u + 4) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 5);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + 4) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 8);
				*(uint32_t *)(dst + bufwidth + 4) = *(const uint32_t *)(cb + 12);
				*(uint32_t *)(dst + bufwidth * 2u) = *(const uint32_t *)(cb + 16);
				*(uint32_t *)(dst + bufwidth * 2u + 4) = *(const uint32_t *)(cb + 20);
				*(uint32_t *)(dst + bufwidth * 3u) = *(const uint32_t *)(cb + 24);
				*(uint32_t *)(dst + bufwidth * 3u + 4) = *(const uint32_t *)(cb + 28);
			}

			dst += 8;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ1_C1_TABLE_ALT -- 4x2 block, C1 via HicolorTable fill
// ===========================================================================
// The "alternate" 4x2 decoder writes its second row at 2*bufwidth and steps
// block rows by 4*bufwidth, leaving an empty scanline between used ones. The
// 32-byte codeword is read at rows 0 and 2 (cb+16). Both quirks are kept.

void __cdecl ASM_UnVQ1_C1_TABLE_ALT(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t rowoffset = bufwidth * 4u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if (idx & 0x8000u) {
				uint32_t pixel = HicolorTable[idx & 0x7FFFu];
				uint32_t fill = (pixel << 16) | pixel;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 2u) = fill;
				*(uint32_t *)(dst + bufwidth * 2u + 4) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 5);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + 4) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth * 2u) = *(const uint32_t *)(cb + 16);
				*(uint32_t *)(dst + bufwidth * 2u + 4) = *(const uint32_t *)(cb + 20);
			}

			dst += 8;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_4x2 -- 4x2 block, C0 (8-bit)
// ===========================================================================
// One-color blocks replicate the low byte across all four pixels of both
// rows. Multi-color blocks copy two 4-pixel rows from an 8-byte codeword.

void __cdecl ASM_UnVQ_4x2(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	uint32_t rowoffset = bufwidth * 2u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if ((idx & 0xFF00u) == 0xFF00u) {
				uint32_t color = idx & 0xFFu;
				uint32_t fill = (color << 16) | color;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 3);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 4);
			}

			dst += 4;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_4x4 -- 4x4 block, C0 (8-bit)
// ===========================================================================
// Multi-color blocks copy four 4-pixel rows from a 16-byte codeword.

void __cdecl ASM_UnVQ_4x4(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	uint32_t rowoffset = bufwidth * 4u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if ((idx & 0xFF00u) == 0xFF00u) {
				uint32_t color = idx & 0xFFu;
				uint32_t fill = (color << 16) | color;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
				*(uint32_t *)(dst + bufwidth * 2u) = fill;
				*(uint32_t *)(dst + bufwidth * 3u) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 4);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth * 2u) = *(const uint32_t *)(cb + 8);
				*(uint32_t *)(dst + bufwidth * 3u) = *(const uint32_t *)(cb + 12);
			}

			dst += 4;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_4x4_HALF -- 4x4 block sampled to 2x2, C0 (8-bit)
// ===========================================================================
// Half-resolution path: each output row samples codebook columns 0 and 2, and
// only codebook rows 0 and 2 are used, giving one output row per two source
// rows.

void __cdecl ASM_UnVQ_4x4_HALF(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	uint32_t rowoffset = bufwidth * 2u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if ((idx & 0xFF00u) == 0xFF00u) {
				uint32_t color = idx & 0xFFu;
				uint16_t fill = (uint16_t)((color << 8) | color);
				*(uint16_t *)(dst + 0) = fill;
				*(uint16_t *)(dst + bufwidth) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 4);
				uint16_t row0 = (uint16_t)((uint16_t)cb[2] << 8) | cb[0];
				uint16_t row1 = (uint16_t)((uint16_t)cb[10] << 8) | cb[8];
				*(uint16_t *)(dst + 0) = row0;
				*(uint16_t *)(dst + bufwidth) = row1;
			}

			dst += 2;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_6 -- 4x2 block sampled to 2x2, C0 (8-bit)
// ===========================================================================
// Half-resolution 4x2 decoder. The second output row reads codebook bytes
// cb+8 and cb+10, which lie past the 8-byte codeword: it pulls the first two
// pixels of the *next* codeword. Reproduced as written.

void __cdecl ASM_UnVQ_6(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	uint32_t rowoffset = bufwidth * 2u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if ((idx & 0xFF00u) == 0xFF00u) {
				uint32_t color = idx & 0xFFu;
				uint16_t fill = (uint16_t)((color << 8) | color);
				*(uint16_t *)(dst + 0) = fill;
				*(uint16_t *)(dst + bufwidth) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 3);
				uint16_t row0 = (uint16_t)((uint16_t)cb[2] << 8) | cb[0];
				uint16_t row1 = (uint16_t)((uint16_t)cb[10] << 8) | cb[8];
				*(uint16_t *)(dst + 0) = row0;
				*(uint16_t *)(dst + bufwidth) = row1;
			}

			dst += 2;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ1_C1_4x4 -- 4x4 block, C1 direct fill
// ===========================================================================
// Same multi-color path as ASM_UnVQ1_C1_TABLE; one-color blocks use the
// 15-bit index itself as the 16-bit pixel instead of a HicolorTable lookup.

void __cdecl ASM_UnVQ1_C1_4x4(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t rowoffset = bufwidth * 4u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if (idx & 0x8000u) {
				uint32_t pixel = idx & 0x7FFFu;
				uint32_t fill = (pixel << 16) | pixel;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + 4) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
				*(uint32_t *)(dst + bufwidth + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 2u) = fill;
				*(uint32_t *)(dst + bufwidth * 2u + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 3u) = fill;
				*(uint32_t *)(dst + bufwidth * 3u + 4) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 5);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + 4) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 8);
				*(uint32_t *)(dst + bufwidth + 4) = *(const uint32_t *)(cb + 12);
				*(uint32_t *)(dst + bufwidth * 2u) = *(const uint32_t *)(cb + 16);
				*(uint32_t *)(dst + bufwidth * 2u + 4) = *(const uint32_t *)(cb + 20);
				*(uint32_t *)(dst + bufwidth * 3u) = *(const uint32_t *)(cb + 24);
				*(uint32_t *)(dst + bufwidth * 3u + 4) = *(const uint32_t *)(cb + 28);
			}

			dst += 8;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_8 -- 4x2 block, C1 direct fill
// ===========================================================================
// Two 8-byte rows from a 16-byte codeword; one-color blocks fill both rows
// with the 15-bit index replicated as a 16-bit pixel pair.

void __cdecl ASM_UnVQ_8(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t rowoffset = bufwidth * 2u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if (idx & 0x8000u) {
				uint32_t pixel = idx & 0x7FFFu;
				uint32_t fill = (pixel << 16) | pixel;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + 4) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
				*(uint32_t *)(dst + bufwidth + 4) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 4);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + 4) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 8);
				*(uint32_t *)(dst + bufwidth + 4) = *(const uint32_t *)(cb + 12);
			}

			dst += 8;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_9 -- 4x8 block, C0 (8-bit)
// ===========================================================================
// Eight 4-pixel rows from a 32-byte codeword; rowoffset covers eight
// scanlines. One-color blocks fill all eight rows.

void __cdecl ASM_UnVQ_9(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	uint32_t rowoffset = bufwidth * 8u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if ((idx & 0xFF00u) == 0xFF00u) {
				uint32_t color = idx & 0xFFu;
				uint32_t fill = (color << 16) | color;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
				*(uint32_t *)(dst + bufwidth * 2u) = fill;
				*(uint32_t *)(dst + bufwidth * 3u) = fill;
				*(uint32_t *)(dst + bufwidth * 4u) = fill;
				*(uint32_t *)(dst + bufwidth * 5u) = fill;
				*(uint32_t *)(dst + bufwidth * 6u) = fill;
				*(uint32_t *)(dst + bufwidth * 7u) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 5);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth * 2u) = *(const uint32_t *)(cb + 8);
				*(uint32_t *)(dst + bufwidth * 3u) = *(const uint32_t *)(cb + 12);
				*(uint32_t *)(dst + bufwidth * 4u) = *(const uint32_t *)(cb + 16);
				*(uint32_t *)(dst + bufwidth * 5u) = *(const uint32_t *)(cb + 20);
				*(uint32_t *)(dst + bufwidth * 6u) = *(const uint32_t *)(cb + 24);
				*(uint32_t *)(dst + bufwidth * 7u) = *(const uint32_t *)(cb + 28);
			}

			dst += 4;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}


// ===========================================================================
// ASM_UnVQ_10 -- 8x4 block ("4x4-wide"), C0 (8-bit)
// ===========================================================================
// Four 8-pixel rows from a 32-byte codeword; one-color blocks fill both
// dwords of each row.

void __cdecl ASM_UnVQ_10(unsigned char * codebook, unsigned char * pointers,
		unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows,
		unsigned long bufwidth)
{
	uint32_t rowoffset = bufwidth * 4u;
	uint32_t entries = numrows * blocksperrow;

	uint32_t pi = 0;
	uint8_t * dst = buffer;
	uint8_t * row_base = buffer;

	while (true) {
		uint32_t blocks = blocksperrow;
		do {
			uint32_t idx = ((uint32_t)pointers[pi + entries] << 8) | pointers[pi];
			pi++;

			if ((idx & 0xFF00u) == 0xFF00u) {
				uint32_t color = idx & 0xFFu;
				uint32_t fill = (color << 16) | color;
				*(uint32_t *)(dst + 0) = fill;
				*(uint32_t *)(dst + 4) = fill;
				*(uint32_t *)(dst + bufwidth) = fill;
				*(uint32_t *)(dst + bufwidth + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 2u) = fill;
				*(uint32_t *)(dst + bufwidth * 2u + 4) = fill;
				*(uint32_t *)(dst + bufwidth * 3u) = fill;
				*(uint32_t *)(dst + bufwidth * 3u + 4) = fill;
			} else {
				const uint8_t * cb = codebook + (idx << 5);
				*(uint32_t *)(dst + 0) = *(const uint32_t *)(cb + 0);
				*(uint32_t *)(dst + 4) = *(const uint32_t *)(cb + 4);
				*(uint32_t *)(dst + bufwidth) = *(const uint32_t *)(cb + 8);
				*(uint32_t *)(dst + bufwidth + 4) = *(const uint32_t *)(cb + 12);
				*(uint32_t *)(dst + bufwidth * 2u) = *(const uint32_t *)(cb + 16);
				*(uint32_t *)(dst + bufwidth * 2u + 4) = *(const uint32_t *)(cb + 20);
				*(uint32_t *)(dst + bufwidth * 3u) = *(const uint32_t *)(cb + 24);
				*(uint32_t *)(dst + bufwidth * 3u + 4) = *(const uint32_t *)(cb + 28);
			}

			dst += 8;
			blocks--;
		} while (blocks != 0);

		dst = row_base + rowoffset;
		row_base = dst;
		if (pi >= entries) {
			break;
		}
	}
}

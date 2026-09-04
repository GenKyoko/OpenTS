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

// C port of the retired vqa_uncomp.asm, the VQA player's audio and LCW
// decompression. AudioUnzap expands the zapped-audio delta codes; the LCW
// entry points expand the run-length encoding used for palettes, codebooks,
// and pointer chunks. The translation keeps the assembly's arithmetic and
// quirks, each called out where it lives.
//
// Audio code byte: the top two bits select 2-bit deltas (0), 4-bit deltas
// (1), raw samples (2), or a zero-delta run (3); the low six bits carry the
// unit count. The assembly increments that count before dispatching, so the
// 4-bit, 2-bit, and zero-delta paths all process count+1 units.
//
// LCW command codes (b = byte, w = little-endian word):
//   n=0xxxyyyy,w    short run    copy (n>>4)+3 bytes from dest - ((n&0xF)<<8|w)
//   n=10000000      end of data
//   n=10xxxxxx      medium copy  copy (n&0x3F) bytes from source
//   n=11xxxxxx,w    medium run   copy (n&0x3F)+3 bytes from dest + w
//   n=11111110,w,b  long run     fill w bytes with b
//   n=11111111,w1,w2 long copy   copy w1 bytes from dest + w2
// VQA_LCW_Uncompress reads the run/copy offsets relative to dest and treats a
// leading 0x00 code byte as the marker for that mode; otherwise offsets are
// absolute from the start of dest.

#include "always.h"

#include "vqalib/cmp.h"

enum {
	CODE_2BIT = 0,
	CODE_4BIT = 1,
	CODE_RAW = 2,
	CODE_SILENCE = 3,
};

// Delta tables, byte for byte the assembly's _2bitdecode / _4bitdecode.
static signed char const _2bitdecode[4] = { -2, -1, 0, 1 };
static signed char const _4bitdecode[16] = { -9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8 };

// Saturated 8-bit add, matching the assembly's `add dl,[table]` followed by
// the carry/borrow clamps: results above 255 become 255, below 0 become 0.
static unsigned int SatAdd8(unsigned int value, signed char delta)
{
	int sum = (int)value + (int)delta;
	if (sum < 0) {
		return 0;
	}
	if (sum > 255) {
		return 255;
	}
	return (unsigned int)sum;
}

// LCW decode core shared by the two entry points. `relative` selects the
// relative-offset mode; `legacy_offsets` reproduces OLD_VQA_LCW_Uncompress,
// which fails to clear the remaining-count register's high 16 bits before
// reading the short-run and medium-run offset operands. The 0xFF long copy
// clears the register first, so its offset stays clean. Both quirks are
// preserved; only the medium-run and short-run offsets are affected.
static unsigned long LCW_Decode(unsigned char const * src, unsigned char * dst,
		unsigned long length, bool relative, bool legacy_offsets)
{
	unsigned char * const a1stdest = dst;
	unsigned char * const lastbyte = dst + length;

	while (dst < lastbyte) {
		unsigned long remaining = (unsigned long)(lastbyte - dst);
		unsigned int code = *src++;

		if ((code & 0x80) == 0) {
			// Short run: copy (code>>4)+3 bytes from dst - offset.
			unsigned long count = (code >> 4) + 3;
			if (count > remaining) {
				count = remaining;
			}
			unsigned long offset = ((unsigned long)(code & 0x0F) << 8) | *src++;
			if (legacy_offsets) {
				offset |= remaining & 0xFFFF0000ul;
			}
			unsigned char * copy = dst - offset;
			while (count--) {
				*dst++ = *copy++;
			}
		} else if ((code & 0x40) == 0) {
			if (code == 0x80) {
				break;    // end of data
			}
			// Medium copy: copy (code & 0x3F) bytes from source.
			unsigned long count = code & 0x3F;
			if (count > remaining) {
				count = remaining;
			}
			while (count--) {
				*dst++ = *src++;
			}
		} else if (code == 0xFE) {
			// Long run: fill the word count with the following byte.
			unsigned long count = (unsigned long)(*src | ((unsigned int)src[1] << 8));
			src += 2;
			unsigned char fill = *src++;
			if (count > remaining) {
				count = remaining;
			}
			while (count--) {
				*dst++ = fill;
			}
		} else if (code == 0xFF) {
			// Long copy: w1 bytes from dest + w2.
			unsigned long count = (unsigned long)(*src | ((unsigned int)src[1] << 8));
			unsigned long index = (unsigned long)(src[2] | ((unsigned int)src[3] << 8));
			src += 4;
			if (count > remaining) {
				count = remaining;
			}
			unsigned char * copy = relative ? dst - index : a1stdest + index;
			while (count--) {
				*dst++ = *copy++;
			}
		} else {
			// Medium run: copy (code & 0x3F) + 3 bytes from dest + w.
			unsigned long count = (code & 0x3F) + 3;
			unsigned long index = (unsigned long)(*src | ((unsigned int)src[1] << 8));
			src += 2;
			if (count > remaining) {
				count = remaining;
			}
			unsigned char * copy;
			if (relative) {
				copy = dst - index;
			} else {
				copy = a1stdest + index;
				if (legacy_offsets) {
					copy += (remaining & 0xFFFF0000ul);
				}
			}
			while (count--) {
				*dst++ = *copy++;
			}
		}
	}

	return (unsigned long)(dst - a1stdest);
}

extern "C" {

// Decompress a zapped audio sample into dest. The count is a byte budget for
// dest, not a sample count; the loop stops when it is exhausted. Returns the
// number of bytes consumed from source (the assembly's incount), not the
// number produced, despite the historical banner's wording. This core is
// shared with Decompress_Frame: the retired auduncmp.asm implements the same
// algorithm byte for byte.
static long Unzap_Audio(void *source, void *dest, long count)
{
	if (source == nullptr || dest == nullptr || count == 0) {
		return 0;
	}

	unsigned char const * src = (unsigned char const *)source;
	unsigned char * dst = (unsigned char *)dest;
	long remaining = count;
	unsigned char prev = 0x80;    // starting 'previous' sample
	long incount = 0;

	while (remaining > 0) {
		unsigned int code = *src++;
		incount++;
		unsigned int sub = code & 0x3F;    // AL
		unsigned int c = code >> 6;        // AH

		if (c == CODE_RAW) {
			if ((sub & 0x20) != 0) {
				// A 5-bit signed delta, added to 'previous' with wrap-around
				// (this path does not saturate).
				int delta = sub & 0x1F;
				if ((delta & 0x10) != 0) {
					delta -= 0x20;
				}
				prev = (unsigned char)(prev + delta);
				*dst++ = prev;
				remaining--;
			} else {
				// Copy sub+1 raw samples; 'previous' becomes the last byte.
				unsigned int n = sub + 1;
				for (unsigned int i = 0; i < n; i++) {
					*dst++ = *src++;
				}
				incount += n;
				remaining -= n;
				prev = dst[-1];
			}
			continue;
		}

		// The assembly increments AL before distinguishing the 4-bit, 2-bit,
		// and zero-delta codes, so all three process sub+1 units.
		unsigned int n = sub + 1;
		if (c == CODE_4BIT) {
			// sub+1 bytes of nibble-paired deltas, two samples per byte.
			for (unsigned int i = 0; i < n; i++) {
				unsigned int b = *src++;
				incount++;
				unsigned int s1 = SatAdd8(prev, _4bitdecode[b & 0x0F]);
				unsigned int s2 = SatAdd8(s1, _4bitdecode[b >> 4]);
				dst[0] = (unsigned char)s1;
				dst[1] = (unsigned char)s2;
				prev = (unsigned char)s2;
				dst += 2;
				remaining -= 2;
			}
		} else if (c == CODE_2BIT) {
			// sub+1 bytes of four 2-bit deltas, four samples per byte.
			for (unsigned int i = 0; i < n; i++) {
				unsigned int b = *src++;
				incount++;
				unsigned int s1 = SatAdd8(prev, _2bitdecode[b & 0x03]);
				unsigned int s2 = SatAdd8(s1, _2bitdecode[(b >> 2) & 0x03]);
				unsigned int s3 = SatAdd8(s2, _2bitdecode[(b >> 4) & 0x03]);
				unsigned int s4 = SatAdd8(s3, _2bitdecode[(b >> 6) & 0x03]);
				dst[0] = (unsigned char)s1;
				dst[1] = (unsigned char)s2;
				dst[2] = (unsigned char)s3;
				dst[3] = (unsigned char)s4;
				prev = (unsigned char)s4;
				dst += 4;
				remaining -= 4;
			}
		} else {
			// Zero-delta run: duplicate 'previous' sub+1 times.
			for (unsigned int i = 0; i < n; i++) {
				*dst++ = prev;
			}
			remaining -= n;
		}
	}

	return incount;
}

long __cdecl AudioUnzap(void *source, void *dest, long count)
{
	return Unzap_Audio(source, dest, count);
}

// Decompress a zapped audio frame; the retired auduncmp.asm entry point
// declared by soundint.h. Shares Unzap_Audio with AudioUnzap and returns the
// same source-byte count.
int __cdecl Decompress_Frame(void * source, void * dest, int size)
{
	return (int)Unzap_Audio(source, dest, size);
}

// The pre-fix LCW entry point, retained for interface completeness; nothing
// in the tree calls it. Its offset reads inherit the remaining-count
// register's high bits, reproduced by LCW_Decode's legacy_offsets path.
unsigned long __cdecl OLD_VQA_LCW_Uncompress(char const *source, char *dest, unsigned long length)
{
	return LCW_Decode((unsigned char const *)source, (unsigned char *)dest, length, false, true);
}

// Decompress an LCW encoded data block. A leading 0x00 code byte selects the
// relative-offset mode (run/copy offsets measured backward from the current
// dest position); otherwise offsets are measured from the start of dest.
// Returns the number of bytes written to dest.
unsigned long __cdecl VQA_LCW_Uncompress(char const *source, char *dest, unsigned long length)
{
	unsigned char const * src = (unsigned char const *)source;
	bool relative = (*src == 0);
	if (relative) {
		src++;    // consume the 0x00 mode marker
	}
	return LCW_Decode(src, (unsigned char*)dest, length, relative, false);
}

}

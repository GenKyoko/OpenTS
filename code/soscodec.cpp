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

// C port of the retired soscodec.asm HMI SOS ADPCM decoder. The assembly
// implements only 16-bit mono; every other bit-size/channel combination jumps
// straight to its exit with the return register left undefined, so callers in
// the tree reach this function only for 16-bit mono. The original packs two
// 4-bit codes per source byte (low nibble first) and keeps the stream index in
// wIndex as a table byte-offset (step * 32); the translation keeps that field
// encoding and the assembly's OR of each nibble into it.

#include "always.h"

#include "soscomp.h"

#include <cstddef>

// Index adjustment per 4-bit code (IMA ADPCM index table).
static short const wCODECIndexTab[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

// Quantization step per index (IMA ADPCM step table, 89 entries).
static short const wCODECStepTab[89] = {
	7,     8,     9,     10,    11,    12,    13,    14,
	16,    17,    19,    21,    23,    25,    28,    31,
	34,    37,    41,    45,    50,    55,    60,    66,
	73,    80,    88,    97,    107,   118,   130,   143,
	157,   173,   190,   209,   230,   253,   279,   307,
	337,   371,   408,   449,   494,   544,   598,   658,
	724,   796,   876,   963,   1060,  1166,  1282,  1411,
	1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
	3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
	7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

// The assembly addressed these offsets directly; pin them at compile time.
static_assert(offsetof(_SOS_COMPRESS_INFO, lpSource) == 0x00, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, lpDest) == 0x04, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, dwPredicted) == 0x14, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wIndex) == 0x22, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wIndex2) == 0x36, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wBitSize) == 0x38, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wChannels) == 0x3A, "_SOS_COMPRESS_INFO layout");

extern "C" {

// Initialize a compression/decompression stream. The assembly resets only the
// two channel index/predicted pairs; the step and sample-index fields are not
// maintained by this codec.
void __cdecl sosCODECInitStream(_SOS_COMPRESS_INFO * sSOSInfo)
{
	sSOSInfo->wIndex = 0;
	sSOSInfo->dwPredicted = 0;
	sSOSInfo->wIndex2 = 0;
	sSOSInfo->dwPredicted2 = 0;
}

// Decompress a 4:1 ADPCM stream. The assembly supports only 16-bit mono and
// leaves the return register undefined for any other combination; return the
// requested byte count, matching the 16-bit mono path.
unsigned long __cdecl sosCODECDecompressData(_SOS_COMPRESS_INFO * sSOSInfo, unsigned long wBytes)
{
	if (sSOSInfo->wBitSize != 16 || sSOSInfo->wChannels != 1) {
		return wBytes;
	}

	// The assembly keeps the stream index in wIndex as a byte offset into the
	// step tables (step * 32) and ORs each nibble into its low bits.
	unsigned short state = sSOSInfo->wIndex;
	int step = state >> 5;
	long pred = sSOSInfo->dwPredicted;
	char * src = sSOSInfo->lpSource;
	short * dst = (short *)sSOSInfo->lpDest;

	unsigned long samples = wBytes >> 1;   // two output bytes per 16-bit sample
	for (unsigned long i = 0; i < samples; i++) {
		// Two 4-bit codes per source byte, low nibble first.
		int code = (i & 1) != 0
			? ((unsigned char)src[i >> 1] >> 4)
			: ((unsigned char)src[i >> 1] & 0x0F);

		state = (unsigned short)(state | (code << 1));
		int token = (state >> 1) & 0x0F;

		short stepValue = wCODECStepTab[step];
		int diff = (stepValue >> 3)
			+ ((token & 4) != 0 ? stepValue : 0)
			+ ((token & 2) != 0 ? stepValue >> 1 : 0)
			+ ((token & 1) != 0 ? stepValue >> 2 : 0);
		if ((token & 8) != 0) {
			diff = -diff;
		}

		step += wCODECIndexTab[token];
		if (step < 0) {
			step = 0;
		} else if (step > 88) {
			step = 88;
		}

		pred += diff;
		if (pred > 32767) {
			pred = 32767;
		} else if (pred < -32768) {
			pred = -32768;
		}
		*dst++ = (short)pred;

		// The assembly reloads the next step from the index table, clearing the
		// nibble bits.
		state = (unsigned short)(step << 5);
	}

	sSOSInfo->wIndex = state;
	sSOSInfo->dwPredicted = pred;
	return wBytes;
}

} // extern "C"

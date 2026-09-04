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

// C port of the retired olsosdec.asm HMI SOS ADPCM decoder. The assembly packs
// two 4-bit codes per source byte (low nibble first, high nibble next via the
// code buffer) and runs separate mono / stereo-left / stereo-right loops; the
// translation keeps that structure and the byte-level pointer strides.

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
static_assert(offsetof(_SOS_COMPRESS_INFO, dwSampleIndex) == 0x10, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wStep) == 0x20, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wIndex) == 0x22, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wIndex2) == 0x36, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wBitSize) == 0x38, "_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_SOS_COMPRESS_INFO, wChannels) == 0x3A, "_SOS_COMPRESS_INFO layout");

// Decode one 4-bit code: the difference from the running step, then the next
// index and step for the channel state.
static int CODEC_DecodeSample(int code, short & step, short & index)
{
	int diff = (step >> 3)
		+ ((code & 4) != 0 ? step : 0)
		+ ((code & 2) != 0 ? step >> 1 : 0)
		+ ((code & 1) != 0 ? step >> 2 : 0);
	if ((code & 8) != 0) {
		diff = -diff;
	}

	int next = index + wCODECIndexTab[code];
	if (next < 0) {
		next = 0;
	} else if (next > 88) {
		next = 88;
	}
	index = (short)next;
	step = wCODECStepTab[next];
	return diff;
}

extern "C" {

// Initialize a compression/decompression stream.
void __cdecl General_sosCODECInitStream(_SOS_COMPRESS_INFO * sSOSInfo)
{
	sSOSInfo->wIndex = 0;
	sSOSInfo->wStep = 7;
	sSOSInfo->dwPredicted = 0;
	sSOSInfo->dwSampleIndex = 0;
	sSOSInfo->wIndex2 = 0;
	sSOSInfo->wStep2 = 7;
	sSOSInfo->dwPredicted2 = 0;
	sSOSInfo->dwSampleIndex2 = 0;
}

// Decompress a 4:1 ADPCM stream. Returns the number of bytes processed.
unsigned long __cdecl General_sosCODECDecompressData(_SOS_COMPRESS_INFO * sSOSInfo, unsigned long wBytes)
{
	unsigned long dwCODECBytesProcessed = wBytes;
	unsigned long dwCODECByteIndex;
	char * lpSrc;
	char * lpDst;

	sSOSInfo->dwSampleIndex = 0;
	sSOSInfo->dwSampleIndex2 = 0;

	dwCODECByteIndex = (sSOSInfo->wBitSize == 16) ? (wBytes >> 1) : wBytes;
	lpSrc = sSOSInfo->lpSource;
	lpDst = sSOSInfo->lpDest;

	if (sSOSInfo->wChannels == 2) {
		// Stereo left channel; the source/destination are interleaved.
		do {
			int code;
			if ((sSOSInfo->dwSampleIndex & 1) != 0) {
				code = (sSOSInfo->wCodeBuf >> 4) & 0x0F;
			} else {
				sSOSInfo->wCodeBuf = (short)(unsigned char)*lpSrc;
				code = sSOSInfo->wCodeBuf & 0x0F;
				lpSrc += 2;
			}
			sSOSInfo->wCode = (short)code;

			int pred = sSOSInfo->dwPredicted + CODEC_DecodeSample(code, sSOSInfo->wStep, sSOSInfo->wIndex);
			if (pred > 32767) {
				pred = 32767;
			} else if (pred < -32768) {
				pred = -32768;
			}
			sSOSInfo->dwPredicted = pred;

			if (sSOSInfo->wBitSize == 16) {
				*(short *)lpDst = (short)pred;
				lpDst += 4;
			} else {
				*lpDst = (char)((pred >> 8) ^ 0x80);
				lpDst += 2;
			}

			sSOSInfo->dwSampleIndex++;
		} while ((dwCODECByteIndex -= 2) != 0);

		// Stereo right channel into the interleaved slots.
		dwCODECBytesProcessed = wBytes;
		lpSrc = sSOSInfo->lpSource + 1;
		lpDst = sSOSInfo->lpDest + 1;
		if (sSOSInfo->wBitSize == 16) {
			dwCODECByteIndex = wBytes >> 1;
			lpDst++;
		} else {
			dwCODECByteIndex = wBytes;
		}

		do {
			int code;
			if ((sSOSInfo->dwSampleIndex2 & 1) != 0) {
				code = (sSOSInfo->wCodeBuf2 >> 4) & 0x0F;
			} else {
				sSOSInfo->wCodeBuf2 = (short)(unsigned char)*lpSrc;
				code = sSOSInfo->wCodeBuf2 & 0x0F;
				lpSrc += 2;
			}
			sSOSInfo->wCode2 = (short)code;

			int pred = sSOSInfo->dwPredicted2 + CODEC_DecodeSample(code, sSOSInfo->wStep2, sSOSInfo->wIndex2);
			if (pred > 32767) {
				pred = 32767;
			} else if (pred < -32768) {
				pred = -32768;
			}
			sSOSInfo->dwPredicted2 = pred;

			if (sSOSInfo->wBitSize == 16) {
				*(short *)lpDst = (short)pred;
				lpDst += 4;
			} else {
				*lpDst = (char)((pred >> 8) ^ 0x80);
				lpDst += 2;
			}

			sSOSInfo->dwSampleIndex2++;
		} while ((dwCODECByteIndex -= 2) != 0);
	} else {
		// Mono.
		do {
			int code;
			if ((sSOSInfo->dwSampleIndex & 1) != 0) {
				code = (sSOSInfo->wCodeBuf >> 4) & 0x0F;
			} else {
				sSOSInfo->wCodeBuf = (short)(unsigned char)*lpSrc;
				code = sSOSInfo->wCodeBuf & 0x0F;
				lpSrc++;
			}
			sSOSInfo->wCode = (short)code;

			int pred = sSOSInfo->dwPredicted + CODEC_DecodeSample(code, sSOSInfo->wStep, sSOSInfo->wIndex);
			if (pred > 32767) {
				pred = 32767;
			} else if (pred < -32768) {
				pred = -32768;
			}
			sSOSInfo->dwPredicted = pred;

			if (sSOSInfo->wBitSize == 16) {
				*(short *)lpDst = (short)pred;
				lpDst += 2;
			} else {
				*lpDst = (char)((pred >> 8) ^ 0x80);
				lpDst++;
			}

			sSOSInfo->dwSampleIndex++;
		} while (--dwCODECByteIndex != 0);
	}

	return dwCODECBytesProcessed;
}

} // extern "C"

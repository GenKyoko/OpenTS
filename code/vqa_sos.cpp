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

// C port of the retired vqa_sos.asm HMI SOS ADPCM decoder used by the VQA
// player. The assembly implements only 16-bit decode; 8-bit and every other
// bit-size/channel combination jump straight to the exit with the return
// register left undefined, so the translation returns the requested byte
// count for those, matching the supported 16-bit paths. Two 4-bit codes pack
// per source byte (low nibble first); the per-channel index lives in wIndex
// as a byte offset (step * 32), and the translation keeps that field encoding
// and the assembly's per-nibble index adjustment. The assembly's source
// alignment and tail-unroll machinery was a speed optimization; the plain
// sample loop below reproduces its net decode.

#include "always.h"

#include "vqalib/cmp.h"

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
static_assert(offsetof(_VQA_SOS_COMPRESS_INFO, dwPredicted) == 0x00, "_VQA_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_VQA_SOS_COMPRESS_INFO, wIndex) == 0x04, "_VQA_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_VQA_SOS_COMPRESS_INFO, dwPredicted2) == 0x06, "_VQA_SOS_COMPRESS_INFO layout");
static_assert(offsetof(_VQA_SOS_COMPRESS_INFO, wIndex2) == 0x0A, "_VQA_SOS_COMPRESS_INFO layout");

// Decode one channel: `samples` 16-bit outputs from `src` (two 4-bit codes
// per byte, low nibble first) into `dst` at `stride` sample spacing. `state`
// is the byte-offset form of the step index (step * 32) and `pred` the
// running predictor, both carried in the per-channel SOS fields between
// calls.
static void VQA_DecodeChannel(unsigned char const * src, short * dst, int stride,
		unsigned long samples, short & state, long & pred)
{
	int step = ((unsigned short)state) >> 5;
	for (unsigned long i = 0; i < samples; i++) {
		unsigned char byte = src[i >> 1];
		int code = (i & 1) != 0 ? (byte >> 4) : (byte & 0x0F);

		short stepValue = wCODECStepTab[step];
		int diff = (stepValue >> 3)
			+ ((code & 4) != 0 ? stepValue : 0)
			+ ((code & 2) != 0 ? stepValue >> 1 : 0)
			+ ((code & 1) != 0 ? stepValue >> 2 : 0);
		if ((code & 8) != 0) {
			diff = -diff;
		}

		step += wCODECIndexTab[code];
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
		*dst = (short)pred;
		dst += stride;
	}
	state = (short)(step << 5);
}

extern "C" {

// Initialize a compression/decompression stream. The assembly resets only the
// two channel index/predicted pairs.
void __cdecl VQA_sosCODECInitStream(_VQA_SOS_COMPRESS_INFO * sSOSInfo)
{
	sSOSInfo->wIndex = 0;
	sSOSInfo->dwPredicted = 0;
	sSOSInfo->wIndex2 = 0;
	sSOSInfo->dwPredicted2 = 0;
}

// Decompress a 4:1 ADPCM stream. Returns the number of bytes decompressed.
// Stereo output interleaves the two channels; each channel decodes the same
// number of samples from its own half of the compressed source (wBytes / 8
// source bytes per channel).
unsigned long __cdecl VQA_sosCODECDecompressData(void * src, void * dst,
		unsigned short wBitSize, unsigned short wChannels, unsigned long wBytes,
		_VQA_SOS_COMPRESS_INFO * sSOSInfo)
{
	if (wBitSize != 16 || (wChannels != 1 && wChannels != 2)) {
		return wBytes;
	}

	if (wChannels == 1) {
		VQA_DecodeChannel((unsigned char const *)src, (short *)dst, 1, wBytes >> 1,
			sSOSInfo->wIndex, sSOSInfo->dwPredicted);
	} else {
		VQA_DecodeChannel((unsigned char const *)src, (short *)dst, 2, wBytes >> 2,
			sSOSInfo->wIndex, sSOSInfo->dwPredicted);
		VQA_DecodeChannel((unsigned char const *)src + (wBytes >> 3), (short *)dst + 1, 2,
			wBytes >> 2, sSOSInfo->wIndex2, sSOSInfo->dwPredicted2);
	}
	return wBytes;
}

} // extern "C"

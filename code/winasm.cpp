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

// C port of the retired winasm.asm. The assembly supplied the low level
// palette tint/brighten helpers, the voxel span drawers behind the
// VoxelDrawFunctions dispatch table, and the 320x200 -> 640x400 palette
// interpolation scalers. The translation reproduces the assembly arithmetic
// byte for byte, including its quirks; each one is called out at the spot it
// lives so the reader can tell preserved behavior from a reconstruction
// artifact.

#include "always.h"

#include "interpal.h"
#include "voxdrsys.h"
#include "voxlib.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Interpolation scaler scratch buffers. winasm.asm kept these as 640 dwords
// each; the scalers only ever touch the first 640 bytes (2x the 320 byte
// source row) but the buffers keep the original capacity.
// ---------------------------------------------------------------------------
unsigned char TopLine[640 * 4];
unsigned char BottomLine[640 * 4];
unsigned char LineBuffer[640 * 4];

} // namespace


// ===========================================================================
// ADJUST_COLOR -- tint a palette into a hicolor translator
// ===========================================================================
//
// Called with the 256x3 byte art palette, a 256 entry 16-bit translator,
// per-channel 0.16 fixed point scale factors and a 256 byte tint mask. Entry
// 0 is always written as 0 (transparent). Every other entry is scaled by the
// matching factor -- or by `intensity` when the mask byte is zero -- clamped
// to 255 and repacked into the target hicolor layout.
//
// winasm.asm dispatched between three implementations at run time. The CMOV
// and the plain scalar path compute identical results (the CMOV "clamp" is
// `value | 0xFF0000` when the product overflows 24 bits, which is exactly the
// `min(x >> 16, 255)` of the scalar path), so the port folds the CMOV path
// into the scalar one. The MMX path is dropped: it shifted both factor and
// color right 4 before a signed word multiply, so its channel result is
// `c * f >> 24` and the translator comes out essentially black -- a
// reconstruction artifact, not a working fast path. winasm.asm keeps the
// original three-way dispatch as the record.
//
// The mask is read as raw bytes: callers pass either `char *` or `bool *`.

namespace {

struct AdjustColorMode {
	unsigned R_MASK, G_MASK, B_MASK;            // channel masks
	unsigned R_SHIFT, G_SHIFT, B_SHIFT;         // scalar packing shifts
};

constexpr AdjustColorMode AdjustMode_565 = { 0xF8, 0xFC, 0xF8, 8, 3, 3 };
constexpr AdjustColorMode AdjustMode_555 = { 0xF8, 0xF8, 0xF8, 7, 2, 3 };
constexpr AdjustColorMode AdjustMode_556 = { 0xF8, 0xF8, 0xFC, 8, 3, 2 };
constexpr AdjustColorMode AdjustMode_655 = { 0xFC, 0xF8, 0xF8, 8, 2, 3 };


// Scalar path (regular + CMOV). `dstpal[0]` is zeroed up front and entries
// 1..255 are scaled; the mask pointer starts at byte 1.
void Adjust_Color_Scalar(AdjustColorMode const & m, void * srcpal, void * dstpal,
		int red, int green, int blue, int intensity, unsigned char const * mask)
{
	unsigned char const * src = (unsigned char const *)srcpal + 3;
	unsigned short * dst = (unsigned short *)dstpal;

	dst[0] = 0;
	for (int i = 1; i < 256; i++) {
		unsigned r = src[0];
		unsigned g = src[1];
		unsigned b = src[2];
		src += 3;

		bool use_intensity = (mask[i] == 0);
		unsigned fr = use_intensity ? (unsigned)intensity : (unsigned)red;
		unsigned fg = use_intensity ? (unsigned)intensity : (unsigned)green;
		unsigned fb = use_intensity ? (unsigned)intensity : (unsigned)blue;

		r = std::min((r * fr) >> 16, 255u);
		g = std::min((g * fg) >> 16, 255u);
		b = std::min((b * fb) >> 16, 255u);

		dst[i] = (unsigned short)(((r & m.R_MASK) << m.R_SHIFT)
				| ((g & m.G_MASK) << m.G_SHIFT)
				| ((b & m.B_MASK) >> m.B_SHIFT));
	}
}


void Adjust_Color(AdjustColorMode const & m, void * srcpal, void * dstpal,
		int red, int green, int blue, int intensity, unsigned char const * mask)
{
	Adjust_Color_Scalar(m, srcpal, dstpal, red, green, blue, intensity, mask);
}

} // namespace


extern "C" {

void __cdecl Adjust_Color_565(void * srcpal, void * dstpal, int red, int green, int blue, int intensity, unsigned char * mask)
{
	Adjust_Color(AdjustMode_565, srcpal, dstpal, red, green, blue, intensity, mask);
}

void __cdecl Adjust_Color_555(void * srcpal, void * dstpal, int red, int green, int blue, int intensity, unsigned char * mask)
{
	Adjust_Color(AdjustMode_555, srcpal, dstpal, red, green, blue, intensity, mask);
}

void __cdecl Adjust_Color_556(void * srcpal, void * dstpal, int red, int green, int blue, int intensity, unsigned char * mask)
{
	Adjust_Color(AdjustMode_556, srcpal, dstpal, red, green, blue, intensity, mask);
}

void __cdecl Adjust_Color_655(void * srcpal, void * dstpal, int red, int green, int blue, int intensity, unsigned char * mask)
{
	Adjust_Color(AdjustMode_655, srcpal, dstpal, red, green, blue, intensity, mask);
}

} // extern "C"


// ===========================================================================
// BRIGHTEN_COLOR -- brighten a hicolor buffer through a 256x1 brightness ramp
// ===========================================================================
//
// Spot light drawing. `mul_buffer` is a 256 x 1 alpha/brightness ramp (a
// spotlight surface) and `color_buffer` is the 16-bit target. Every pixel with
// a non-zero brightness gets each channel scaled as
// `c + (c * brightness >> 8)`, clamped to 255 and repacked. The row strides
// are in bytes (color_buff_width is a surface byte stride, applied to the
// 16-bit color cursor). Zero-brightness pixels are left untouched.

namespace {

struct BrightenColorMode {
	unsigned C0, C1, C2, C3;       // channel extraction
	unsigned C4, C5;               // channel scale shifts
	unsigned C6, C7;               // final shift per channel
	unsigned C8, C9;               // final place per channel
	unsigned C10, C11, C12;        // blue extraction / scale / final
};

constexpr BrightenColorMode BrightenMode_565 = { 8, 3, 0xF8, 0xFC, 8, 8, 3, 2, 11, 5, 3, 8, 3 };
constexpr BrightenColorMode BrightenMode_655 = { 8, 2, 0xFC, 0xF8, 8, 8, 2, 3, 10, 5, 3, 8, 3 };
constexpr BrightenColorMode BrightenMode_556 = { 8, 3, 0xF8, 0xF8, 8, 8, 3, 3, 11, 6, 2, 8, 2 };
constexpr BrightenColorMode BrightenMode_555 = { 7, 2, 0xF8, 0xF8, 8, 8, 3, 3, 10, 5, 3, 8, 3 };

void Brighten_Color(BrightenColorMode const & m, unsigned char * mul_buffer,
		unsigned short * color_buffer, int mulbuff_width, int color_buff_width,
		int dst_width, int dst_height)
{
	for (int row = 0; row < dst_height; row++) {
		unsigned char * mul = mul_buffer + row * mulbuff_width;
		unsigned char * col = (unsigned char *)color_buffer + row * color_buff_width;

		for (int x = 0; x < dst_width; x++) {
			unsigned char bright = *mul++;
			if (bright != 0) {
				unsigned short color = *(unsigned short *)col;

				unsigned red = (color >> m.C0) & m.C2;
				unsigned green = (color >> m.C1) & m.C3;
				unsigned blue = (color << m.C10) & 0xFF;

				unsigned r = red + ((red * bright) >> m.C4);
				unsigned g = green + ((green * bright) >> m.C5);
				unsigned b = blue + ((blue * bright) >> m.C11);
				if (r > 255) r = 255;
				if (g > 255) g = 255;
				if (b > 255) b = 255;

				unsigned short out = (unsigned short)(((r >> m.C6) << m.C8)
						| ((g >> m.C7) << m.C9)
						| (b >> m.C12));
				*(unsigned short *)col = out;
			}
			col += 2;
		}
	}
}

} // namespace


extern "C" {

void __cdecl Brighten_Color_565(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height)
{
	Brighten_Color(BrightenMode_565, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height);
}

void __cdecl Brighten_Color_555(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height)
{
	Brighten_Color(BrightenMode_555, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height);
}

void __cdecl Brighten_Color_556(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height)
{
	Brighten_Color(BrightenMode_556, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height);
}

void __cdecl Brighten_Color_655(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height)
{
	Brighten_Color(BrightenMode_655, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height);
}

} // extern "C"


// ===========================================================================
// MMX_BRIGHTEN_COLOR -- MMX accelerated variant of the above
// ===========================================================================
//
// Same contract, plus `mmx_buffer`: a 65536 entry table holding each hicolor
// pixel split as (R << 16) | (G << 8) | B so the per-channel values come out
// of a single dword lookup. The per-channel math is `c + (c * brightness
// >> 8)` with byte saturation -- identical to the scalar path. The assembly
// packed the channels [B, G, R] from that dword; the port keeps that lane
// order internally and then packs to the target layout.
//
// One quirk is preserved: the rgb655 variant masks its green field with the
// constant 0x423A0A60 instead of a clean green mask (the effective mask is
// 0x60 after the shift, so green keeps only two bits). This matches the
// assembly; the other three modes mask their red field normally.

namespace {

struct MmxBrightenColorMode {
	unsigned C2, C3;         // post-saturation shifts
	unsigned C4, C5;         // place shifts
	unsigned C6;             // red mask (or the rgb655 green mask constant)
};

constexpr MmxBrightenColorMode MmxBrightenMode_565 = { 2, 1, 5, 10, 0xF8 };
constexpr MmxBrightenColorMode MmxBrightenMode_555 = { 3, 0, 5, 10, 0x7C };
constexpr MmxBrightenColorMode MmxBrightenMode_556 = { 2, 0, 6, 10, 0xF8 };
constexpr MmxBrightenColorMode MmxBrightenMode_655 = { 2, 1, 4, 10, 0x423A0A60u };

void MMX_Brighten_Color(MmxBrightenColorMode const & m, unsigned char * mul_buffer,
		unsigned short * color_buffer, int mulbuff_width, int color_buff_width,
		int dst_width, int dst_height, int const * mmx_buffer)
{
	for (int row = 0; row < dst_height; row++) {
		unsigned char * mul = mul_buffer + row * mulbuff_width;
		unsigned char * col = (unsigned char *)color_buffer + row * color_buff_width;

		for (int x = 0; x < dst_width; x++) {
			unsigned char bright = *mul++;
			if (bright != 0) {
				unsigned short color = *(unsigned short *)col;
				unsigned packed = (unsigned)mmx_buffer[color];

				// punpcklbw from the little-endian dword yields lanes
				// [B, G, R, 0]; the stale low bytes are shifted out by
				// psrlw so the first iteration needs no special casing.
				unsigned b = packed & 0xFF;
				unsigned g = (packed >> 8) & 0xFF;
				unsigned r = (packed >> 16) & 0xFF;

				// pmullw / psrlw 8 / paddusb / psrlw C2 per lane.
				unsigned b2 = std::min(b + ((b * bright) >> 8), 255u) >> m.C2;
				unsigned g2 = std::min(g + ((g * bright) >> 8), 255u) >> m.C2;
				unsigned r2 = std::min(r + ((r * bright) >> 8), 255u) >> m.C2;

				unsigned blue = b2;
				if (m.C3 > 0) {
					blue >>= m.C3;
				}
				unsigned green = g2 << m.C4;
				unsigned red = r2 << m.C5;

				unsigned out;
				if (m.C6 == 0x423A0A60u) {
					green &= m.C6;              // rgb655 quirk, see above
					out = blue | green | red;
				} else {
					red &= (m.C6 * 256);
					out = blue | green | red;
				}

				*(unsigned short *)col = (unsigned short)out;
			}
			col += 2;
		}
	}
}

} // namespace


extern "C" {

void __cdecl MMX_Brighten_Color_565(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height, int * mmx_buffer)
{
	MMX_Brighten_Color(MmxBrightenMode_565, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height, mmx_buffer);
}

void __cdecl MMX_Brighten_Color_555(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height, int * mmx_buffer)
{
	MMX_Brighten_Color(MmxBrightenMode_555, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height, mmx_buffer);
}

void __cdecl MMX_Brighten_Color_556(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height, int * mmx_buffer)
{
	MMX_Brighten_Color(MmxBrightenMode_556, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height, mmx_buffer);
}

void __cdecl MMX_Brighten_Color_655(unsigned char * mul_buffer, unsigned short * color_buffer, int mulbuff_width, int color_buff_width, int dst_width, int dst_height, int * mmx_buffer)
{
	MMX_Brighten_Color(MmxBrightenMode_655, mul_buffer, color_buffer, mulbuff_width, color_buff_width, dst_width, dst_height, mmx_buffer);
}

} // extern "C"


// ===========================================================================
// Voxel span drawers
// ===========================================================================
//
// These are the low level drawers behind VoxelDrawFunctions (voxlib.cpp). Each
// fills VoxelPixelDeltaTable with the screen offset of every Z step, then
// walks the layer's columns through StartOffset/EndOffset, parsing the
// per-column RLE stream and stamping colors into VoxelDrawBuffer.
//
// RLE layout per segment, forward and backward readable:
//   [delta][runlen][(color, normal) x runlen][runlen]
// The trailing runlen duplicates the leading one so the reverse drawers can
// walk the same bytes from the far end. A column may hold several segments.
//
// The eight assembly functions differ along four axes, all reproduced:
//   * direction: StartOffset + forward walk vs EndOffset + backward walk;
//   * span test: a column is drawn when its span word has the sign bit clear;
//   * width: the Normals/UNUSED variants stamp two adjacent pixels per voxel
//     (the second write is index + 1), the Regular variants stamp one;
//   * lighting: the Lit variants map the normal through
//     VoxelNormalTranslateTable and index VoxelPaletteTranslateTable.
//
// Delta table fill count quirk: the four Normals/Lighting variants fill
// ZSize entries, the four Regular/UNUSED variants fill (0xFF - ZSize)
// entries. With ZSize <= 127 that still covers every reachable delta; the
// extra entries past ZSize are stale extrapolations that well-formed data
// never indexes, and for ZSize > 127 the Regular variants read stale slots
// for large deltas. All of it matches the assembly.

namespace {

enum class VoxelDir { Forward, Reverse };
enum class VoxelMode { Single, Double };
enum class VoxelLit { Plain, Lit };
enum class DeltaCount { ZSize, FFMinusZSize };


inline uint32_t Delta_Step(unsigned char delta)
{
	return (uint32_t)(uint16_t)VoxelPixelDeltaTable[delta][0]
			| ((uint32_t)(uint16_t)VoxelPixelDeltaTable[delta][1] << 16);
}


template<VoxelMode M>
void Write_Voxel_Pixel(uint32_t pos, unsigned char color);


template<VoxelDir D, VoxelMode M, VoxelLit L, DeltaCount C>
void Draw_Voxel_Asm(VoxelFuncArgumentStruct * state)
{
	// ---- Precompute screen projection deltas for every Z step ----
	int fill_count = (C == DeltaCount::ZSize) ? state->ZSize : (0xFF - state->ZSize);
	{
		int16_t zx = state->TransformMatrix[3].I;
		int16_t zy = state->TransformMatrix[3].J;
		uint16_t px = 0;
		uint16_t py = 0;
		for (int i = 0; i < fill_count; i++) {
			VoxelPixelDeltaTable[i][0] = (short)px;
			VoxelPixelDeltaTable[i][1] = (short)py;
			px = (uint16_t)(px + zx);
			py = (uint16_t)(py + zy);
		}
	}

	// ---- Projection setup; positions are packed (J << 16) | I in 8.8 ----
	int const * span = (D == VoxelDir::Forward)
			? (int const *)state->StartOffset : (int const *)state->EndOffset;
	uint32_t pos = (uint32_t)(uint16_t)state->TransformMatrix[0].J << 16
			| (uint32_t)(uint16_t)state->TransformMatrix[0].I;
	uint32_t xstep = (uint32_t)(uint16_t)state->TransformMatrix[1].J << 16
			| (uint32_t)(uint16_t)state->TransformMatrix[1].I;
	uint32_t ystep = (uint32_t)(uint16_t)state->TransformMatrix[2].J << 16
			| (uint32_t)(uint16_t)state->TransformMatrix[2].I;
	uint32_t zstep = (uint32_t)(uint16_t)state->TransformMatrix[3].J << 16
			| (uint32_t)(uint16_t)state->TransformMatrix[3].I;

	// ---- Walk the layer rows (YSize) and columns (XSize) ----
	// The assembly held the two counters in the byte halves of ecx and
	// decremented them, so a zero size wraps to 255 and the loop still runs
	// 256 times. The byte counters reproduce that.
	uint8_t ycount = state->YSize;
	do {
		int base_index = state->StartIndex;
		uint32_t row_start = pos;
		uint8_t xcount = state->XSize;

		do {
			// The assembly writes the current position back into
			// TransformMatrix[0] before testing the span.
			state->TransformMatrix[0].I = (short)(pos & 0xFFFF);
			state->TransformMatrix[0].J = (short)(pos >> 16);

			int data_offset = span[state->StartIndex];
			if (data_offset >= 0) {             // or edi, [span]; jns
				unsigned char const * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;
				uint32_t p = pos;

				if constexpr (D == VoxelDir::Forward) {
					while (remaining) {
						unsigned char delta = *ptr++;
						remaining -= delta;
						p += Delta_Step(delta);

						unsigned char run = *ptr++;
						while (run) {
							if constexpr (L == VoxelLit::Lit) {
								unsigned char normal = ptr[1];
								unsigned char color = ptr[0];
								ptr += 2;
								unsigned char shade = VoxelNormalTranslateTable[normal];
								color = VoxelPaletteTranslateTable[shade][color];
								Write_Voxel_Pixel<M>(p, color);
							} else {
								unsigned char color = *ptr;
								ptr += 2;
								Write_Voxel_Pixel<M>(p, color);
							}
							p += zstep;
							remaining--;
							run--;
						}

						ptr++;                  // skip trailing runlen byte
					}
				} else {
					while (remaining) {
						unsigned char run = *ptr;
						ptr--;

						while (run) {
							if constexpr (L == VoxelLit::Lit) {
								unsigned char normal = *ptr;
								unsigned char color = ptr[-1];
								ptr -= 2;
								unsigned char shade = VoxelNormalTranslateTable[normal];
								color = VoxelPaletteTranslateTable[shade][color];
								Write_Voxel_Pixel<M>(p, color);
							} else {
								ptr--;
								unsigned char color = *ptr;
								ptr--;
								Write_Voxel_Pixel<M>(p, color);
							}
							p += zstep;
							remaining--;
							run--;
						}

						ptr--;                  // skip forward runlen byte
						unsigned char delta = *ptr;
						ptr--;
						remaining -= delta;
						p += Delta_Step(delta);
					}
				}
			}

			// Advance to the next voxel column.
			pos += xstep;
			state->StartIndex += state->StrideX;
		} while (--xcount);

		// Advance to the next voxel row.
		pos = row_start + ystep;
		state->StartIndex = base_index + state->StrideY;
	} while (--ycount);
}


template<VoxelMode M>
void Write_Voxel_Pixel(uint32_t pos, unsigned char color)
{
	// (pixel_x >> 8) | (pixel_y & 0xFF00), computed from the packed
	// position exactly as the assembly did.
	unsigned index = ((pos >> 16) & 0xFF00u) | ((pos >> 8) & 0xFFu);
	VoxelDrawBuffer[index] = color;
	if constexpr (M == VoxelMode::Double) {
		VoxelDrawBuffer[index + 1] = color;
	}
}

} // namespace


extern "C" {

void __cdecl Draw_Voxel_Regular_Normals_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Forward, VoxelMode::Double, VoxelLit::Plain, DeltaCount::ZSize>(state);
}

void __cdecl Draw_Voxel_Reverse_Normals_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Reverse, VoxelMode::Double, VoxelLit::Plain, DeltaCount::ZSize>(state);
}

void __cdecl Draw_Voxel_Regular_Lighting_Normals_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Forward, VoxelMode::Double, VoxelLit::Lit, DeltaCount::ZSize>(state);
}

void __cdecl Draw_Voxel_Reverse_Lighting_Normals_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Reverse, VoxelMode::Double, VoxelLit::Lit, DeltaCount::ZSize>(state);
}

void __cdecl Draw_Voxel_Regular_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Forward, VoxelMode::Single, VoxelLit::Plain, DeltaCount::FFMinusZSize>(state);
}

void __cdecl Draw_Voxel_Reverse_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Reverse, VoxelMode::Single, VoxelLit::Plain, DeltaCount::FFMinusZSize>(state);
}

// Dead in the original binary (never referenced by the dispatch table) but
// ported so the module stays complete.
void __cdecl Draw_Voxel_Regular_UNUSED_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Forward, VoxelMode::Double, VoxelLit::Plain, DeltaCount::FFMinusZSize>(state);
}

void __cdecl Draw_Voxel_Reverse_UNUSED_ASM(VoxelFuncArgumentStruct * state)
{
	Draw_Voxel_Asm<VoxelDir::Reverse, VoxelMode::Double, VoxelLit::Plain, DeltaCount::FFMinusZSize>(state);
}

} // extern "C"


// ===========================================================================
// 320x200 -> 640x400 palette interpolation scalers
// ===========================================================================
//
// Interpolate_2X_Scale (interpal.cpp) picks one of these three routines
// through CopyType; the shipped game only ever uses Asm_Interpolate (CopyType
// is 0), the other two are kept as the assembly had them.
//
// Each source pixel pair is turned into four destination pixels using
// PaletteInterpolationTable, the 65536 entry table built (or loaded) for the
// active palette. The assembly reads a 32-bit word spanning two pairs and
// produces a fixed 4 pixel pattern. Tracking the register juggling byte by
// byte, reading [b0 b1 b2 b3] yields destination bytes [b1, b3, b0,
// table[b3][b2]] -- i.e. it echoes b1 and b0, echoes b3, and interpolates
// b3/b2. The row tail writes [b0, table[b1][b0], b1, 0]. This odd echo
// pattern is the historical behavior and is preserved exactly.

namespace {

// The per-group 4 pixel pattern produced by the assembly's ror/shift dance.
inline void Interpolate_Group(unsigned char const * src, unsigned char * dst)
{
	unsigned char b0 = src[0];
	unsigned char b1 = src[1];
	unsigned char b2 = src[2];
	unsigned char b3 = src[3];
	dst[0] = b1;
	dst[1] = b3;
	dst[2] = b0;
	dst[3] = PaletteInterpolationTable[b3][b2];
}

// The two trailing source pixels and a blank pixel that close a row.
inline void Interpolate_Tail(unsigned char const * src, unsigned char * dst)
{
	unsigned char p0 = src[0];
	unsigned char p1 = src[1];
	dst[0] = p0;
	dst[1] = PaletteInterpolationTable[p1][p0];
	dst[2] = p1;
	dst[3] = 0;
}


// Interpolates a single 320 byte row into a 640 byte row.
void Interpolate_Single_Line(unsigned char const * source, unsigned char * dest, int source_width)
{
	int count = (source_width - 2) >> 1;
	for (int i = 0; i < count; i++) {
		Interpolate_Group(source, dest);
		source += 2;
		dest += 4;
	}
	Interpolate_Tail(source, dest);
}

// Blends two already-interpolated 640 byte rows into a third, per pixel:
// PaletteInterpolationTable[row2[i]][row1[i]].
void Interpolate_Between_Lines(unsigned char const * source1, unsigned char const * source2,
		unsigned char * destination, int source_width)
{
	int count = source_width * 2;
	for (int i = 0; i < count; i++) {
		destination[i] = PaletteInterpolationTable[source2[i]][source1[i]];
	}
}

} // namespace


extern "C" {

// Straight 2x horizontal interpolation of every source row.
void __cdecl Asm_Interpolate(unsigned char * src_ptr, unsigned char * dest_ptr,
		int source_height, int source_width, int dest_width)
{
	unsigned char * old_dest = dest_ptr;
	unsigned char * src = src_ptr;

	for (int line = 0; line < source_height; line++) {
		int count = (source_width - 2) >> 1;
		unsigned char * dst = old_dest;

		for (int i = 0; i < count; i++) {
			Interpolate_Group(src, dst);
			src += 2;
			dst += 4;
		}
		Interpolate_Tail(src, dst);
		src += 2;

		old_dest += dest_width;
	}
}


// Same horizontal interpolation, with every line written twice so the image
// doubles vertically as well. The first copy goes straight out, the second
// through the LineBuffer scratch.
void __cdecl Asm_Interpolate_Line_Double(unsigned char * src_ptr, unsigned char * dest_ptr,
		int source_height, int source_width, int dest_width)
{
	unsigned char * old_dest = dest_ptr;
	unsigned char * src = src_ptr;

	for (int line = 0; line < source_height; line++) {
		int count = (source_width - 2) >> 1;
		unsigned char * dst = old_dest;
		unsigned char * lb = LineBuffer;

		for (int i = 0; i < count; i++) {
			unsigned char group[4];
			Interpolate_Group(src, group);
			src += 2;
			std::memcpy(dst, group, 4);
			std::memcpy(lb, group, 4);
			dst += 4;
			lb += 4;
		}

		unsigned char tail[4];
		Interpolate_Tail(src, tail);
		src += 2;
		std::memcpy(dst, tail, 4);
		std::memcpy(lb, tail, 4);

		old_dest += dest_width / 2;
		std::memcpy(old_dest, LineBuffer, (size_t)source_width * 2);
		old_dest += dest_width / 2;
	}
}


// Doubles the frame by interpolating between adjacent source rows as well as
// across each row. Quirk preserved from the assembly: the loop consumes
// `source_lines + 1` source rows (one past the buffer) and writes
// `2 * source_lines - 1` destination rows (one short of a full 2x frame).
void __cdecl Asm_Interpolate_Line_Interpolate(unsigned char * src_ptr, unsigned char * dest_ptr,
		int source_lines, int source_width, int dest_width)
{
	unsigned char * old_dest = dest_ptr;
	unsigned char * next_line = TopLine;
	unsigned char * last_line = BottomLine;
	int pixel_count = source_width >> 1;        // dwords per interpolated row
	dest_width >>= 1;                           // byte stride of one dest row
	unsigned char * src = src_ptr;

	Interpolate_Single_Line(src, next_line, source_width);
	src += source_width;
	std::swap(next_line, last_line);
	source_lines--;

	while (source_lines-- != 0) {
		Interpolate_Single_Line(src, next_line, source_width);
		Interpolate_Between_Lines(last_line, next_line, LineBuffer, source_width);

		std::memcpy(old_dest, last_line, (size_t)pixel_count * 4);
		old_dest += dest_width;
		std::memcpy(old_dest, LineBuffer, (size_t)pixel_count * 4);
		old_dest += dest_width;

		src += source_width;
		std::swap(next_line, last_line);
	}

	Interpolate_Single_Line(src, next_line, source_width);
	std::memcpy(old_dest, next_line, (size_t)pixel_count * 4);
}

} // extern "C"


// ===========================================================================
// Asm_Create_Palette_Interpolation_Table -- build the interpolation table
// ===========================================================================
//
// Rebuilds PaletteInterpolationTable from InterpolationPalette: for every
// pair of palette entries it averages the RGB bytes and then searches the
// palette for the closest entry under squared Euclidean distance. It is the
// historical assembly version of Create_Palette_Interpolation_Table; the C++
// version in interpal.cpp is the one actually used, and this routine keeps
// two quirks the C++ version does not:
//   * the average is computed on bytes, so a component sum >= 256 wraps
//     before the halving (e.g. 255 + 255 -> 127 instead of 255);
//   * the per-channel difference wraps to a signed byte before squaring, so
//     differences of 128..255 compare as negative (128 - 1 = -129 -> 127).
// Ties in distance resolve to the later palette entry (the search accepts
// `distance <= closest`), and a perfect (zero distance) hit stops the search
// early.

extern "C" {

void __cdecl Asm_Create_Palette_Interpolation_Table(void)
{
	unsigned char const * pal = (unsigned char const *)InterpolationPalette;

	for (int i = 0; i < 256; i++) {
		unsigned char const * second = pal;

		for (int j = 0; j < 256; j++) {
			// Byte-average of the two entries (wrapping sum, see above).
			unsigned char avg_r = (unsigned char)(pal[0] + second[0]) >> 1;
			unsigned char avg_g = (unsigned char)(pal[1] + second[1]) >> 1;
			unsigned char avg_b = (unsigned char)(pal[2] + second[2]) >> 1;

			unsigned char const * cand = pal;
			int closest = 0;
			int closest_dist = -1;

			for (int p = 0; p < 256; p++) {
				int dr = (int8_t)(unsigned char)(cand[0] - avg_r);
				int dg = (int8_t)(unsigned char)(cand[1] - avg_g);
				int db = (int8_t)(unsigned char)(cand[2] - avg_b);
				int dist = dr * dr + dg * dg + db * db;

				if (dist <= closest_dist) {
					closest_dist = dist;
					closest = p;
					if (dist == 0) {
						break;              // got_perfect
					}
				}
				cand += 3;
			}

			PaletteInterpolationTable[i][j] = (unsigned char)closest;
			second += 3;
		}

		pal += 3;
	}
}

} // extern "C"

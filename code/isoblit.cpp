/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// C port of the retired isoasm.asm iso-tile rasterizers. The original assembly
// walked the state buffer through the packed layout below, which differs from
// the C++ IsoBlitState in isotype.cpp (that struct ends at 0x6A and never runs
// this path -- the Iso_Blit_Asm2 call site is gated behind IsoTileUseAsmDrawFunc,
// which is never enabled). The translation reproduces the assembly byte for
// byte, including its byte-level pointer arithmetic and two-LUT indexing.

#include "always.h"

#include <cstddef>

// Asm-side view of the blit state (isoasm.asm IsoBlitState STRUCT 1). Packed to
// match the raw byte offsets the assembly accessed; the C++ IsoBlitState fills
// the same buffer with a different field interpretation and is never used with
// this code.
#pragma pack(push, 1)
struct IsoBlitStateAsm {
	unsigned int InnerCount;			// +0x00 (low byte used as loop bound)
	unsigned int OuterCount;			// +0x04 (low byte used as loop bound)
	unsigned int ShapeBase;				// +0x08 (used as an integer table index base)
	unsigned int ShapeIndexBias;		// +0x0C
	unsigned int ZDepthBias;			// +0x10
	int ZBufferRowStride;				// +0x14 (byte stride, applied 2x per row)
	int RemapSourceRowStride;			// +0x18 (byte stride, applied 2x per row)
	int SurfaceRowStride;				// +0x1C (byte stride, applied 1x per row)
	int SourceByteStride;				// +0x20 (byte stride, applied 1x per row)
	unsigned int _unused_24;			// +0x24
	unsigned int _unused_28;			// +0x28
	unsigned int _unused_2C;			// +0x2C
	unsigned int _unused_30;			// +0x30
	unsigned int _unused_34;			// +0x34
	unsigned int _unused_38;			// +0x38
	unsigned int _unused_3C;			// +0x3C
	unsigned short *ZBufferCursor;		// +0x40
	unsigned short *RemapIndexCursor;	// +0x44
	unsigned short *SurfaceCursor;		// +0x48
	unsigned char *SourceByteCursor;	// +0x4C
	unsigned char *ZSourceByteCursor;	// +0x50
	unsigned char *TileShapeRowPtr;		// +0x54 (advances 48 bytes per row)
	unsigned short *PaletteToHicolor;	// +0x58
	int *ExtraStrideTable1;				// +0x5C
	int *ExtraStrideTable2;				// +0x60
	unsigned int _unused_64;			// +0x64
	unsigned short HalfbrightMask;		// +0x68
	unsigned short _pad_6A;				// +0x6A
	unsigned char *scratch6C;			// +0x6C
	unsigned int _unused_70;			// +0x70
	unsigned int scratch74;				// +0x74 (flag1 | flag2)
	unsigned int _unused_78;			// +0x78
	unsigned int _unused_7C;			// +0x7C
	unsigned int _unused_80;			// +0x80
	unsigned int _unused_84;			// +0x84
	unsigned int ExtraBlockFlag2;		// +0x88
	unsigned int ExtraBlockFlag1;		// +0x8C
	unsigned short *DepthShadingLUT;	// +0x90
};
#pragma pack(pop)

// The assembly walked these offsets directly; pin them at compile time.
static_assert(offsetof(IsoBlitStateAsm, OuterCount) == 0x04, "IsoBlitStateAsm layout");
static_assert(offsetof(IsoBlitStateAsm, ZBufferCursor) == 0x40, "IsoBlitStateAsm layout");
static_assert(offsetof(IsoBlitStateAsm, TileShapeRowPtr) == 0x54, "IsoBlitStateAsm layout");
static_assert(offsetof(IsoBlitStateAsm, DepthShadingLUT) == 0x90, "IsoBlitStateAsm layout");
static_assert(sizeof(IsoBlitStateAsm) == 0x94, "IsoBlitStateAsm layout");


extern "C" {

// Shape-aware diamond rasterizer (48x23 blit). Dead in the original binary;
// kept here so the port stays complete.
void __cdecl Iso_Blit_Asm1(IsoBlitStateAsm * s)
{
	unsigned int combined = s->ExtraBlockFlag1 | s->ExtraBlockFlag2;

	for (int row = 0; row < (int)(unsigned char)s->OuterCount; row++) {
		unsigned char * shape = s->TileShapeRowPtr;
		for (int col = 0; col < (int)(unsigned char)s->InnerCount; col++) {
			if (*shape++ != 0) {
				unsigned short z = (unsigned short)((unsigned char)*s->ZSourceByteCursor + s->ZDepthBias);
				if (z < *s->ZBufferCursor) {
					*s->ZBufferCursor = z;
					unsigned short shade = s->DepthShadingLUT[*s->RemapIndexCursor];
					unsigned char srcc = *s->SourceByteCursor;
					// The assembly loads the shaded word, then overwrites its low
					// byte with the source byte, so the palette index is the
					// shaded word's high byte combined with the source byte.
					*s->SurfaceCursor = s->PaletteToHicolor[((shade & 0xFF00) | srcc)];
				}
				s->ZSourceByteCursor++;
				s->SourceByteCursor++;
			}
			s->SurfaceCursor++;
			s->ZBufferCursor++;
			s->RemapIndexCursor++;
		}

		s->SurfaceCursor = (unsigned short *)((char *)s->SurfaceCursor + s->SurfaceRowStride);
		s->ZBufferCursor = (unsigned short *)((char *)s->ZBufferCursor + 2 * s->ZBufferRowStride);
		s->RemapIndexCursor = (unsigned short *)((char *)s->RemapIndexCursor + 2 * s->RemapSourceRowStride);
		s->TileShapeRowPtr += 48;

		if (combined != 0) {
			unsigned int base = s->ShapeBase + (unsigned int)(row + s->ShapeIndexBias) * 48;
			if (s->ExtraBlockFlag1 != 0) {
				int delta = s->ExtraStrideTable2[base + s->InnerCount];
				s->SourceByteCursor += delta;
				s->ZSourceByteCursor += delta;
			}
			if (s->ExtraBlockFlag2 != 0) {
				int delta = s->ExtraStrideTable1[base + 48];
				s->SourceByteCursor += delta;
				s->ZSourceByteCursor += delta;
			}
		}
	}
}


// Simpler rectangular rasterizer; the walked source is the shape stream itself.
void __cdecl Iso_Blit_Asm2(IsoBlitStateAsm * s)
{
	// The assembly keeps the running source in esi and never writes it back.
	unsigned char * src = s->SourceByteCursor;

	for (int row = 0; row < (int)(unsigned char)s->OuterCount; row++) {
		for (int col = 0; col < (int)(unsigned char)s->InnerCount; col++) {
			unsigned char srcc = *src++;
			if (srcc != 0) {
				unsigned short z = (unsigned short)((unsigned char)*s->ZSourceByteCursor + s->ZDepthBias);
				if (z < *s->ZBufferCursor) {
					*s->ZBufferCursor = z;
					unsigned short shade = s->DepthShadingLUT[*s->RemapIndexCursor];
					*s->SurfaceCursor = s->PaletteToHicolor[((shade & 0xFF00) | srcc)];
				}
			}
			s->ZSourceByteCursor++;
			s->SurfaceCursor++;
			s->ZBufferCursor++;
			s->RemapIndexCursor++;
		}

		s->SurfaceCursor = (unsigned short *)((char *)s->SurfaceCursor + s->SurfaceRowStride);
		s->ZBufferCursor = (unsigned short *)((char *)s->ZBufferCursor + 2 * s->ZBufferRowStride);
		s->RemapIndexCursor = (unsigned short *)((char *)s->RemapIndexCursor + 2 * s->RemapSourceRowStride);
		src += s->SourceByteStride;
		s->ZSourceByteCursor += s->SourceByteStride;
	}
}

} // extern "C"

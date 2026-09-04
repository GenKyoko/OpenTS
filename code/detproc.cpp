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

// C port of the retired detproc.asm (originally MMX.ASM). The feature flags
// below are consumed by winasm.asm, so their names and byte sizes are fixed.

#include "always.h"

#include "getcpu.h"
#include "misc.h"
#include "mpu.h"

#include <intrin.h>
#include <cstring>


// Returns whether the given EFLAGS bit can be toggled. The AC flag (bit 18)
// detects a 386 and the ID flag (bit 21) detects CPUID support. EFLAGS is
// restored to its original value on return.
static bool Eflags_Bit_Toggles(unsigned int bit)
{
	unsigned int eflags = __readeflags();
	__writeeflags(eflags ^ bit);
	bool toggles = ((__readeflags() ^ eflags) != 0);
	__writeeflags(eflags);
	return toggles;
}


extern "C" {

char UseCMOV = 0;
char HasCMOV = 0;
char UseMMX = 0;
char CPUType = 0;
char VendorID[20] = "Not available";


bool __cdecl Detect_MMX_Availability(void)
{
	unsigned char cpu_type = 3;

	if (Eflags_Bit_Toggles(0x40000u)) {
		cpu_type = 4;

		if (Eflags_Bit_Toggles(0x200000u)) {
			int regs[4];
			__cpuid(regs, 0);
			std::memcpy(VendorID, regs + 1, 4);		// EBX
			std::memcpy(VendorID + 4, regs + 3, 4);	// EDX
			std::memcpy(VendorID + 8, regs + 2, 4);	// ECX
			VendorID[12] = ' ';

			if ((unsigned int)regs[0] >= 1) {
				__cpuid(regs, 1);
				cpu_type = (unsigned char)(((unsigned int)regs[0] >> 8) & 0xF);
			}
		}
	}

	CPUType = (char)cpu_type;

	if (cpu_type < 5) {
		UseMMX = 0;
		return false;
	}

	int regs[4];
	__cpuid(regs, 1);
	if ((regs[3] & 0x00800000u) == 0) {		// MMX feature bit
		UseMMX = 0;
		return false;
	}

	UseMMX = 1;
	return true;
}


bool __cdecl Detect_CMOV_Availability(void)
{
	if ((unsigned char)CPUType < 5) {
		UseCMOV = 0;
		HasCMOV = 0;
		return false;
	}

	int regs[4];
	__cpuid(regs, 1);
	if ((regs[3] & 0x00008000u) == 0) {		// CMOV feature bit
		UseCMOV = 0;
		HasCMOV = 0;
		return false;
	}

	// Family-5 chips that report CMOV keep the feature known but unused,
	// matching the original detection.
	if ((unsigned char)CPUType <= 5) {
		UseCMOV = 0;
		HasCMOV = 1;
		return true;
	}

	UseCMOV = 1;
	HasCMOV = 1;
	return true;
}


unsigned int __cdecl Get_CPU_Clock(unsigned int & high)
{
	unsigned __int64 stamp = __rdtsc();
	high = (unsigned int)(stamp >> 32);
	return (unsigned int)stamp;
}


WORD __cdecl Processor(void)
{
	if (!Eflags_Bit_Toggles(0x40000u)) {
		return 0;	// 80386
	}

	if (!Eflags_Bit_Toggles(0x200000u)) {
		return 1;	// 80486
	}

	return 2;		// CPUID available, Pentium or later
}

} // extern "C"

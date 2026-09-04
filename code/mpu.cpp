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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/mpu.cpp                                $*
 *                                                                                             *
 *                      $Author:: Denzil_l                                                    $*
 *                                                                                             *
 *                     $Modtime:: 8/23/01 5:07p                                               $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Get_CPU_Rate -- Fetch the rate of CPU ticks per second.                                   *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "mpu.h"

#include "win.h"

#include <math.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

typedef union {
	LARGE_INTEGER LargeInt;
	struct QuadPart {
		unsigned int LowPart;
		unsigned int HighPart;
	} QuadPart;
} QuadValue;


/***********************************************************************************************
 * Get_CPU_Rate -- Fetch the rate of CPU ticks per second.                                     *
 *                                                                                             *
 *    This routine will query the CPU to determine how many clock per second it is.            *
 *                                                                                             *
 * INPUT:   high  -- Reference to the location that will be filled with the upper 32 bits      *
 *                   of the result.                                                            *
 *                                                                                             *
 * OUTPUT:  Returns with the lower 32 bits of the result.                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/20/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
unsigned int Get_CPU_Rate(unsigned int & high)
{
	union {
		LARGE_INTEGER LargeInt;
		struct {
			unsigned int LowPart;
			unsigned int HighPart;
		} QuadPart;
	} value;

	if (QueryPerformanceFrequency(&value.LargeInt)) {
		high = value.QuadPart.HighPart;
		return(value.QuadPart.LowPart);
	}
	high = 0;
	return(0);
}


#if 0
/***********************************************************************************************
 * Get_CPU_Clock -- Fetches the current CPU clock time.                                        *
 *                                                                                             *
 *    This routine will return the internal Pentium clock accumulator. This accumulator is     *
 *    incremented every clock tick. Since this clock value can get very very large, the value  *
 *    returned is in 64 bits. The low half is returned directly, the high half is stored in    *
 *    location specified.                                                                      *
 *                                                                                             *
 * INPUT:   high  -- Reference to the high value of the 64 bit clock number.                   *
 *                                                                                             *
 * OUTPUT:  Returns with the low half of the CPU clock value.                                  *
 *                                                                                             *
 * WARNINGS:   This instruction is only available on Pentium or later processors.              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/17/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
unsigned int Get_CPU_Clock(unsigned int & high)
{
	int h;
	int l;
	__asm {
		_emit 0Fh
		_emit 31h
		mov	[h],edx
		mov	[l],eax
	}
	high = h;
	return(l);
}
#endif


/*
 * Based on code released by Intel
 * http://db.zmitac.aei.polsl.pl/Electronics_Firm_Docs/PENTIUMIII/pentium/cpuinfo.zip
 */

/*
**
** Cut and paste job from an intel example.
**
**
**
**
**
**
*/

// Max # of samplings to allow before giving up and returning current average.
#define MAX_TRIES			20
#define ROUND_THRESHOLD		6

// # of MHz to allow samplings to deviate from average of samplings.
#define TOLERANCE			1

static unsigned long TSC_Low;
static unsigned long TSC_High;

/// <summary>
/// Samples the processor's time stamp counter.
/// This routine executes the RDTSC opcode and stashes the two halves of the 64 bit cycle
/// count in the module's time stamp globals, where the timing code can pick them up.
/// </summary>
/// <remarks>Only call this routine on a processor that supports the RDTSC opcode. On
/// architectures without a time stamp counter the globals are set to zero.</remarks>
void RDTSC(void)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
	unsigned __int64 stamp = __rdtsc();
	TSC_Low = (unsigned long)stamp;
	TSC_High = (unsigned long)(stamp >> 32);
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
	unsigned int lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	TSC_Low = lo;
	TSC_High = hi;
#else
	TSC_Low = 0;
	TSC_High = 0;
#endif
}


/// <summary>
/// Determines the speed of the processor in megahertz.
/// This routine races the processor's time stamp counter against the Win32 high resolution
/// performance counter. The measurement is repeated until successive readings agree, so
/// that other activity on the machine cannot skew the result.
/// </summary>
/// <returns>Returns with the processor speed in megahertz. Zero is returned if the machine
/// has no high resolution performance counter.</returns>
/// <remarks>Only call this routine on a processor that supports the RDTSC opcode.</remarks>
int Get_RDTSC_CPU_Speed(void)
{
	LARGE_INTEGER t0,t1;
	unsigned int	freq=0;						// Most current freq. calc.
	unsigned int	freq2=0;						// 2nd most current freq. calc.
	unsigned int	freq3=0;						// 3rd most current freq. calc.
	unsigned int	total;						// Sum of previous three freq. calc.
	int	tries=0;						// Number of times a calculation has been
												// made on this call
	unsigned int	total_cycles=0, cycles;	// Clock cycles elapsed during test
	unsigned int	stamp0, stamp1;			// Time Stamp for beginning and end of test
	unsigned int	total_ticks=0, ticks;	// Microseconds elapsed during test
// unsigned int	current = 0;				// Elapsed time during loop
	LARGE_INTEGER count_freq;			// Hi-Res Performance Counter frequency


	if ( !QueryPerformanceFrequency(&count_freq) ) return(0);


#if 0 ///Change in Ren
	HANDLE process = GetCurrentProcess();
	DWORD processPri = GetPriorityClass(process);
	SetPriorityClass(process, REALTIME_PRIORITY_CLASS);

	HANDLE thread = GetCurrentThread();
	int threadPri = GetThreadPriority(thread);
	SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
#endif

	/*
	** On processors supporting the TSC opcode, compare elapsed time on the
	** High-Resolution Counter with elapsed cycles on the Time Stamp Counter.
	*/

	do	{
		/*
		** This do loop runs up to 20 times or until the average of the previous
		** three calculated frequencies is within 1 MHz of each of the individual
		** calculated frequencies.   This resampling increases the accuracy of the
		** results since outside factors could affect this calculation.
		*/

		tries++;								// Increment number of times sampled
												//   on this call to cpuspeed

		freq3 = freq2;						// Shift frequencies back to make
		freq2 = freq;						//   room for new frequency measurement

		/*
		** Get high-resolution performance counter time
		*/
		QueryPerformanceCounter(&t0);

		t1.LowPart = t0.LowPart;		// Set Initial time
		t1.HighPart = t0.HighPart;

		/*
		** Loop until 50 ticks have passed since last read of hi-res counter.
		** This accounts for overhead later.
		*/
		while ( (unsigned int)t1.LowPart - (unsigned int)t0.LowPart<50) {
			QueryPerformanceCounter(&t1);
		}

		RDTSC();
		stamp0 = TSC_Low;

		t0.LowPart = t1.LowPart;		// Reset Initial Time
		t0.HighPart = t1.HighPart;

		/*
		** Loop until 1000 ticks have passed since last read of hi-res counter.
		** This allows for elapsed time for sampling.
		*/
		while ( (unsigned int)t1.LowPart - (unsigned int)t0.LowPart < 1000 ) {
			QueryPerformanceCounter(&t1);
		}

		RDTSC();
		stamp1 = TSC_Low;


		cycles = stamp1 - stamp0;					// # of cycles passed between reads

		ticks = ((unsigned int)t1.LowPart - (unsigned int)t0.LowPart);
		ticks = ticks * 100000;						// Convert ticks to hundred
															//   thousandths of a tick
		ticks = (unsigned int)(ticks / (count_freq.LowPart / 10));
															// Hundred Thousandths of a
															//   Ticks / ( 10 ticks/second )
															//   = microseconds (us)
		total_ticks += ticks;
		total_cycles += cycles;
		if ( (ticks % count_freq.LowPart) > (count_freq.LowPart/2) ) ticks++;		// Round up if necessary

		freq = cycles/ticks;							// MHz = cycles / us

		if ( cycles%ticks > ticks/2 ) freq++;	// Round up if necessary

		total = ( freq + freq2 + freq3 );		// Total last three frequency calcs

	} while ( (tries < 3 ) || (tries < 20) && ((abs((int)(3 * freq -total)) > 3*TOLERANCE )|| (abs((int)(3 * freq2-total)) > 3*TOLERANCE )|| (abs((int)(3 * freq3-total)) > 3*TOLERANCE )));

#if 0 ///Change in Ren
	SetThreadPriority(thread, threadPri);
	SetPriorityClass(process, processPri);
#endif

	/*
	** Try one more significant digit.
	*/
	freq3 = ( total_cycles * 10 ) / total_ticks;
	freq2 = ( total_cycles * 100 ) / total_ticks;

	if ( freq2 - (freq3 * 10) >= ROUND_THRESHOLD ) freq3++;

	int norm_freq = total_cycles / total_ticks;

	freq = norm_freq * 10;
	if ( (freq3 - freq) >= ROUND_THRESHOLD ) norm_freq++;

	return (norm_freq);

}



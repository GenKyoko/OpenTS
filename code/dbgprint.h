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

#pragma once

#include "sun.h"

#include <sal.h>

void Debug_Init(void);
void Debug_Init_Console(void);
void Debug_Console_Hold(void);
char const * Debug_Log_File_Name(void);
bool Delete_Files_Older_Than(char const * directory, char const * pattern, unsigned days);

void __cdecl DebugString(_Printf_format_string_ char const * string, ...);
void __cdecl DebugStringNoPrefix(_Printf_format_string_ char const * string, ...);

/*
 * Emits one finished message and records the current errno as the thread's
 * last error. DebugFmt calls this so that its template body needs nothing
 * from the Windows headers.
 */
void Debug_Emit_Line(char const * buffer, bool with_prefix);

#include <format>
/// <summary>
/// Reports a formatted message to the debug log, the debugger, and the debug console. A
/// message that starts a line is stamped with the time it was reported.
/// </summary>
/// <param name="string">The printf style format string to report.</param>
template<typename... Args>
inline void DebugFmt(const std::format_string<Args...> fmt, Args&&... args)
{
	std::string format = std::vformat(fmt.get(), std::make_format_args(args...));

	Debug_Emit_Line(format.c_str(), true);
}

char const * Last_Error_Text(unsigned long error);

/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
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
 * UI.INI TrueType font registry.
 *
 * Reads the [TTFFonts] section of UI.INI, where every entry maps a registry name to one
 * TrueType file relative to the game directory, and turns the [UI] DefaultFont list into
 * the fallback chain the wide (Unicode) text path draws with. The fonts themselves are
 * loaded and rasterized by FreeType inside the wide text path.
 ***********************************************************************************************/

#include "always.h"

#include "uifonts.h"

#include "ccfile.h"
#include "ccini.h"
#include "dbgprint.h"
#include "_surface.h"
#include "rect.h"
#include "surface.h"
#include "widetext.h"

#include <map>
#include <string>
#include <vector>

namespace {

	char const * const FONT_SECTION = "TTFFonts";
	char const * const UI_SECTION = "UI";

	/*
	** Registry name (as written in UI.INI) to the TTF file it points at, relative to
	** the game directory.
	*/
	std::map<std::string, std::string> Registry;

}

/***********************************************************************************************
 * Init_UI_Fonts -- Sets up the wide text path's font chain from UI.INI.                       *
 *                                                                                             *
 *    Loads UI.INI from the game directory when it exists, records the [TTFFonts] registry     *
 *    and resolves the [UI] DefaultFont list into the chain of TTF files the wide text path    *
 *    loads. An empty or missing DefaultFont leaves the chain to the wide text path's own      *
 *    system font defaults.                                                    *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/04/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void Init_UI_Fonts(void)
{
	CCFileClass file("UI.INI");
	if (!file.Is_Available()) {
		return;
	}

	CCINIClass ini;
	ini.Load(file, false);
	if (!ini.Is_Present(FONT_SECTION) && !ini.Is_Present(UI_SECTION)) {
		return;
	}

	/*
	** Record the registry: every entry maps a name to a TTF file relative to the game
	** directory. Files are loaded later, when the chain is resolved.
	*/
	int count = ini.Entry_Count(FONT_SECTION);
	for (int index = 0; index < count; ++index) {
		char const * name = ini.Get_Entry(FONT_SECTION, index);
		if (name == NULL || *name == '\0') {
			continue;
		}

		char path[512];
		ini.Get_String(FONT_SECTION, name, "", path, sizeof(path));
		if (path[0] == '\0') {
			continue;
		}

		Registry[name] = path;
		DebugString("UI.INI: registered font '%s' from '%s'\n", name, path);
	}

	/*
	** Resolve the [UI] DefaultFont list. The value is a comma separated list of
	** registry names ordered by priority -- the first face that carries a glyph for a
	** character draws it -- and every named entry contributes its TTF file to the
	** chain.
	*/
	char defaultfont[256];
	ini.Get_String(UI_SECTION, "DefaultFont", "", defaultfont, sizeof(defaultfont));
	if (defaultfont[0] == '\0') {
		return;
	}

	std::vector<std::string> chain;
	char * context = NULL;
	for (char * token = strtok_s(defaultfont, ",", &context); token != NULL; token = strtok_s(NULL, ",", &context)) {
		while (*token == ' ' || *token == '\t') {
			token++;
		}
		char * end = token + strlen(token);
		while (end > token && (end[-1] == ' ' || end[-1] == '\t')) {
			*--end = '\0';
		}
		if (*token == '\0') {
			continue;
		}

		auto found = Registry.find(token);
		if (found == Registry.end()) {
			DebugString("UI.INI: default font '%s' is not in the registry, skipping\n", token);
			continue;
		}
		chain.push_back(found->second);
		DebugString("UI.INI: chain entry '%s' -> '%s'\n", token, found->second.c_str());
	}

	Wide_Text_Set_Font_Chain(chain);
}


/***********************************************************************************************
 * Paint_Wide_Text_Sample -- Draws the wide text test sample in the lower left corner.         *
 *                                                                                             *
 *    Prints a short Chinese and English mixed two line sample through the wide text path,     *
 *    exercising the configured font chain, HarfBuzz shaping and the FreeType rasterizer at    *
 *    once. Drawn every frame right after the tactical map has rendered.                       *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/04/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
void Paint_Wide_Text_Sample(void)
{
	static bool logged = false;
	if (LogicalSurface == NULL) {
		return;
	}

	Rect rect = LogicalSurface->Get_Rect();
	if (rect.Height < 60 || rect.Width < 200) {
		return;
	}

	TextPrintType style = TextPrintType(TPF_8POINT | TPF_DROPSHADOW | TPF_BRIGHT_COLOR);
	Point2D point(8, rect.Height - 66);
	wchar_t const * line1 = L"OpenTS 宽字符文本测试 / Wide Text Sample";
	wchar_t const * line2 = L"中文与 English 混排 —— 链式字体 0123";

	Wide_Text_Print(line1, *LogicalSurface, rect, point, NULL, TBLACK, style, -1);
	Wide_Text_Print(line2, *LogicalSurface, rect, point + Point2D(0, 24), NULL, TBLACK, style, -1);
	logged = true;
}

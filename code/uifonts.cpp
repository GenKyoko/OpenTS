/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
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
#include "font.h"
#include "localization.h"
#include "ttffont.h"
#include "_surface.h"
#include "rect.h"
#include "surface.h"
#include "widetext.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

	char const * const FONT_SECTION = "TTFFonts";
	char const * const UI_SECTION = "UI";
	char const * const REPLACE_SECTION = "FontReplacements";

	/*
	** Registry name (as written in UI.INI) to the TTF file it points at, relative to
	** the game directory.
	*/
	std::map<std::string, std::string> Registry;

	/*
	** Legacy bitmap font name (6PT_GRAD and friends) to the registry name of the TTF
	** that replaces it, plus the built replacement instances.
	*/
	std::map<std::string, std::string> Replacements;
	std::map<std::string, FontClass *> ReplacementFonts;
	bool ReplacementsLoaded = false;

	/*
	** Reads the [FontReplacements] section of UI.INI once: every entry maps a legacy
	** font name to the registry name of the TTF that replaces it.
	*/
	void Load_Font_Replacements(void)
	{
		if (ReplacementsLoaded) {
			return;
		}
		ReplacementsLoaded = true;

		CCFileClass file("UI.INI");
		if (!file.Is_Available()) {
			return;
		}

		CCINIClass ini;
		ini.Load(file, false);
		if (!ini.Is_Present(REPLACE_SECTION)) {
			return;
		}

		int count = ini.Entry_Count(REPLACE_SECTION);
		for (int index = 0; index < count; ++index) {
			char const * name = ini.Get_Entry(REPLACE_SECTION, index);
			if (name == NULL || *name == '\0') {
				continue;
			}

			char value[256];
			ini.Get_String(REPLACE_SECTION, name, "", value, sizeof(value));
			if (value[0] == '\0') {
				continue;
			}

			Replacements[name] = value;
			DebugString("UI.INI: font replacement '%s' -> '%s'\n", name, value);
		}
	}

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
 * Fetch_TTF_Font_Replacement -- Returns the TTF stand-in for a legacy bitmap font.             *
 *                                                                                             *
 *    Looks the legacy font name up in UI.INI's [FontReplacements] section, resolves the       *
 *    value through the [TTFFonts] registry and builds a TtfFontClass over the face at the     *
 *    requested pixel height. The instance is cached, so every caller of a given legacy        *
 *    font shares one replacement. A null return means no replacement is configured and the    *
 *    caller keeps using the embedded font.                                    *
 *                                                                                             *
 * INPUT:   legacy_name  -- The embedded font's name, as used by UI.INI (6PT_GRAD etc.).       *
 *          pixel_height -- Rasterization height, normally the replaced font's height.         *
 *                                                                                             *
 * OUTPUT:  Returns the replacement font, or null when none is configured.                     *
 *                                                                                             *
 * WARNINGS:   The returned instance stays valid for the rest of the process.                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/05/2026 OpenTS : Created.                                                              *
 *=============================================================================================*/
FontClass * Fetch_TTF_Font_Replacement(char const * legacy_name, int pixel_height)
{
	if (legacy_name == NULL || *legacy_name == '\0') {
		return(NULL);
	}

	Load_Font_Replacements();

	auto entry = Replacements.find(legacy_name);
	if (entry == Replacements.end()) {
		return(NULL);
	}

	auto cached = ReplacementFonts.find(legacy_name);
	if (cached != ReplacementFonts.end()) {
		return(cached->second);
	}

	/*
	** The value is a comma separated chain of registry names, tried in order until
	** one carries the glyph -- the same convention as the [UI] DefaultFont chain.
	** A purely numeric segment sets the pixel height instead of naming a font;
	** without it the replaced font's height is used.
	*/
	std::vector<std::string> chainnames;
	int height = pixel_height;
	{
		char * context = NULL;
		char * mutablevalue = new char[entry->second.size() + 1];
		strcpy(mutablevalue, entry->second.c_str());
		for (char * token = strtok_s(mutablevalue, ",", &context); token != NULL; token = strtok_s(NULL, ",", &context)) {
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
			bool numeric = true;
			for (char * p = token; *p != '\0'; ++p) {
				if (*p < '0' || *p > '9') {
					numeric = false;
					break;
				}
			}
			if (numeric) {
				int configured = atoi(token);
				if (configured > 0) {
					height = configured;
				}
			} else {
				chainnames.push_back(token);
			}
		}
		delete[] mutablevalue;
	}

	std::vector<void *> faces;
	for (std::string const & name : chainnames) {
		auto registered = Registry.find(name);
		if (registered == Registry.end()) {
			DebugString("UI.INI: replacement '%s' names unregistered font '%s'\n",
				legacy_name, name.c_str());
			continue;
		}
		void * face = Wide_Text_Acquire_Face(registered->second.c_str());
		if (face == NULL) {
			continue;
		}
		faces.push_back(face);
	}
	if (faces.empty()) {
		return(NULL);
	}

	TtfFontClass * font = new TtfFontClass(faces, height);
	ReplacementFonts[legacy_name] = font;
	std::string chainlabel;
	for (std::string const & name : chainnames) {
		if (!chainlabel.empty()) {
			chainlabel += ",";
		}
		chainlabel += name;
	}
	DebugString("UI.INI: legacy font '%s' replaced by chain '%s' at %d px\n",
		legacy_name, chainlabel.c_str(), height);
	return(font);
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

	// TextPrintType style = TextPrintType(TPF_8POINT | TPF_DROPSHADOW | TPF_BRIGHT_COLOR);
	// Point2D point(8, rect.Height - 90);
	// wchar_t const * line1 = L"OpenTS 宽字符文本测试 / Wide Text Sample";
	// wchar_t const * line2 = L"中文与 English 混排 —— 链式字体 0123";
	// // sample.key resolves through the localization chain (localization\ui.json).

	// Wide_Text_Print(line1, *LogicalSurface, rect, point, NULL, TBLACK, style, -1);
	// Wide_Text_Print(line2, *LogicalSurface, rect, point + Point2D(0, 24), NULL, TBLACK, style, -1);

	// /*
	// ** Third and fourth lines: the localization system's answers for a flat key and
	// ** a nested key, demonstrating the JSON chain end to end. Localize_Wide hands
	// ** over UTF-16 text ready for the wide text path (the JSON escapes decode to
	// ** real characters).
	// */
	// wchar_t const * line3 = Localize_Wide("sample.key");
	// Wide_Text_Print(line3, *LogicalSurface, rect, point + Point2D(0, 48), NULL, TBLACK, style, -1);
	// wchar_t const * line4 = Localize_Wide("sample.nested");
	// Wide_Text_Print(line4, *LogicalSurface, rect, point + Point2D(0, 72), NULL, TBLACK, style, -1);

	logged = true;
}

/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

#include "always.h"

#include "localization.h"

#include "ccfile.h"
#include "ccini.h"
#include "dbgprint.h"
#include "jsonio.h"
#include "txtids.h"

#include <json/json.h>

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

	char const * const LOC_SECTION = "Localization";
	char const * const UI_INI_NAME = "UI.INI";

	std::vector<Json::Value> FixedFiles;   // Documents loaded once from UI.INI, in order.
	std::vector<Json::Value> MapFiles;     // Documents of the current scenario, in order.

	std::string ReturnBuffer;              // Backing store of the last Localize answer.
	std::string MissingBuffer;             // Backing store of the last MISSING answer.
	std::wstring WideBuffer;               // Wide (UTF-16) backing store of the last answer.
	std::string Language;                  // Selected region code; empty = default entries only.

	/*
	** Splits a comma separated file list and loads every existing entry into the
	** given chain. Later entries win when the same key repeats.
	*/
	void Load_Chain(char * list, std::vector<Json::Value> & files)
	{
		char * context = NULL;
		for (char * token = strtok_s(list, ",", &context); token != NULL; token = strtok_s(NULL, ",", &context)) {
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
			Json::Value root;
			if (Read_Json_File(token, root)) {
				if (root.isObject()) {
					files.push_back(root);
				} else {
					DebugString("Localization: '%s' root is not an object, skipped\n", token);
				}
			}
		}
	}

	/*
	** Resolves a key inside one document. A whole-key match wins (flat style);
	** otherwise a dotted key drills down through nested objects ("a.b.c"). Only
	** string entries count as provided, so a mistyped value keeps falling through
	** to later documents and eventually to the MISSING answer.
	*/
	Json::Value const * Resolve_In(Json::Value const & root, char const * key)
	{
		if (!root.isObject()) {
			return(NULL);
		}
		if (root.isMember(key)) {
			Json::Value const & value = root[key];
			return(value.isString() ? &value : NULL);
		}
		if (strchr(key, '.') == NULL) {
			return(NULL);
		}
		char path[256];
		strncpy_s(path, sizeof(path), key, _TRUNCATE);
		Json::Value const * node = &root;
		char * context = NULL;
		for (char * token = strtok_s(path, ".", &context); token != NULL; token = strtok_s(NULL, ".", &context)) {
			if (!node->isObject() || !node->isMember(token)) {
				return(NULL);
			}
			node = &(*node)[token];
		}
		return(node->isString() ? node : NULL);
	}

	/*
	** Searches the chain from the last document backwards. Returns the entry of the
	** first document that provides the key as a string.
	*/
	bool Find_Key(std::vector<Json::Value> const & files, char const * key, Json::Value const *& found)
	{
		for (int index = (int)files.size() - 1; index >= 0; --index) {
			Json::Value const * value = Resolve_In(files[index], key);
			if (value != NULL) {
				found = value;
				return(true);
			}
		}
		return(false);
	}

}    // namespace

void Init_Localization(void)
{
    FixedFiles.clear();

    CCFileClass file(UI_INI_NAME);
    if (!file.Is_Available()) {
        return;
    }

    CCINIClass ini;
    ini.Load(file, false);
    if (!ini.Is_Present(LOC_SECTION)) {
        return;
    }

    char list[1024];
    ini.Get_String(LOC_SECTION, "Files", "", list, sizeof(list));
    Load_Chain(list, FixedFiles);
    DebugString("Localization: fixed chain has %d file(s)\n", (int)FixedFiles.size());
}

void Load_Map_Localization(char const * map_filename)
{
    Unload_Map_Localization();

    if (map_filename == NULL || *map_filename == '\0') {
        return;
    }

    CCFileClass file(map_filename);
    if (!file.Is_Available()) {
        return;
    }

    CCINIClass ini;
    ini.Load(file, false);
    if (!ini.Is_Present(LOC_SECTION)) {
        DebugString("Localization: map '%s' has no [Localization] section\n", map_filename);
        return;
    }

    char list[1024];
    ini.Get_String(LOC_SECTION, "Files", "", list, sizeof(list));
    Load_Chain(list, MapFiles);
    DebugString("Localization: map chain has %d file(s)\n", (int)MapFiles.size());
}

void Unload_Map_Localization(void)
{
    MapFiles.clear();
}

void Set_Localization_Language(char const * language)
{
    Language = (language != NULL) ? language : "";
    DebugString("Localization: language set to '%s'\n", Language.c_str());
}

char const * Get_Localization_Language(void)
{
    return(Language.c_str());
}

char const * Localize(char const * key)
{
    if (key == NULL || *key == '\0') {
        return("");
    }

    Json::Value const * found = NULL;

    /*
    ** A selected language wins: "<key>.<region>" (flat) or "<key>" -> "<region>"
    ** (nested drill-down) is tried across the whole chain first. The answer is the
    ** string the chain document stores, so its pointer stays valid for as long as
    ** the chain is loaded -- concurrent holders of different answers never share
    ** one buffer.
    */
    if (!Language.empty()) {
        std::string langkey(key);
        langkey += '.';
        langkey += Language;
        if (Find_Key(MapFiles, langkey.c_str(), found) || Find_Key(FixedFiles, langkey.c_str(), found)) {
            return(found->asCString());
        }
    }

    /*
    ** Fall back to the default, unsuffixed entries.
    */
    if (!Find_Key(MapFiles, key, found) && !Find_Key(FixedFiles, key, found)) {
        MissingBuffer = "MISSING:";
        MissingBuffer += key;
        return(MissingBuffer.c_str());
    }

    return(found->asCString());
}

char const * Localize(int txt_id)
{
    if (txt_id < 0 || txt_id >= TXT_KEY_COUNT || TXT_KEYS[txt_id] == NULL) {
        return("");
    }
    return(Localize(TXT_KEYS[txt_id]));
}

wchar_t const * Localize_Wide(char const * key)
{
    /*
    ** Narrow answer first (UTF-8), then convert to UTF-16 for the TTF wide text
    ** path. An invalid sequence falls back to a byte-wise widening so the MISSING
    ** prefix is always visible.
    */
    char const * text = Localize(key);
    WideBuffer.clear();

    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (len > 1) {
        WideBuffer.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, text, -1, &WideBuffer[0], len);
    } else {
        for (char const * p = text; *p != '\0'; ++p) {
            WideBuffer.push_back((wchar_t)(unsigned char)*p);
        }
    }
    return(WideBuffer.c_str());
}

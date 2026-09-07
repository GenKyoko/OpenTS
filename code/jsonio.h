/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

#pragma once

/***********************************************************************************************
 * JSON file I/O over the game's own file system.
 *
 * The JSON text is read and written through CCFileClass, so the files live by the same
 * rules as every other game data file: relative paths resolve against the game directory
 * and every registered mixfile, in the same search order the rest of the engine uses.
 * Parsing and generation are done by the bundled jsoncpp.
 ***********************************************************************************************/

#include <json/json.h>

#include <string>

/*
** Reads and parses a JSON file. The root is replaced with the parsed content on
** success and reset to null on failure. When errors is not null it receives the
** parser's message on failure. Returns false when the file is missing or malformed.
*/
bool Read_Json_File(char const * filename, Json::Value & root, std::string * errors = NULL);

/*
** Serializes the value (indented, UTF-8) and writes it to the file, replacing any
** existing file of the same name. Returns false when the write failed.
*/
bool Write_Json_File(char const * filename, Json::Value const & root);

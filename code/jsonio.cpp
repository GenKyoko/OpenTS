/*******************************************************************************
 *                                O P E N  T S
 ******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 <AUTHOR>
 *
 * Part of the OpenTS engine.
 ******************************************************************************/

#include "always.h"

#include "jsonio.h"

#include "ccfile.h"
#include "dbgprint.h"

#include <json/json.h>

#include <sstream>
#include <string>
bool Read_Json_File(char const * filename, Json::Value & root, std::string * errors)
{
    root = Json::Value();

    CCFileClass file(filename);
    if (!file.Is_Available()) {
        DebugString("JSON: '%s' not found\n", filename);
        return(false);
    }

    int size = file.Size();
    if (size <= 0) {
        DebugString("JSON: '%s' is empty\n", filename);
        return(false);
    }

    std::string text(size, char());
    int read = file.Read(&text[0], size);
    if (read > 0 && read < size) {
        text.resize(read);
    }

    /*
    ** Skip a UTF-8 byte order mark; jsoncpp's parser does not accept one.
    */
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text.erase(0, 3);
    }

    Json::CharReaderBuilder builder;
    std::istringstream stream(text);
    std::string localerrors;
    if (!Json::parseFromStream(builder, stream, &root, &localerrors)) {
        DebugString("JSON: parse error in '%s': %s\n", filename, localerrors.c_str());
        if (errors != NULL) {
            *errors = localerrors;
        }
        return(false);
    }
    return(true);
}
bool Write_Json_File(char const * filename, Json::Value const & root)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    builder["commentStyle"] = "None";
    std::string text = Json::writeString(builder, root);

    CCFileClass file(filename);
    file.Delete();

    int written = file.Write(text.data(), (int)text.size());
    if (written != (int)text.size()) {
        DebugString("JSON: failed to write '%s' (%d of %d bytes)\n", filename, written, (int)text.size());
        return(false);
    }
    DebugString("JSON: wrote '%s' (%d bytes)\n", filename, (int)text.size());
    return(true);
}

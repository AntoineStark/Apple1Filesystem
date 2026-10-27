#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace apple1
{
    inline constexpr std::string_view shell_demo =
        "HELP\n"
        "MEM\n"
        "PWD\n"
        "LS\n"
        "ECHO APPLE 1 SHELL DEMO\n"
        "MKDIR DEMO\n"
        "CD DEMO\n"
        "PWD\n"
        "WRITE RAW HELLO WITHOUT A NEWLINE\n"
        "ECHO APPENDED LINE >> RAW\n"
        "CAT RAW\n"
        "ECHO FIRST BLOCK DEMO > LARGE\n"
        "ECHO SECOND LINE WITH MORE TEXT >> LARGE\n"
        "ECHO THIRD LINE WITH MORE TEXT >> LARGE\n"
        "ECHO FOURTH LINE WITH MORE TEXT >> LARGE\n"
        "ECHO FIFTH LINE WITH MORE TEXT >> LARGE\n"
        "ECHO SIXTH LINE WITH MORE TEXT >> LARGE\n"
        "ECHO SEVENTH LINE WITH MORE TEXT >> LARGE\n"
        "ECHO EIGHTH LINE WITH MORE TEXT >> LARGE\n"
        "ECHO NINTH LINE WITH MORE TEXT >> LARGE\n"
        "ECHO TENTH LINE CROSSES THE BLOCK BOUNDARY >> LARGE\n"
        "CAT LARGE\n"
        "ECHO HELLO WORLD > NOTE\n"
        "CAT NOTE\n"
        "WC NOTE\n"
        "CAT NOTE | WC\n"
        "CAT NOTE | GREP HELLO > MATCH\n"
        "CAT MATCH\n"
        "CAT NOTE | GREP MISSING | WC\n"
        "LS | GREP NOTE\n"
        "LS > COMMANDS\n"
        "CAT NOTE | CAT | WRITE COPY\n"
        "CP NOTE BACKUP\n"
        "MV BACKUP SAVED\n"
        "MKDIR SUB\n"
        "CP NOTE SUB\n"
        "MV SAVED SUB/RENAMED\n"
        "LS SUB\n"
        "CD SUB\n"
        "PWD\n"
        "CAT ../MATCH\n"
        "CAT /DEMO/NOTE\n"
        "CD ..\n"
        "MKDIR EMPTY\n"
        "RMDIR EMPTY\n"
        "WRITE ZERO\n"
        "WC ZERO\n"
        "RM COPY\n"
        "LS .\n"
        "FORMAT\n"
        "CD /\n"
        "LS /DEMO\n"
        "ECHO DEMO COMPLETE\n";

    struct EditorLine
    {
        std::string text;
        size_t next_cursor;
    };

    inline EditorLine editor_line(std::string_view text, size_t cursor)
    {
        cursor = std::min(cursor, text.size());
        const auto previous = cursor == 0 ? std::string_view::npos : text.rfind('\n', cursor - 1);
        const size_t start = previous == std::string_view::npos ? 0 : previous + 1;
        const auto newline = text.find('\n', cursor);
        size_t end = newline == std::string_view::npos ? text.size() : newline;
        if (end > start && text[end - 1] == '\r')
            --end;
        return {std::string(text.substr(start, end - start)),
                newline == std::string_view::npos ? text.size() : newline + 1};
    }
}
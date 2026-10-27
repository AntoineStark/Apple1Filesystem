#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace apple1
{
    inline constexpr std::string_view shell_demo =
        "HELP | GREP FILE\n"
        "MKDIR DEMO\n"
        "CD DEMO\n"
        "HELP > COMMANDS\n"
        "GREP FILE COMMANDS | WC\n"
        "ECHO HELLO WORLD > NOTE\n"
        "ECHO SECOND LINE >> NOTE\n"
        "CAT NOTE | GREP HELLO > MATCH\n"
        "MKDIR SUB\n"
        "CP NOTE SUB\n"
        "MV SUB/NOTE SUB/SAVED\n"
        "CD SUB\n"
        "PWD\n"
        "CAT ../NOTE | GREP SECOND | WRITE COPY\n"
        "GREP HELLO ./SAVED\n"
        "CAT /DEMO/MATCH | WC\n"
        "LS ../SUB\n"
        "CP ../COMMANDS ../LARGE\n"
        "MEM >> ../LARGE\n"
        "WC ../LARGE\n"
        "CD ../..\n"
        "LS /DEMO\n"
        "CAT DEMO/SUB/COPY\n"
        "CD /DEMO\n"
        "MKDIR EMPTY\n"
        "RMDIR EMPTY\n"
        "WRITE ZERO\n"
        "CAT ZERO | GREP NOTHING | WC\n"
        "CP MATCH TEMP\n"
        "RM TEMP\n"
        "FORMAT\n"
        "CD /\n"
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
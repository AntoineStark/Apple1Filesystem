#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace apple1
{
    inline constexpr std::string_view shell_demo =
        "HELP\n"
        "HELP | GREP MKDIR\n"
        "MKDIR DEMO\n"
        "CD DEMO\n"
        "PWD\n"
        "HELP > COMMANDS\n"
        "LS\n"
        "CAT COMMANDS\n"
        "GREP FILE COMMANDS | WC\n"
        "HELP | GREP FILE | GREP DEST\n"
        "MKDIR ../DEMO2\n"
        "CD ../DEMO2\n"
        "LS\n"
        "CP ../DEMO/COMMANDS ../DEMO2/CMD\n"
        "LS\n"
        "CAT CMD\n"
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
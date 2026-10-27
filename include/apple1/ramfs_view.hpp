#pragma once

#include "apple1/machine.hpp"

namespace apple1
{
    struct RamFileView
    {
        int slot;
        std::string name;
        std::vector<uint8_t> data;
    };
    struct RamFsView
    {
        std::string error;
        std::vector<RamFileView> files;
        size_t bytes = 0;
    };
    RamFsView inspect_ramfs(const Machine &machine);
}
#pragma once

#include "apple1/machine.hpp"

namespace apple1
{
    struct RamFileView
    {
        int slot;
        std::string name;
        std::vector<uint8_t> data;
        bool directory = false;
        int data_slot = 0;
        int parent_slot = 1;
        std::string path;
    };
    struct RamFsView
    {
        std::string error;
        std::vector<RamFileView> files;
        size_t bytes = 0;
        size_t allocated_slots = 0;
    };
    RamFsView inspect_ramfs(const Machine &machine);
}
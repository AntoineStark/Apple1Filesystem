#include "apple1/ramfs_view.hpp"
#include <array>
#include <functional>
#include <set>
#include <stdexcept>

namespace apple1
{
    RamFsView inspect_ramfs(const Machine &machine)
    {
        RamFsView view;
        const auto read = [&](unsigned address)
        { return machine.peek(static_cast<uint16_t>(address)); };
        const auto require = [](bool condition, const char *message)
        {
            if (!condition)
                throw std::runtime_error(message);
        };
        try
        {
            for (unsigned segment = 10; segment <= 12; ++segment)
                require(machine.segments[segment] == Segment::Ram, "RAMFS needs RAM at $A000-$CFFF.");
            const std::array<uint8_t, 5> signature{'A', '1', 'F', 'S', 2};
            for (unsigned index = 0; index < signature.size(); ++index)
                require(read(0xa000 + index) == signature[index], "No valid A1FS version 2 filesystem; old formats require explicit FORMAT YES.");
            require(read(0xa010) == 3 && read(0xa011) == 2 && read(0xa051) == 1,
                    "Invalid reserved header/root slots.");
            std::array<bool, 48> seen{};
            seen[0] = seen[1] = true;
            std::function<void(int, const std::string &, unsigned)> visit;
            visit = [&](int directory, const std::string &path, unsigned depth)
            {
                require(depth <= 15, "Directory depth exceeds limit or contains a cycle.");
                std::set<std::string> names;
                for (int index = 0; index < 16; ++index)
                {
                    const unsigned address = 0xa000 + directory * 256 + index * 16;
                    if (!read(address))
                        continue;
                    RamFileView file{};
                    file.slot = directory * 16 + index;
                    file.parent_slot = directory;
                    bool padding = false;
                    for (unsigned letter = 0; letter < 8; ++letter)
                    {
                        const auto value = read(address + letter);
                        if (!value)
                        {
                            padding = true;
                            continue;
                        }
                        require(!padding && ((value >= 'A' && value <= 'Z') ||
                                             (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-'),
                                "Invalid filename or padding.");
                        file.name += static_cast<char>(value);
                    }
                    require(file.name != "." && file.name != "..", "Reserved directory name stored in metadata.");
                    require(names.insert(file.name).second, "Duplicate name within a directory.");
                    const unsigned length = read(address + 8);
                    const unsigned type = read(address + 9);
                    file.data_slot = read(address + 10);
                    file.directory = type == 2;
                    file.path = path + file.name;
                    require((type == 1 || type == 2) && length <= 240 && (!file.directory || length == 0),
                            "Invalid entry type or length.");
                    if (file.data_slot)
                    {
                        require(file.data_slot >= 2 && file.data_slot < 48 && !seen[file.data_slot],
                                "Invalid or multiply referenced storage slot.");
                        seen[file.data_slot] = true;
                        require(read(0xa010 + file.data_slot) == type, "Allocation type mismatch.");
                    }
                    else
                    {
                        require(!file.directory && length == 0, "Nonempty file or directory has no slot.");
                    }
                    if (file.directory)
                    {
                        require(read(0xa050 + file.data_slot) == directory, "Directory parent mismatch.");
                        const auto child_path = file.path + '/';
                        const auto child_slot = file.data_slot;
                        view.files.push_back(std::move(file));
                        visit(child_slot, child_path, depth + 1);
                    }
                    else
                    {
                        for (unsigned byte = 0; byte < length; ++byte)
                            file.data.push_back(read(0xa000 + file.data_slot * 256 + byte));
                        view.bytes += length;
                        view.files.push_back(std::move(file));
                    }
                }
            };
            visit(1, "/", 0);
            for (unsigned slot = 0; slot < 48; ++slot)
            {
                const auto type = read(0xa010 + slot);
                require((type != 0) == seen[slot] && (slot == 0 || type <= 2),
                        "Invalid allocation table or orphaned slot.");
                view.allocated_slots += seen[slot];
            }
        }
        catch (const std::runtime_error &error)
        {
            view.error = error.what();
            view.files.clear();
            view.bytes = view.allocated_slots = 0;
        }
        return view;
    }
}
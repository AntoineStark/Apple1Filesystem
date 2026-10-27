#include "apple1/ramfs_view.hpp"
#include <algorithm>
#include <array>

namespace apple1 {
RamFsView inspect_ramfs(const Machine& machine) {
    RamFsView view;
    auto invalid = [&](const std::string& reason) {
        view.error = reason;
        view.files.clear();
        view.bytes = 0;
        return view;
    };
    if (machine.segments[10] != Segment::Ram)
        return invalid("RAM filesystem is not mapped at $A000.");
    const std::array<uint8_t, 5> signature{'A', '1', 'F', 'S', 1};
    for (size_t index = 0; index < signature.size(); ++index)
        if (machine.peek(static_cast<uint16_t>(0xa000 + index)) != signature[index])
            return invalid("No valid A1FS version 1 filesystem.");
    for (int slot = 0; slot < 15; ++slot) {
        const uint16_t address = static_cast<uint16_t>(0xa100 + slot * 256);
        if (machine.peek(address) == 0) continue;
        RamFileView file{slot, {}, {}};
        bool padding = false;
        for (unsigned index = 0; index < 8; ++index) {
            const auto value = machine.peek(address + index);
            if (value == 0) { padding = true; continue; }
            if (padding || !((value >= 'A' && value <= 'Z') ||
                             (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-'))
                return invalid("Invalid filename metadata; inspection stopped.");
            file.name += static_cast<char>(value);
        }
        if (std::any_of(view.files.begin(), view.files.end(),
                        [&](const auto& other) { return other.name == file.name; }))
            return invalid("Duplicate filenames; inspection stopped.");
        const auto length = machine.peek(address + 8);
        if (length > 240) return invalid("Invalid file length; inspection stopped.");
        for (unsigned index = 0; index < length; ++index)
            file.data.push_back(machine.peek(address + 16 + index));
        view.bytes += length;
        view.files.push_back(std::move(file));
    }
    return view;
}
}
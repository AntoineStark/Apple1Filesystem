#include "apple1/machine.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace apple1
{

    Machine::Machine(Profile profile) : profile_(profile)
    {
        rom.fill(0xff);
        monitor.fill(0xff);
        if (profile == Profile::Standard)
        {
            motherboard_low = motherboard_high = motherboard_monitor = true;
        }
        else
        {
            for (unsigned segment : {0, 1, 6, 10, 11, 12})
                segments[segment] = Segment::Ram;
            for (unsigned segment : {2, 3, 4, 5, 7, 8, 9, 14, 15})
                segments[segment] = Segment::Rom;
        }
        cpu.read = [this](uint16_t address)
        { return read(address); };
        cpu.write = [this](uint16_t address, uint8_t value)
        { write(address, value); };
        clear_screen();
    }

    std::vector<uint8_t> Machine::read_binary(const std::filesystem::path &path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot open ROM: " + path.string());
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    void Machine::load_rom(const std::filesystem::path &path)
    {
        const auto data = read_binary(path);
        const size_t expected = profile_ == Profile::Standard ? monitor.size() : rom.size();
        if (data.size() != expected)
            throw std::runtime_error("ROM must contain exactly " + std::to_string(expected) + " bytes");
        if (profile_ == Profile::Standard)
            std::copy(data.begin(), data.end(), monitor.begin());
        else
            std::copy(data.begin(), data.end(), rom.begin());
    }

    void Machine::validate_mapping() const
    {
        if (segments[13] != Segment::Disconnected)
            throw std::runtime_error("Card segment D must be disconnected for the motherboard PIA");
        if ((motherboard_low && segments[0] != Segment::Disconnected) ||
            (motherboard_high && segments[14] != Segment::Disconnected) ||
            (motherboard_monitor && segments[15] != Segment::Disconnected))
            throw std::runtime_error("Card and motherboard memory mappings conflict");
    }

    bool Machine::is_pia(uint16_t address) { return (address & 0xfffc) == 0xd010; }

    uint8_t Machine::keyboard_control() const
    {
        return keyboard_cr_ | (keyboard_ready_ ? 0x80 : 0);
    }

    uint8_t Machine::display_control() const { return display_cr_; }

    uint8_t Machine::peek(uint16_t address) const
    {
        if (is_pia(address))
        {
            switch (address & 3)
            {
            case 0:
                return (keyboard_cr_ & 4) ? keyboard_data_ : keyboard_ddr_;
            case 1:
                return keyboard_control();
            case 2:
                return (display_cr_ & 4)
                           ? static_cast<uint8_t>((display_data_ & 0x7f) | (display_pending_ ? 0x80 : 0))
                           : display_ddr_;
            case 3:
                return display_control();
            }
        }
        const auto segment = segments[address >> 12];
        if (segment == Segment::Ram)
            return ram[address & 0x7fff];
        if (segment == Segment::Rom)
            return rom[address & 0x7fff];
        if (motherboard_low && address < 0x1000)
            return motherboard_ram[address];
        if (motherboard_high && address >= 0xe000 && address < 0xf000)
            return motherboard_ram[0x1000 + (address & 0xfff)];
        if (motherboard_monitor && address >= 0xff00)
            return monitor[address & 0xff];
        return 0xff;
    }

    uint8_t Machine::read(uint16_t address)
    {
        const uint8_t value = peek(address);
        if (address == 0xd010 && (keyboard_cr_ & 4))
        {
            keyboard_ready_ = false;
            keyboard_ready_at_ = clock_ + 1000;
        }
        return value;
    }

    void Machine::write(uint16_t address, uint8_t value)
    {
        if (is_pia(address))
        {
            switch (address & 3)
            {
            case 0:
                if (!(keyboard_cr_ & 4))
                    keyboard_ddr_ = value;
                break;
            case 1:
                keyboard_cr_ = value & 0x3f;
                break;
            case 2:
                if (!(display_cr_ & 4))
                    display_ddr_ = value;
                else if (!display_pending_)
                {
                    display_data_ = value & display_ddr_;
                    display_pending_ = true;
                    display_ready_at_ = (clock_ / 16667 + 1) * 16667;
                }
                break;
            case 3:
                display_cr_ = value & 0x3f;
                break;
            }
            return;
        }
        const auto segment = segments[address >> 12];
        if (segment == Segment::Ram)
            ram[address & 0x7fff] = value;
        else if (segment == Segment::Disconnected)
        {
            if (motherboard_low && address < 0x1000)
                motherboard_ram[address] = value;
            if (motherboard_high && address >= 0xe000 && address < 0xf000)
                motherboard_ram[0x1000 + (address & 0xfff)] = value;
        }
    }

    void Machine::clear_screen()
    {
        for (auto &line : screen)
            line.fill(' ');
        column = row = 0;
    }

    void Machine::emit(uint8_t value)
    {
        value &= 0x7f;
        if (value == 13)
            column = 0, ++row;
        else if (value >= 32 && value != 127)
        {
            if (value >= 96)
                value -= 32;
            screen[row][column++] = static_cast<char>(value);
            if (column == 40)
                column = 0, ++row;
        }
        else
            return;
        if (row == 24)
        {
            for (unsigned line = 1; line < 24; ++line)
                screen[line - 1] = screen[line];
            screen.back().fill(' ');
            row = 23;
        }
        if (output)
            output(static_cast<char>(value));
    }

    void Machine::enqueue(std::string text)
    {
        for (unsigned char value : text)
        {
            if (value == '\n')
                value = '\r';
            if (value == 8 || value == 127)
                value = '_';
            if (value < 128)
                input_.push_back(static_cast<uint8_t>(std::toupper(value)) | 0x80);
        }
    }

    size_t Machine::pending_input() const { return input_.size() + (keyboard_ready_ ? 1 : 0); }

    void Machine::clear_input()
    {
        input_.clear();
        keyboard_ready_ = false;
    }

    void Machine::tick()
    {
        if (display_pending_ && clock_ >= display_ready_at_)
        {
            display_pending_ = false;
            emit(display_data_);
        }
        if (!keyboard_ready_ && !input_.empty() && clock_ >= keyboard_ready_at_)
        {
            keyboard_data_ = input_.front();
            input_.pop_front();
            keyboard_ready_ = true;
        }
    }

    unsigned Machine::step()
    {
        const unsigned elapsed = cpu.step();
        clock_ += elapsed;
        tick();
        return elapsed;
    }

    void Machine::reset()
    {
        validate_mapping();
        keyboard_cr_ = display_cr_ = keyboard_ddr_ = display_ddr_ = 0;
        keyboard_data_ = display_data_ = 0;
        keyboard_ready_ = display_pending_ = false;
        input_.clear();
        clock_ = keyboard_ready_at_ = display_ready_at_ = 0;
        cpu.reset();
    }

    void Machine::power_cycle()
    {
        ram.fill(0);
        motherboard_ram.fill(0);
        clear_screen();
        reset();
    }

    std::string Machine::owner(uint16_t address) const
    {
        if (is_pia(address))
            return "PIA";
        if (segments[address >> 12] == Segment::Ram)
            return "Card RAM";
        if (segments[address >> 12] == Segment::Rom)
            return "Card ROM";
        if ((motherboard_low && address < 0x1000) ||
            (motherboard_high && address >= 0xe000 && address < 0xf000))
            return "Motherboard RAM";
        if (motherboard_monitor && address >= 0xff00)
            return "WozMon";
        return "Unmapped";
    }

}
#pragma once

#include "apple1/cpu.hpp"
#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace apple1 {

enum class Segment { Disconnected, Ram, Rom };
enum class Profile { Standard, Expansion };

class Machine {
public:
    explicit Machine(Profile profile = Profile::Expansion);
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Cpu cpu;
    std::array<Segment, 16> segments{};
    bool motherboard_low = false;
    bool motherboard_high = false;
    bool motherboard_monitor = false;
    std::array<uint8_t, 32768> ram{};
    std::array<uint8_t, 32768> rom{};
    std::array<uint8_t, 8192> motherboard_ram{};
    std::array<uint8_t, 256> monitor{};
    std::array<std::array<char, 40>, 24> screen{};
    unsigned column = 0, row = 0;
    std::function<void(char)> output;
    void load_rom(const std::filesystem::path& path);
    void validate_mapping() const;
    void reset();
    void power_cycle();
    void clear_screen();
    void enqueue(std::string text);
    void clear_input();
    unsigned step();
    uint8_t read(uint16_t address);
    uint8_t peek(uint16_t address) const;
    void write(uint16_t address, uint8_t value);
    std::string owner(uint16_t address) const;
    size_t pending_input() const;
    uint8_t keyboard_control() const;
    uint8_t display_control() const;
    uint64_t ticks() const { return clock_; }
    static std::vector<uint8_t> read_binary(const std::filesystem::path& path);

private:
    Profile profile_;
    uint64_t clock_ = 0, display_ready_at_ = 0, keyboard_ready_at_ = 0;
    uint8_t keyboard_cr_ = 0, display_cr_ = 0;
    uint8_t keyboard_ddr_ = 0, display_ddr_ = 0;
    uint8_t keyboard_data_ = 0, display_data_ = 0;
    bool keyboard_ready_ = false, display_pending_ = false;
    std::deque<uint8_t> input_;
    void tick();
    void emit(uint8_t value);
    static bool is_pia(uint16_t address);
};

}
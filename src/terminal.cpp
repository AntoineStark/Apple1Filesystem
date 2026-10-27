#include "apple1/machine.hpp"
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <termios.h>
#include <thread>
#include <unistd.h>

namespace
{
    volatile std::sig_atomic_t interrupted = 0;

    void stop(int signal) { interrupted = signal; }

    class Terminal
    {
        termios original_{};
        bool active_ = false;
        struct sigaction old_[4]{};
        unsigned installed_ = 0;
        static constexpr int signals_[4] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};

    public:
        Terminal()
        {
            struct sigaction action{};
            action.sa_handler = stop;
            sigemptyset(&action.sa_mask);
            for (int signal : signals_)
            {
                if (sigaction(signal, &action, &old_[installed_]) != 0)
                {
                    restore();
                    throw std::runtime_error("Cannot install signal handler");
                }
                ++installed_;
            }
            if (isatty(STDIN_FILENO))
            {
                if (tcgetattr(STDIN_FILENO, &original_) != 0)
                {
                    restore();
                    throw std::runtime_error("Cannot read terminal settings");
                }
                auto raw = original_;
                cfmakeraw(&raw);
                raw.c_cc[VMIN] = 0;
                raw.c_cc[VTIME] = 0;
                if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
                {
                    restore();
                    throw std::runtime_error("Cannot enable raw terminal");
                }
                active_ = true;
            }
        }

        Terminal(const Terminal &) = delete;
        Terminal &operator=(const Terminal &) = delete;
        ~Terminal() { restore(); }

    private:
        void restore() noexcept
        {
            if (active_)
            {
                while (tcsetattr(STDIN_FILENO, TCSANOW, &original_) != 0 && errno == EINTR)
                {
                }
                active_ = false;
            }
            while (installed_)
            {
                --installed_;
                sigaction(signals_[installed_], &old_[installed_], nullptr);
            }
        }
    };

    struct Options
    {
        apple1::Profile profile = apple1::Profile::Expansion;
        std::optional<std::string> rom;
        std::optional<uint64_t> cycles;
        std::string input;
        bool turbo = false;
    };

    void usage()
    {
        std::cout << "Usage: apple1-terminal [--profile standard|expansion] [--rom PATH]\n"
                     "                       [--turbo] [--cycles N] [--input TEXT] [--help]\n"
                     "Default profile: expansion. ROM defaults: roms/32KA1COMPIL.BIN\n"
                     "(expansion), roms/wozmon.rom (standard), relative to working directory.\n"
                     "Input text is literal; use shell quoting for newlines.\n"
                     "Ctrl-] exit, Ctrl-R reset, Ctrl-L clear, Ctrl-P pause/resume.\n"
                     "Without --turbo, execution is paced at 1 MHz. --cycles bounds CPU\n"
                     "execution (the final instruction may cross the limit).\n";
    }

    Options parse(int argc, char **argv)
    {
        Options options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            if (argument == "--turbo")
                options.turbo = true;
            else if (argument == "--profile" || argument == "--rom" ||
                     argument == "--cycles" || argument == "--input")
            {
                if (++index == argc)
                    throw std::runtime_error("Missing value for " + std::string(argument));
                const std::string_view value = argv[index];
                if (argument == "--profile")
                {
                    if (value == "standard")
                        options.profile = apple1::Profile::Standard;
                    else if (value == "expansion")
                        options.profile = apple1::Profile::Expansion;
                    else
                        throw std::runtime_error("Profile must be standard or expansion");
                }
                else if (argument == "--rom")
                    options.rom = std::string(value);
                else if (argument == "--input")
                    options.input += value;
                else
                {
                    uint64_t count = 0;
                    const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
                    if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size())
                        throw std::runtime_error("Cycles must be an unsigned integer");
                    options.cycles = count;
                }
            }
            else
                throw std::runtime_error("Unknown option: " + std::string(argument));
        }
        return options;
    }
}

int main(int argc, char **argv)
{
    try
    {
        for (int index = 1; index < argc; ++index)
            if (std::string_view(argv[index]) == "--help")
            {
                usage();
                return 0;
            }
        const auto options = parse(argc, argv);
        apple1::Machine machine(options.profile);
        machine.load_rom(options.rom.value_or(options.profile == apple1::Profile::Standard
                                                  ? "roms/wozmon.rom"
                                                  : "roms/32KA1COMPIL.BIN"));
        machine.reset();
        unsigned column = 0;
        machine.output = [&](char value)
        {
            if (value == '\r')
            {
                std::cout << "\r\n";
                column = 0;
            }
            else
            {
                std::cout.put(value);
                if (++column == 40)
                {
                    std::cout << "\r\n";
                    column = 0;
                }
            }
            std::cout.flush();
        };
        Terminal terminal;
        std::deque<char> input(options.input.begin(), options.input.end());
        using Clock = std::chrono::steady_clock;
        auto epoch = Clock::now();
        uint64_t executed = 0, paced_cycles = 0, next_input = 0;
        bool paused = false, running = true, stdin_open = true;
        while (running && !interrupted && (!options.cycles || executed < *options.cycles))
        {
            if (stdin_open)
            {
                pollfd descriptor{STDIN_FILENO, POLLIN, 0};
                const int ready = poll(&descriptor, 1, paused ? 20 : 0);
                if (ready < 0 && errno != EINTR)
                    throw std::runtime_error("Cannot poll stdin");
                if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)))
                {
                    char buffer[256];
                    const auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
                    if (count < 0 && errno != EINTR && errno != EAGAIN)
                        throw std::runtime_error("Cannot read stdin");
                    if (count == 0)
                        stdin_open = false;
                    for (ssize_t index = 0; index < count; ++index)
                    {
                        const unsigned char value = buffer[index];
                        if (value == 29)
                            running = false;
                        else if (value == 18)
                        {
                            machine.reset();
                            input.clear();
                            next_input = 0;
                            paced_cycles = 0;
                            epoch = Clock::now();
                        }
                        else if (value == 12)
                        {
                            machine.clear_screen();
                            column = 0;
                            std::cout << "\033[2J\033[H" << std::flush;
                        }
                        else if (value == 16)
                        {
                            paused = !paused;
                            paced_cycles = 0;
                            epoch = Clock::now();
                        }
                        else
                            input.push_back(static_cast<char>(value));
                    }
                }
                if (ready > 0 && (descriptor.revents & (POLLERR | POLLNVAL)))
                    throw std::runtime_error("Stdin poll error");
            }
            if (!running || interrupted)
                break;
            if (paused)
            {
                if (!stdin_open)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (!input.empty() && machine.pending_input() == 0 && machine.ticks() >= next_input)
            {
                machine.enqueue(std::string(1, input.front()));
                input.pop_front();
                next_input = machine.ticks() + 100000;
            }
            uint64_t batch = 0;
            while (batch < 1000 && (!options.cycles || executed < *options.cycles))
            {
                const auto elapsed = machine.step();
                batch += elapsed;
                executed += elapsed;
            }
            if (!options.turbo)
            {
                paced_cycles += batch;
                std::this_thread::sleep_until(epoch + std::chrono::microseconds(paced_cycles));
            }
        }
        return interrupted ? 128 + interrupted : 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "apple1-terminal: " << error.what() << '\n';
        return 1;
    }
}
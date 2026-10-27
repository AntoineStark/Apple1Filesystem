#include "apple1/machine.hpp"
#include "apple1/gui_input.hpp"
#include "apple1/ramfs_view.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <deque>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    struct Options
    {
#ifdef APPLE1_OS_ROM
        std::string rom = APPLE1_OS_ROM;
#else
        std::string rom = "roms/32KA1COMPIL.BIN";
#endif
        apple1::Profile profile = apple1::Profile::Expansion;
        bool smoke = false;
        bool input_smoke = false;
        bool demo_smoke = false;
        std::string screenshot;

        bool shell_rom() const
        {
#ifdef APPLE1_OS_ROM
            return profile == apple1::Profile::Expansion && rom == APPLE1_OS_ROM;
#else
            return false;
#endif
        }
    };

    Options parse_options(int argc, char **argv)
    {
        Options options;
        bool explicit_rom = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            auto value = [&]() -> std::string
            {
                if (++index == argc)
                    throw std::runtime_error("Missing value for " + argument);
                return argv[index];
            };
            if (argument == "--rom")
            {
                options.rom = value();
                explicit_rom = true;
            }
            else if (argument == "--profile")
            {
                const auto profile = value();
                if (profile == "standard")
                    options.profile = apple1::Profile::Standard;
                else if (profile == "expansion")
                    options.profile = apple1::Profile::Expansion;
                else
                    throw std::runtime_error("Profile must be standard or expansion");
            }
            else if (argument == "--smoke-test")
                options.smoke = true;
            else if (argument == "--input-smoke-test")
                options.smoke = options.input_smoke = true;
            else if (argument == "--demo-smoke-test")
                options.smoke = options.demo_smoke = true;
            else if (argument == "--screenshot")
                options.screenshot = value();
            else
                throw std::runtime_error("Unknown option: " + argument);
        }
        if (!explicit_rom && options.profile == apple1::Profile::Standard)
            options.rom = "roms/wozmon.rom";
        return options;
    }

    struct Native
    {
        SDL_Window *window = nullptr;
        SDL_GLContext context = nullptr;
        bool imgui = false, platform = false, renderer = false;
        ~Native()
        {
            if (renderer)
                ImGui_ImplOpenGL3_Shutdown();
            if (platform)
                ImGui_ImplSDL2_Shutdown();
            if (imgui)
                ImGui::DestroyContext();
            if (context)
                SDL_GL_DeleteContext(context);
            if (window)
                SDL_DestroyWindow(window);
            SDL_Quit();
        }
        void start(bool smoke)
        {
            SDL_SetMainReady();
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
                throw std::runtime_error(SDL_GetError());
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            window = SDL_CreateWindow("Apple 1 | Native Debugger", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, 1440, 960,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
            if (!window)
                throw std::runtime_error(SDL_GetError());
            context = SDL_GL_CreateContext(window);
            if (!context)
                throw std::runtime_error(SDL_GetError());
            if (SDL_GL_MakeCurrent(window, context) != 0)
                throw std::runtime_error(SDL_GetError());
            SDL_GL_SetSwapInterval(smoke ? 0 : 1);
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            imgui = true;
            auto &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.IniFilename = "apple1-gui.ini";
            io.Fonts->AddFontDefault();
            ImGui::StyleColorsDark();
            auto &style = ImGui::GetStyle();
            style.WindowRounding = 0;
            style.FrameRounding = 2;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.065f, 1);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.83f, 0.55f, 1);
            platform = ImGui_ImplSDL2_InitForOpenGL(window, context);
            if (!platform)
                throw std::runtime_error("ImGui SDL2 initialization failed");
            renderer = ImGui_ImplOpenGL3_Init("#version 150");
            if (!renderer)
                throw std::runtime_error("ImGui OpenGL3 initialization failed");
            SDL_StartTextInput();
        }
    };

    struct StopPoint
    {
        bool enabled = true;
        uint16_t address = 0;
        bool execute = true, read = false, write = false;
    };

    struct Debugger
    {
        apple1::Machine machine;
        std::vector<StopPoint> stops;
        std::deque<std::string> trace;
        std::string error, status = "Running", output;
        bool running = true, turbo = false, tracing = false, executing = false;
        bool watch_hit = false, keyboard_focus = false;
        bool focus_screen = true;
        bool show_files = true;
        bool focus_files = false;
        int selected_file = -1;
        ImVec2 reset_button_center{};
        std::deque<char> input_buffer;
        std::array<char, 4096> input_draft{};
        int input_cursor = 0;
        bool set_input_cursor = false;
        bool focus_input = false;
        bool append_return = true;
        uint64_t next_input = 0;
        std::optional<uint16_t> target, skip_breakpoint;
        std::optional<uint8_t> target_stack;
        uint16_t cursor = 0, disassembly_address = 0, memory_address = 0;
        bool follow_pc = true;
        int memory_view = 0;
        uint16_t raw_offset = 0;
        uint16_t edit_address = 0;
        uint8_t edit_value = 0;
        std::array<apple1::Segment, 16> draft_segments;
        bool draft_low, draft_high, draft_monitor;
        double budget = 0;

        explicit Debugger(apple1::Profile profile) : machine(profile),
                                                     draft_segments(machine.segments), draft_low(machine.motherboard_low),
                                                     draft_high(machine.motherboard_high), draft_monitor(machine.motherboard_monitor)
        {
            if (profile == apple1::Profile::Expansion)
                load_demo();
            machine.cpu.read = [this](uint16_t address)
            {
                const auto value = machine.read(address);
                watch(address, value, false);
                return value;
            };
            machine.cpu.write = [this](uint16_t address, uint8_t value)
            {
                machine.write(address, value);
                watch(address, value, true);
            };
            machine.output = [this](char value)
            {
                output += value;
                if (output.size() > 16384)
                    output.erase(0, output.size() - 16384);
            };
        }

        void load_demo()
        {
            input_draft.fill(0);
            std::copy(apple1::shell_demo.begin(), apple1::shell_demo.end(), input_draft.begin());
            input_cursor = 0;
            set_input_cursor = true;
        }

        bool send_line()
        {
            if (!input_buffer.empty() || machine.pending_input() != 0)
                return false;
            const auto line = apple1::editor_line(input_draft.data(), static_cast<size_t>(input_cursor));
            if (line.text.empty() && line.next_cursor == static_cast<size_t>(input_cursor) &&
                input_draft[input_cursor] == 0)
                return false;
            queue_input(line.text + '\r');
            input_cursor = static_cast<int>(line.next_cursor);
            set_input_cursor = true;
            return true;
        }

        template <class Action>
        void protect(Action action)
        {
            try
            {
                action();
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
                running = false;
                executing = false;
                target.reset();
                target_stack.reset();
                status = "Exception";
            }
        }

        void append_trace(std::string entry)
        {
            if (trace.size() >= 2048)
                trace.pop_front();
            trace.push_back(std::move(entry));
        }

        void watch(uint16_t address, uint8_t value, bool write)
        {
            if (!executing)
                return;
            for (const auto &stop : stops)
            {
                if (!stop.enabled || stop.address != address || !(write ? stop.write : stop.read))
                    continue;
                char text[96];
                std::snprintf(text, sizeof(text), "%s watchpoint $%04X = $%02X (instruction $%04X)",
                              write ? "Write" : "Read", address, value, instruction_pc);
                status = text;
                append_trace(status);
                watch_hit = true;
            }
        }

        uint16_t instruction_pc = 0;
        void queue_input(const std::string &text)
        {
            for (unsigned char value : text)
            {
                if (value >= 128)
                    continue;
                if (value == '\n')
                    value = '\r';
                if (value == 8 || value == 127)
                    value = '_';
                input_buffer.push_back(static_cast<char>(std::toupper(value)));
            }
        }

        void clear_input()
        {
            input_buffer.clear();
            machine.clear_input();
            next_input = 0;
        }

        unsigned one_step()
        {
            if (!input_buffer.empty() && machine.pending_input() == 0 && machine.ticks() >= next_input)
            {
                machine.enqueue(std::string(1, input_buffer.front()));
                input_buffer.pop_front();
                next_input = machine.ticks() + 100000;
            }
            instruction_pc = machine.cpu.pc;
            if (tracing)
            {
                char prefix[96];
                std::snprintf(prefix, sizeof(prefix), "%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X  ",
                              instruction_pc, machine.cpu.a, machine.cpu.x, machine.cpu.y, machine.cpu.sp, machine.cpu.p);
                append_trace(prefix + apple1::Cpu::disassemble(instruction_pc,
                                                               [this](uint16_t address)
                                                               { return machine.peek(address); }));
            }
            watch_hit = false;
            executing = true;
            unsigned cycles = 0;
            try
            {
                cycles = machine.step();
            }
            catch (...)
            {
                executing = false;
                throw;
            }
            executing = false;
            if (watch_hit)
                pause(false);
            return cycles;
        }

        void pause(bool set_status = true)
        {
            running = false;
            budget = 0;
            target.reset();
            target_stack.reset();
            skip_breakpoint.reset();
            if (set_status)
                status = "Paused";
        }

        void resume()
        {
            skip_breakpoint = machine.cpu.pc;
            running = true;
            budget = 0;
            status = "Running";
        }

        void step_over()
        {
            pause();
            if (machine.peek(machine.cpu.pc) != 0x20)
            {
                one_step();
                return;
            }
            target = static_cast<uint16_t>(machine.cpu.pc + 3);
            target_stack = machine.cpu.sp;
            resume();
            status = "Step over";
        }

        void reset(bool power)
        {
            pause();
            if (power)
                machine.power_cycle();
            else
                machine.reset();
            clear_input();
            output.clear();
            trace.clear();
            error.clear();
            resume();
            focus_screen = true;
            status = power ? "Power cycled; running" : "Reset; running";
        }

        void advance(double elapsed, bool smoke)
        {
            if (!running)
            {
                budget = 0;
                return;
            }
            budget += smoke ? 1000000.0 : turbo ? 250000.0
                                                : std::min(elapsed, 0.10) * 1000000.0;
            budget = std::min(budget, smoke ? 1000000.0 : 250000.0);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(smoke ? 1000 : 12);
            unsigned count = 0;
            while (running && budget > 0)
            {
                const uint16_t pc = machine.cpu.pc;
                if (target && pc == *target && (!target_stack || machine.cpu.sp == *target_stack))
                {
                    pause();
                    status = "Run target reached";
                    break;
                }
                bool hit = false;
                for (const auto &stop : stops)
                    hit |= stop.enabled && stop.execute && stop.address == pc;
                if (hit && (!skip_breakpoint || pc != *skip_breakpoint))
                {
                    pause();
                    status = "Execute breakpoint";
                    break;
                }
                skip_breakpoint.reset();
                const auto cycles = one_step();
                if (!cycles)
                    throw std::runtime_error("CPU returned a zero cycle instruction");
                budget -= cycles;
                if (++count % 1024 == 0 && std::chrono::steady_clock::now() >= deadline)
                    break;
            }
        }

        bool is_ram(uint16_t address) const
        {
            const auto owner = machine.owner(address);
            return owner == "Card RAM" || owner == "Motherboard RAM";
        }

        void apply_mapping()
        {
            const auto previous = machine.segments;
            const bool low = machine.motherboard_low, high = machine.motherboard_high,
                       monitor = machine.motherboard_monitor;
            machine.segments = draft_segments;
            machine.motherboard_low = draft_low;
            machine.motherboard_high = draft_high;
            machine.motherboard_monitor = draft_monitor;
            try
            {
                machine.validate_mapping();
            }
            catch (...)
            {
                machine.segments = previous;
                machine.motherboard_low = low;
                machine.motherboard_high = high;
                machine.motherboard_monitor = monitor;
                throw;
            }
            status = "Memory map applied";
        }
    };

    void hex16(const char *label, uint16_t &value)
    {
        ImGui::SetNextItemWidth(90);
        ImGui::InputScalar(label, ImGuiDataType_U16, &value, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
    }

    void layout()
    {
        const auto dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        static bool first = true;
        if (!first)
            return;
        first = false;
        if (ImGui::DockBuilderGetNode(dock)->ChildNodes[0])
            return;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
        ImGuiID center = dock, right, bottom, right_bottom, bottom_right;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.39f, &right, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.36f, &bottom, &center);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.52f, &right_bottom, &right);
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.45f, &bottom_right, &bottom);
        ImGui::DockBuilderDockWindow("Screen", center);
        ImGui::DockBuilderDockWindow("CPU / Disassembly", right);
        ImGui::DockBuilderDockWindow("Memory", bottom);
        ImGui::DockBuilderDockWindow("Files", bottom);
        ImGui::DockBuilderDockWindow("Memory Map", right_bottom);
        ImGui::DockBuilderDockWindow("Breakpoints", right_bottom);
        ImGui::DockBuilderDockWindow("PIA", right_bottom);
        ImGui::DockBuilderDockWindow("Trace", bottom_right);
        ImGui::DockBuilderDockWindow("Input Buffer", bottom_right);
        ImGui::DockBuilderDockWindow("Diagnostics", bottom_right);
        ImGui::DockBuilderFinish(dock);
    }

    void screen_window(Debugger &debugger)
    {
        debugger.keyboard_focus = false;
        if (ImGui::Begin("Screen"))
        {
            bool run = debugger.running;
            if (ImGui::Checkbox("Run", &run))
            {
                if (run)
                    debugger.resume();
                else
                    debugger.pause();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Turbo", &debugger.turbo);
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
                debugger.protect([&]
                                 { debugger.reset(false); });
            const auto reset_min = ImGui::GetItemRectMin();
            const auto reset_max = ImGui::GetItemRectMax();
            debugger.reset_button_center = ImVec2((reset_min.x + reset_max.x) * 0.5f,
                                                  (reset_min.y + reset_max.y) * 0.5f);
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                debugger.machine.clear_screen();
            ImGui::SameLine();
            if (ImGui::Button("Power cycle"))
                debugger.protect([&]
                                 { debugger.reset(true); });
            ImGui::TextWrapped("%s", debugger.status.c_str());
            if (!debugger.error.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.35f, 1));
                ImGui::TextWrapped("%s", debugger.error.c_str());
                ImGui::PopStyleColor();
            }
            const auto available = ImGui::GetContentRegionAvail();
            const auto padding = ImGui::GetStyle().WindowPadding;
            const float scale = std::clamp(std::min(
                                               (available.x - padding.x * 2 - 4) / (ImGui::CalcTextSize("M").x * 40),
                                               (available.y - padding.y * 2 - 4) / (ImGui::GetFontSize() * 24)),
                                           0.2f, 2.5f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.015f, 0.025f, 0.018f, 1));
            ImGui::BeginChild("Terminal", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (debugger.focus_screen)
            {
                ImGui::SetWindowFocus();
                debugger.focus_screen = false;
            }
            ImGui::SetWindowFontScale(scale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            debugger.keyboard_focus = ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.97f, 0.59f, 1));
            for (unsigned row = 0; row < 24; ++row)
            {
                auto line = debugger.machine.screen[row];
                if (row == debugger.machine.row && ImGui::GetTime() - int(ImGui::GetTime()) < 0.5)
                    line[std::min(debugger.machine.column, 39u)] = '@';
                ImGui::TextUnformatted(line.data(), line.data() + line.size());
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    unsigned instruction_length(const std::string &instruction)
    {
        if (instruction.rfind(".BYTE", 0) == 0)
            return 1;
        const auto mnemonic = instruction.substr(0, 3);
        if (mnemonic == "BCC" || mnemonic == "BCS" || mnemonic == "BEQ" || mnemonic == "BMI" ||
            mnemonic == "BNE" || mnemonic == "BPL" || mnemonic == "BVC" || mnemonic == "BVS")
            return 2;
        const auto dollar = instruction.find('$');
        if (dollar == std::string::npos)
            return 1;
        size_t end = dollar + 1;
        while (end < instruction.size() && std::isxdigit(static_cast<unsigned char>(instruction[end])))
            ++end;
        return end - dollar > 3 ? 3 : 2;
    }

    void cpu_window(Debugger &debugger)
    {
        if (ImGui::Begin("CPU / Disassembly"))
        {
            if (ImGui::Button(debugger.running ? "Pause" : "Continue"))
            {
                if (debugger.running)
                    debugger.pause();
                else
                    debugger.resume();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(debugger.running);
            if (ImGui::Button("Step"))
                debugger.protect([&]
                                 { debugger.one_step(); });
            ImGui::SameLine();
            if (ImGui::Button("Step over"))
                debugger.protect([&]
                                 { debugger.step_over(); });
            ImGui::SameLine();
            if (ImGui::Button("Run to cursor"))
            {
                debugger.target = debugger.cursor;
                debugger.target_stack.reset();
                debugger.resume();
            }
            ImGui::EndDisabled();
            auto &cpu = debugger.machine.cpu;
            ImGui::Text("PC %04X  A %02X  X %02X  Y %02X  SP %02X  P %02X",
                        cpu.pc, cpu.a, cpu.x, cpu.y, cpu.sp, cpu.p);
            ImGui::Text("N%d V%d - B%d D%d I%d Z%d C%d | %llu cycles",
                        !!(cpu.p & 128), !!(cpu.p & 64), !!(cpu.p & 16), !!(cpu.p & 8),
                        !!(cpu.p & 4), !!(cpu.p & 2), !!(cpu.p & 1), static_cast<unsigned long long>(cpu.cycles));
            ImGui::Checkbox("Follow PC", &debugger.follow_pc);
            ImGui::SameLine();
            hex16("Address", debugger.disassembly_address);
            ImGui::SameLine();
            hex16("Cursor", debugger.cursor);
            uint16_t address = debugger.follow_pc ? cpu.pc : debugger.disassembly_address;
            ImGui::BeginChild("Instructions");
            for (unsigned index = 0; index < 64; ++index)
            {
                const auto text = apple1::Cpu::disassemble(address,
                                                           [&](uint16_t location)
                                                           { return debugger.machine.peek(location); });
                bool breakpoint = false;
                for (const auto &stop : debugger.stops)
                    breakpoint |= stop.enabled && stop.execute && stop.address == address;
                char line[128];
                std::snprintf(line, sizeof(line), "%c%c %04X  %-18s", address == cpu.pc ? '>' : ' ',
                              breakpoint ? '*' : ' ', address, text.c_str());
                ImGui::PushID(index);
                if (ImGui::Selectable(line, address == debugger.cursor))
                    debugger.cursor = address;
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Add execute breakpoint"))
                        debugger.stops.push_back({true, address, true, false, false});
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                address = static_cast<uint16_t>(address + instruction_length(text));
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void stops_window(Debugger &debugger)
    {
        if (ImGui::Begin("Breakpoints"))
        {
            if (ImGui::Button("Add"))
                debugger.stops.push_back({true, debugger.cursor, true, false, false});
            if (ImGui::BeginTable("Stops", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX))
            {
                for (const char *title : {"On", "Address", "Exec", "Read", "Write", ""})
                    ImGui::TableSetupColumn(title);
                ImGui::TableHeadersRow();
                for (size_t index = 0; index < debugger.stops.size();)
                {
                    auto &stop = debugger.stops[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("##enabled", &stop.enabled);
                    ImGui::TableNextColumn();
                    hex16("##address", stop.address);
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("##exec", &stop.execute);
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("##read", &stop.read);
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("##write", &stop.write);
                    ImGui::TableNextColumn();
                    const bool remove = ImGui::SmallButton("X");
                    ImGui::PopID();
                    if (remove)
                        debugger.stops.erase(debugger.stops.begin() + index);
                    else
                        ++index;
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void memory_window(Debugger &debugger)
    {
        if (ImGui::Begin("Memory"))
        {
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("View", &debugger.memory_view,
                         "Mapped bus\0Raw ROM (32K)\0Raw monitor (256)\0Raw card RAM (32K)\0Raw motherboard RAM (8K)\0");
            if (debugger.memory_view == 0)
                hex16("Base address", debugger.memory_address);
            else
                hex16("Raw offset", debugger.raw_offset);
            ImGui::BeginDisabled(debugger.running);
            hex16("Edit address", debugger.edit_address);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(65);
            ImGui::InputScalar("Byte", ImGuiDataType_U8, &debugger.edit_value, nullptr, nullptr, "%02X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine();
            ImGui::BeginDisabled(!debugger.is_ram(debugger.edit_address));
            if (ImGui::Button("Write RAM"))
                debugger.machine.write(debugger.edit_address, debugger.edit_value);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(debugger.machine.owner(debugger.edit_address).c_str());
            const size_t size = debugger.memory_view == 0 ? 65536 : debugger.memory_view == 2 ? 256
                                                                : debugger.memory_view == 4   ? 8192
                                                                                              : 32768;
            const size_t offset = debugger.memory_view == 0 ? debugger.memory_address : std::min(static_cast<size_t>(debugger.raw_offset), size - 1);
            auto peek = [&](size_t address) -> uint8_t
            {
                switch (debugger.memory_view)
                {
                case 1:
                    return debugger.machine.rom[address];
                case 2:
                    return debugger.machine.monitor[address];
                case 3:
                    return debugger.machine.ram[address];
                case 4:
                    return debugger.machine.motherboard_ram[address];
                default:
                    return debugger.machine.peek(static_cast<uint16_t>(address));
                }
            };
            ImGui::BeginChild("Hex", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>((size - offset + 15) / 16));
            while (clipper.Step())
            {
                for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
                {
                    const size_t address = offset + size_t(line) * 16;
                    char text[100];
                    int used = std::snprintf(text, sizeof(text), "%04X  ", static_cast<unsigned>(address));
                    std::string ascii;
                    for (unsigned column = 0; column < 16; ++column)
                    {
                        if (address + column >= size)
                        {
                            used += std::snprintf(text + used, sizeof(text) - used, "   ");
                            continue;
                        }
                        const auto value = peek(address + column);
                        used += std::snprintf(text + used, sizeof(text) - used, "%02X ", value);
                        ascii += value >= 32 && value < 127 ? static_cast<char>(value) : '.';
                    }
                    std::snprintf(text + used, sizeof(text) - used, " %s", ascii.c_str());
                    ImGui::TextUnformatted(text);
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void map_window(Debugger &debugger)
    {
        if (ImGui::Begin("Memory Map"))
        {
            ImGui::BeginDisabled(debugger.running);
            ImGui::Checkbox("Motherboard $0000", &debugger.draft_low);
            ImGui::Checkbox("Motherboard $E000", &debugger.draft_high);
            ImGui::Checkbox("Monitor $FF00", &debugger.draft_monitor);
            const float table_height = std::max(80.0f,
                                                ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
            if (ImGui::BeginTable("Segments", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, table_height)))
            {
                ImGui::TableSetupColumn("Card range");
                ImGui::TableSetupColumn("Mapping");
                ImGui::TableHeadersRow();
                for (unsigned index = 0; index < 16; ++index)
                {
                    ImGui::PushID(index);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("$%04X-$%04X", index * 4096, index * 4096 + 4095);
                    ImGui::TableNextColumn();
                    int segment = static_cast<int>(debugger.draft_segments[index]);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##segment", &segment, "Disconnected\0RAM\0ROM\0"))
                        debugger.draft_segments[index] = static_cast<apple1::Segment>(segment);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Apply"))
                debugger.protect([&]
                                 { debugger.apply_mapping(); });
            ImGui::SameLine();
            if (ImGui::Button("Revert draft"))
            {
                debugger.draft_segments = debugger.machine.segments;
                debugger.draft_low = debugger.machine.motherboard_low;
                debugger.draft_high = debugger.machine.motherboard_high;
                debugger.draft_monitor = debugger.machine.motherboard_monitor;
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }

    void files_window(Debugger &debugger)
    {
        if (!debugger.show_files)
            return;
        static bool docked = false;
        if (!docked)
        {
            if (const auto *memory = ImGui::FindWindowByName("Memory"); memory && memory->DockId)
            {
                ImGui::SetNextWindowDockID(memory->DockId, ImGuiCond_FirstUseEver);
                docked = true;
            }
        }
        if (debugger.focus_files)
        {
            ImGui::SetNextWindowFocus();
            debugger.focus_files = false;
        }
        if (ImGui::Begin("Files", &debugger.show_files))
        {
            const auto view = apple1::inspect_ramfs(debugger.machine);
            if (!view.error.empty())
                ImGui::TextWrapped("%s", view.error.c_str());
            else
            {
                ImGui::Text("%zu / 48 slots   %zu bytes in files   12 KiB RAM", view.allocated_slots, view.bytes);
                if (ImGui::BeginTable("Directory", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                      ImVec2(0, std::max(64.0f, ImGui::GetContentRegionAvail().y * 0.4f))))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Bytes");
                    ImGui::TableSetupColumn("Address");
                    ImGui::TableSetupColumn("Storage slot");
                    ImGui::TableHeadersRow();
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("/");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("DIR");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("$A100");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("1 ($A100)");
                    for (const auto &file : view.files)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(file.slot);
                        const auto depth = std::count(file.path.begin(), file.path.end(), '/');
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + float(depth) * ImGui::GetTreeNodeToLabelSpacing());
                        const std::string label = file.name + (file.directory ? "/" : "");
                        if (ImGui::Selectable(label.c_str(), debugger.selected_file == file.slot,
                                              ImGuiSelectableFlags_SpanAllColumns))
                            debugger.selected_file = file.slot;
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", file.path.c_str());
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        if (file.directory)
                            ImGui::TextUnformatted("DIR");
                        else
                            ImGui::Text("%zu", file.data.size());
                        ImGui::TableNextColumn();
                        ImGui::Text("$%04X", 0xa000 + file.parent_slot * 256 + (file.slot % 16) * 16);
                        ImGui::TableNextColumn();
                        if (file.data_slot)
                            ImGui::Text("%d ($%04X)", file.data_slot, 0xa000 + file.data_slot * 256);
                        else
                            ImGui::TextUnformatted("None");
                    }
                    ImGui::EndTable();
                }
                const auto selected = std::find_if(view.files.begin(), view.files.end(),
                                                   [&](const auto &file)
                                                   { return file.slot == debugger.selected_file; });
                if (selected != view.files.end() && !selected->directory)
                {
                    ImGui::TextUnformatted(selected->path.c_str());
                    std::string blocks = "Blocks:";
                    for (int block : selected->blocks)
                        blocks += " " + std::to_string(block);
                    ImGui::TextWrapped("%s", blocks.c_str());
                    if (ImGui::BeginTabBar("File contents"))
                    {
                        if (ImGui::BeginTabItem("Text"))
                        {
                            ImGui::BeginChild("File text", ImVec2(0, 0), ImGuiChildFlags_None,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            std::string text;
                            for (auto value : selected->data)
                                text += value == 13 ? '\n' : value == 10 || (value >= 32 && value < 127) ? static_cast<char>(value)
                                                                                                         : '.';
                            ImGui::TextUnformatted(text.c_str());
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Hex"))
                        {
                            ImGui::BeginChild("File hex", ImVec2(0, 0), ImGuiChildFlags_None,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            for (size_t offset = 0; offset < selected->data.size(); offset += 16)
                            {
                                char line[80];
                                int used = std::snprintf(line, sizeof(line), "%04X  ", unsigned(offset));
                                for (size_t index = offset; index < std::min(offset + 16, selected->data.size()); ++index)
                                    used += std::snprintf(line + used, sizeof(line) - used, "%02X ", selected->data[index]);
                                ImGui::TextUnformatted(line);
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
            }
        }
        ImGui::End();
    }

    void input_window(Debugger &debugger)
    {
        static bool docked = false;
        if (!docked)
        {
            if (const auto *trace = ImGui::FindWindowByName("Trace"); trace && trace->DockId)
            {
                ImGui::SetNextWindowDockID(trace->DockId, ImGuiCond_FirstUseEver);
                docked = true;
            }
        }
        if (ImGui::Begin("Input Buffer"))
        {
            ImGui::Text("Queued: %zu   In device: %zu", debugger.input_buffer.size(),
                        debugger.machine.pending_input());
            if (ImGui::Button("Clear queue"))
                debugger.clear_input();
            ImGui::SameLine();
            ImGui::Checkbox("Append Return", &debugger.append_return);
            const float available = ImGui::GetContentRegionAvail().y;
            if (debugger.focus_input)
            {
                ImGui::SetKeyboardFocusHere();
                debugger.focus_input = false;
            }
            ImGui::InputTextMultiline("##input", debugger.input_draft.data(), debugger.input_draft.size(), ImVec2(-1, std::max(48.0f, available * 0.5f)), ImGuiInputTextFlags_CallbackAlways, [](ImGuiInputTextCallbackData *data)
                                      {
                                          auto &state = *static_cast<Debugger *>(data->UserData);
                                          if (state.set_input_cursor)
                                          {
                                              data->CursorPos = std::clamp(state.input_cursor, 0, data->BufTextLen);
                                              data->SelectionStart = data->SelectionEnd = data->CursorPos;
                                              state.set_input_cursor = false;
                                          }
                                          state.input_cursor = data->CursorPos;
                                          return 0; }, &debugger);
            ImGui::BeginDisabled(!debugger.input_buffer.empty() || debugger.machine.pending_input() != 0);
            if (ImGui::Button("Send line"))
            {
                debugger.send_line();
                debugger.focus_input = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Send") && debugger.input_draft[0])
            {
                debugger.queue_input(debugger.input_draft.data());
                if (debugger.append_return)
                    debugger.queue_input("\r");
                debugger.input_draft.fill(0);
                debugger.input_cursor = 0;
                debugger.set_input_cursor = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load demo"))
            {
                debugger.load_demo();
                debugger.focus_input = true;
            }
            ImGui::Separator();
            ImGui::BeginChild("Queued characters", ImVec2(0, 0), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar);
            std::string preview;
            const size_t count = std::min(debugger.input_buffer.size(), size_t(4096));
            for (size_t index = 0; index < count; ++index)
            {
                const unsigned char value = debugger.input_buffer[index];
                if (value == '\r')
                    preview += "<CR>\n";
                else if (value == '\x1b')
                    preview += "<ESC>";
                else if (value < 32)
                    preview += '.';
                else
                    preview += static_cast<char>(value);
            }
            ImGui::TextUnformatted(preview.c_str());
            if (debugger.input_buffer.size() > count)
                ImGui::Text("... %zu more bytes", debugger.input_buffer.size() - count);
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void inspectors(Debugger &debugger)
    {
        if (ImGui::Begin("PIA"))
        {
            for (uint16_t address = 0xd010; address <= 0xd013; ++address)
                ImGui::Text("$%04X  %02X", address, debugger.machine.peek(address));
            ImGui::Text("Keyboard control %02X  Display control %02X", debugger.machine.keyboard_control(),
                        debugger.machine.display_control());
            ImGui::Text("Queued input %zu  Cursor %u,%u", debugger.machine.pending_input(),
                        debugger.machine.column, debugger.machine.row);
            ImGui::Text("Machine ticks %llu", static_cast<unsigned long long>(debugger.machine.ticks()));
        }
        ImGui::End();
        if (ImGui::Begin("Trace"))
        {
            ImGui::Checkbox("Record instructions", &debugger.tracing);
            ImGui::SameLine();
            if (ImGui::Button("Clear trace"))
                debugger.trace.clear();
            ImGui::SameLine();
            ImGui::Text("%zu / 2048", debugger.trace.size());
            ImGui::BeginChild("Trace entries", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(debugger.trace.size()));
            while (clipper.Step())
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                    ImGui::TextUnformatted(debugger.trace[index].c_str());
            ImGui::EndChild();
        }
        ImGui::End();
        if (ImGui::Begin("Diagnostics"))
        {
            if (!debugger.error.empty())
            {
                ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", debugger.error.c_str());
                if (ImGui::Button("Dismiss"))
                    debugger.error.clear();
            }
            ImGui::TextWrapped("%s", debugger.status.c_str());
        }
        ImGui::End();
    }

    void capture(SDL_Window *window, const std::string &path, bool verify)
    {
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Empty GL drawable");
        std::vector<unsigned char> pixels(size_t(width) * height * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("GL framebuffer read failed");
        if (verify)
        {
            size_t distinct = 0, bright = 0;
            for (size_t index = 0; index < pixels.size(); index += 3)
            {
                distinct += pixels[index] != pixels[0] || pixels[index + 1] != pixels[1] || pixels[index + 2] != pixels[2];
                bright += pixels[index] > 100 || pixels[index + 1] > 100 || pixels[index + 2] > 100;
            }
            if (distinct < 1000 || bright < 1000)
                throw std::runtime_error("Blank GL framebuffer");
        }
        if (!path.empty())
        {
            std::ofstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Cannot create screenshot: " + path);
            stream << "P6\n"
                   << width << ' ' << height << "\n255\n";
            for (int row = height - 1; row >= 0; --row)
                stream.write(reinterpret_cast<const char *>(pixels.data() + size_t(row) * width * 3), width * 3);
            if (!stream)
                throw std::runtime_error("Screenshot write failed: " + path);
        }
    }

    int run(const Options &options)
    {
        Native native;
        native.start(options.smoke);
        Debugger debugger(options.profile);
        debugger.protect([&]
                         { debugger.machine.load_rom(options.rom); debugger.machine.reset(); });
        bool quit = false;
        unsigned frames = 0;
        uint64_t demo_sent_at = 0;
        auto previous = std::chrono::steady_clock::now();
        while (!quit)
        {
            std::vector<SDL_Event> keyboard;
            if (options.input_smoke && frames == 100)
            {
                SDL_Event text{};
                text.type = SDL_TEXTINPUT;
                text.text.windowID = SDL_GetWindowID(native.window);
                std::snprintf(text.text.text, sizeof(text.text.text), "%s", options.shell_rom() ? "MKDIR D\rECHO HI>D/N\rCAT D/N\r" : options.profile == apple1::Profile::Expansion ? "IPRINT 2+3\r"
                                                                                                                                                                                    : "0300: 42\r0300\r");
                keyboard.push_back(text);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT &&
                                               event.window.event == SDL_WINDOWEVENT_CLOSE &&
                                               event.window.windowID == SDL_GetWindowID(native.window)))
                    quit = true;
                if (event.type == SDL_TEXTINPUT || event.type == SDL_KEYDOWN)
                    keyboard.push_back(event);
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            if (options.input_smoke && frames >= 59 && frames <= 62)
            {
                auto &io = ImGui::GetIO();
                io.AddMousePosEvent(debugger.reset_button_center.x, debugger.reset_button_center.y);
                io.AddMouseButtonEvent(0, frames == 61);
            }
            ImGui::NewFrame();
            if (options.input_smoke && options.shell_rom() && frames == 150)
            {
                debugger.focus_files = true;
                debugger.selected_file = 32;
            }
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("Debug"))
                {
                    if (ImGui::MenuItem("Files", nullptr, &debugger.show_files) && debugger.show_files)
                        debugger.focus_files = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
            layout();
            screen_window(debugger);
            debugger.protect([&]
                             {
            cpu_window(debugger);
            stops_window(debugger);
            memory_window(debugger);
            map_window(debugger);
            inspectors(debugger);
            files_window(debugger);
            input_window(debugger); });
            if (options.demo_smoke && frames == 178)
                ImGui::SetWindowFocus("Input Buffer");
            if (options.input_smoke && frames == 65 &&
                (!debugger.running || !debugger.keyboard_focus ||
                 debugger.status != "Reset; running"))
                throw std::runtime_error("Reset state: running=" + std::to_string(debugger.running) +
                                         " focus=" + std::to_string(debugger.keyboard_focus) +
                                         " status=" + debugger.status);
            if (debugger.keyboard_focus && !ImGui::IsAnyItemActive() &&
                (SDL_GetWindowFlags(native.window) & SDL_WINDOW_INPUT_FOCUS))
            {
                for (const auto &key : keyboard)
                {
                    if (key.type == SDL_TEXTINPUT)
                        debugger.queue_input(key.text.text);
                    else if ((key.key.keysym.mod & KMOD_CTRL) && key.key.keysym.sym == SDLK_c)
                        debugger.queue_input(std::string(1, '\x03'));
                    else if (!(key.key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)))
                    {
                        switch (key.key.keysym.sym)
                        {
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            debugger.queue_input("\r");
                            break;
                        case SDLK_BACKSPACE:
                            debugger.queue_input("\b");
                            break;
                        case SDLK_ESCAPE:
                            debugger.queue_input(std::string(1, '\x1b'));
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
            if (options.demo_smoke && debugger.machine.ticks() > demo_sent_at + 100000 &&
                debugger.output.ends_with("> ") && debugger.send_line())
                demo_sent_at = debugger.machine.ticks();
            const auto now = std::chrono::steady_clock::now();
            debugger.protect([&]
                             { debugger.advance(std::chrono::duration<double>(now - previous).count(), options.smoke); });
            previous = now;
            ImGui::Render();
            int width, height;
            SDL_GL_GetDrawableSize(native.window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.035f, 0.045f, 0.045f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            ++frames;
            if (options.smoke && !debugger.error.empty())
                throw std::runtime_error(debugger.error);
            if ((options.smoke && frames == 180) || (!options.smoke && frames == 2 && !options.screenshot.empty()))
            {
                capture(native.window, options.screenshot, options.smoke);
                if (options.smoke)
                {
                    if (options.demo_smoke)
                    {
                        if (!debugger.input_buffer.empty() || debugger.machine.pending_input() ||
                            debugger.output.find("DEMO COMPLETE\r") == std::string::npos ||
                            debugger.output.find("ERROR") != std::string::npos)
                            throw std::runtime_error("GUI line-by-line demo failed: " + debugger.output);
                        const auto view = apple1::inspect_ramfs(debugger.machine);
                        if (!view.error.empty() || view.files.size() != 10)
                            throw std::runtime_error("Demo filesystem validation failed");
                        std::printf("DEMO PASS: preloaded editor sent one line at a time through the paced buffer\n");
                    }
                    if (options.input_smoke)
                    {
                        const std::string expected = options.shell_rom() ? "\rHI\r" : options.profile == apple1::Profile::Expansion ? "\r5\r"
                                                                                                                                    : "\r0300: 42";
                        if (debugger.output.find(expected) == std::string::npos ||
                            !debugger.input_buffer.empty())
                            throw std::runtime_error("Post-reset buffered typing failed: " + debugger.output);
                        if (options.shell_rom())
                        {
                            const auto view = apple1::inspect_ramfs(debugger.machine);
                            if (!view.error.empty() || view.files.size() != 2 || !view.files[0].directory ||
                                view.files[1].path != "/D/N" || view.files[1].data != std::vector<uint8_t>{'H', 'I', 13})
                                throw std::runtime_error("Shell directory viewer did not find /D/N");
                        }
                        std::printf("INPUT PASS: clicked Reset, restored focus, delivered SDL text, drained paced buffer\n");
                    }
                    if (debugger.machine.ticks() < 1000000 || debugger.output.empty())
                        throw std::runtime_error("Boot smoke failed: no emulated terminal output");
                    if (options.shell_rom() && debugger.output.find("A1OS\r") == std::string::npos)
                        throw std::runtime_error("OS ROM did not boot the shell");
                    if (options.profile == apple1::Profile::Expansion && options.rom == "roms/32KA1COMPIL.BIN")
                    {
                        std::string upper = debugger.output;
                        std::transform(upper.begin(), upper.end(), upper.begin(),
                                       [](unsigned char value)
                                       { return static_cast<char>(std::toupper(value)); });
                        if (upper.find("BASIC") == std::string::npos && upper.find("MENU") == std::string::npos)
                            throw std::runtime_error("Compilation ROM did not display a boot menu");
                    }
                    std::printf("SMOKE PASS: %u frames, %llu ticks, %zu terminal bytes, nonblank GL framebuffer\n",
                                frames, static_cast<unsigned long long>(debugger.machine.ticks()), debugger.output.size());
                    quit = true;
                }
            }
            SDL_GL_SwapWindow(native.window);
        }
        if (options.smoke && frames < 180)
            throw std::runtime_error("Smoke test interrupted before completion");
        return 0;
    }

}

int main(int argc, char **argv)
{
    try
    {
        return run(parse_options(argc, argv));
    }
    catch (const std::exception &exception)
    {
        std::fprintf(stderr, "apple1-gui: %s\n", exception.what());
        return 1;
    }
}
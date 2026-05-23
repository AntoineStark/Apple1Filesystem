#define GL_SILENCE_DEPRECATION
#include "gui_frontend.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>

GuiFrontend* GuiFrontend::s_instance = nullptr;

// ─── 6502 disassembler ────────────────────────────────────────────────────────
// Returns string like "LDA #$1A" and advances addr by instruction length
static std::string disasm_one(const uint8_t* mem, uint16_t& addr) {
    static const char* mnemonics[256] = {
        "BRK","ORA","KIL","ORA","NOP","ORA","ASL","ORA","PHP","ORA","ASL","NOP","NOP","ORA","ASL","NOP",
        "BPL","ORA","KIL","ORA","NOP","ORA","ASL","ORA","CLC","ORA","NOP","ORA","NOP","ORA","ASL","ORA",
        "JSR","AND","KIL","AND","BIT","AND","ROL","AND","PLP","AND","ROL","NOP","BIT","AND","ROL","AND",
        "BMI","AND","KIL","AND","NOP","AND","ROL","AND","SEC","AND","NOP","AND","NOP","AND","ROL","AND",
        "RTI","EOR","KIL","EOR","NOP","EOR","LSR","EOR","PHA","EOR","LSR","NOP","JMP","EOR","LSR","EOR",
        "BVC","EOR","KIL","EOR","NOP","EOR","LSR","EOR","CLI","EOR","NOP","EOR","NOP","EOR","LSR","EOR",
        "RTS","ADC","KIL","ADC","NOP","ADC","ROR","ADC","PLA","ADC","ROR","NOP","JMP","ADC","ROR","ADC",
        "BVS","ADC","KIL","ADC","NOP","ADC","ROR","ADC","SEI","ADC","NOP","ADC","NOP","ADC","ROR","ADC",
        "NOP","STA","NOP","STA","STY","STA","STX","STA","DEY","NOP","TXA","NOP","STY","STA","STX","STA",
        "BCC","STA","KIL","STA","STY","STA","STX","STA","TYA","STA","TXS","STA","NOP","STA","NOP","STA",
        "LDY","LDA","LDX","LDA","LDY","LDA","LDX","LDA","TAY","LDA","TAX","NOP","LDY","LDA","LDX","LDA",
        "BCS","LDA","KIL","LDA","LDY","LDA","LDX","LDA","CLV","LDA","TSX","LDA","LDY","LDA","LDX","LDA",
        "CPY","CMP","NOP","CMP","CPY","CMP","DEC","CMP","INY","CMP","DEX","NOP","CPY","CMP","DEC","CMP",
        "BNE","CMP","KIL","CMP","NOP","CMP","DEC","CMP","CLD","CMP","NOP","CMP","NOP","CMP","DEC","CMP",
        "CPX","SBC","NOP","SBC","CPX","SBC","INC","SBC","INX","SBC","NOP","NOP","CPX","SBC","INC","SBC",
        "BEQ","SBC","KIL","SBC","NOP","SBC","INC","SBC","SED","SBC","NOP","SBC","NOP","SBC","INC","SBC",
    };
    // Addressing mode: 0=imp, 1=imm, 2=zp, 3=zpx, 4=zpy, 5=abs, 6=absx, 7=absy,
    //                  8=ind, 9=indx, 10=indy, 11=rel, 12=acc
    static const uint8_t mode[256] = {
        0, 9, 0, 9, 2, 2, 2, 2, 0, 1, 12,1, 5, 5, 5, 5,  // 00
        11,10, 0,10, 3, 3, 3, 3, 0, 7, 0, 7, 6, 6, 6, 6,  // 10
        5, 9, 0, 9, 2, 2, 2, 2, 0, 1, 12,1, 5, 5, 5, 5,   // 20
        11,10, 0,10, 3, 3, 3, 3, 0, 7, 0, 7, 6, 6, 6, 6,  // 30
        0, 9, 0, 9, 2, 2, 2, 2, 0, 1, 12,1, 5, 5, 5, 5,   // 40
        11,10, 0,10, 3, 3, 3, 3, 0, 7, 0, 7, 6, 6, 6, 6,  // 50
        0, 9, 0, 9, 2, 2, 2, 2, 0, 1, 12,1, 8, 5, 5, 5,   // 60
        11,10, 0,10, 3, 3, 3, 3, 0, 7, 0, 7, 6, 6, 6, 6,  // 70
        1, 9, 1, 9, 2, 2, 2, 2, 0, 1, 0, 1, 5, 5, 5, 5,   // 80
        11,10, 0,10, 3, 3, 4, 4, 0, 7, 0, 7, 6, 6, 7, 7,  // 90
        1, 9, 1, 9, 2, 2, 2, 2, 0, 1, 0, 1, 5, 5, 5, 5,   // A0
        11,10, 0,10, 3, 3, 4, 4, 0, 7, 0, 7, 6, 6, 7, 7,  // B0
        1, 9, 1, 9, 2, 2, 2, 2, 0, 1, 0, 1, 5, 5, 5, 5,   // C0
        11,10, 0,10, 3, 3, 3, 3, 0, 7, 0, 7, 6, 6, 6, 6,  // D0
        1, 9, 1, 9, 2, 2, 2, 2, 0, 1, 0, 1, 5, 5, 5, 5,   // E0
        11,10, 0,10, 3, 3, 3, 3, 0, 7, 0, 7, 6, 6, 6, 6,  // F0
    };

    uint16_t orig = addr;
    uint8_t op = mem[addr++];
    char buf[64];
    uint8_t lo, hi;
    int16_t rel;
    switch (mode[op]) {
        case 0:  snprintf(buf, sizeof(buf), "%s", mnemonics[op]); break;
        case 1:  lo=mem[addr++]; snprintf(buf,sizeof(buf),"%s #$%02X",mnemonics[op],lo); break;
        case 2:  lo=mem[addr++]; snprintf(buf,sizeof(buf),"%s $%02X",mnemonics[op],lo); break;
        case 3:  lo=mem[addr++]; snprintf(buf,sizeof(buf),"%s $%02X,X",mnemonics[op],lo); break;
        case 4:  lo=mem[addr++]; snprintf(buf,sizeof(buf),"%s $%02X,Y",mnemonics[op],lo); break;
        case 5:  lo=mem[addr++]; hi=mem[addr++]; snprintf(buf,sizeof(buf),"%s $%02X%02X",mnemonics[op],hi,lo); break;
        case 6:  lo=mem[addr++]; hi=mem[addr++]; snprintf(buf,sizeof(buf),"%s $%02X%02X,X",mnemonics[op],hi,lo); break;
        case 7:  lo=mem[addr++]; hi=mem[addr++]; snprintf(buf,sizeof(buf),"%s $%02X%02X,Y",mnemonics[op],hi,lo); break;
        case 8:  lo=mem[addr++]; hi=mem[addr++]; snprintf(buf,sizeof(buf),"%s ($%02X%02X)",mnemonics[op],hi,lo); break;
        case 9:  lo=mem[addr++]; snprintf(buf,sizeof(buf),"%s ($%02X,X)",mnemonics[op],lo); break;
        case 10: lo=mem[addr++]; snprintf(buf,sizeof(buf),"%s ($%02X),Y",mnemonics[op],lo); break;
        case 11: {
            rel=(int8_t)mem[addr++];
            uint16_t target=(uint16_t)(addr+rel);
            snprintf(buf,sizeof(buf),"%s $%04X",mnemonics[op],target);
            break;
        }
        case 12: snprintf(buf,sizeof(buf),"%s A",mnemonics[op]); break;
        default: snprintf(buf,sizeof(buf),"???"); break;
    }
    (void)orig;
    return buf;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
GuiFrontend::GuiFrontend(Apple1& machine) : machine_(machine) {
    s_instance = this;
    machine_.on_output = [this](char c){ handle_output(c); };
    screen_lines_.push_back("");
    init_gl();
}

GuiFrontend::~GuiFrontend() {
    shutdown_gl();
    s_instance = nullptr;
}

// ─── OpenGL / ImGui init ──────────────────────────────────────────────────────
void GuiFrontend::init_gl() {
    glfwSetErrorCallback([](int, const char* msg){ fprintf(stderr, "GLFW: %s\n", msg); });
    if (!glfwInit()) { fprintf(stderr, "GLFW init failed\n"); return; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    auto* win = glfwCreateWindow(1200, 800, "Apple 1 Emulator", nullptr, nullptr);
    if (!win) { fprintf(stderr, "GLFW window creation failed\n"); return; }
    window_ = win;

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1); // vsync

    glfwSetWindowUserPointer(win, this);
    glfwSetKeyCallback(win, [](GLFWwindow* w, int k, int sc, int a, int m){
        auto* self = static_cast<GuiFrontend*>(glfwGetWindowUserPointer(w));
        if (self) self->handle_key_callback(k, sc, a, m);
    });
    // Character callback for printable characters
    glfwSetCharCallback(win, [](GLFWwindow* w, unsigned int codepoint){
        auto* self = static_cast<GuiFrontend*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        // Only pass ASCII range to the Apple 1 (it's uppercase-only)
        if (codepoint < 128) self->machine_.send_key((char)codepoint);
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style: dark, Apple 1 inspired (green on black)
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void GuiFrontend::shutdown_gl() {
    if (!window_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(static_cast<GLFWwindow*>(window_));
    glfwTerminate();
    window_ = nullptr;
}

// ─── Output handler ───────────────────────────────────────────────────────────
void GuiFrontend::handle_output(char c) {
    c &= 0x7F;
    if (c == 0x0D || c == 0x0A) {
        screen_lines_.push_back("");
        if ((int)screen_lines_.size() > ROWS * 4) screen_lines_.pop_front();
    } else if (c >= 0x20 && c < 0x7F) {
        if (screen_lines_.empty()) screen_lines_.push_back("");
        screen_lines_.back() += c;
    } else if (c == 0x08) {
        if (!screen_lines_.empty() && !screen_lines_.back().empty())
            screen_lines_.back().pop_back();
    }
}

// ─── Key input ────────────────────────────────────────────────────────────────
void GuiFrontend::handle_key_callback(int key, int /*sc*/, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    // Keys handled here: Enter, Backspace, Escape, Ctrl-C, F-keys
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
        machine_.send_key('\r');
    else if (key == GLFW_KEY_BACKSPACE)
        machine_.send_key(0x08);
    else if (key == GLFW_KEY_ESCAPE)
        machine_.send_key(0x1B);
    else if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL))
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(window_), GLFW_TRUE);
    else if (key == GLFW_KEY_F5)
        running_ = !running_;
    else if (key == GLFW_KEY_F6)
        step_once_ = true;
    else if (key == GLFW_KEY_F7) {
        fake6502::reset();
        screen_lines_.clear(); screen_lines_.push_back("");
    }
}

// ─── Main loop ────────────────────────────────────────────────────────────────
void GuiFrontend::run() {
    if (!window_) return;
    auto* win = static_cast<GLFWwindow*>(window_);

    using clock = std::chrono::steady_clock;
    auto prev = clock::now();

    while (!glfwWindowShouldClose(win)) {
        auto now = clock::now();
        double dt = std::chrono::duration<double>(now - prev).count();
        prev = now;

        glfwPollEvents();
        draw_frame(dt);
    }
}

// ─── Frame render ─────────────────────────────────────────────────────────────
void GuiFrontend::draw_frame(double dt) {
    // ── Emulate CPU ──
    if (running_ || step_once_) {
        const fake6502::State& st = machine_.cpu_state();
        bool hit_bp = false;

        // Check if current PC is a breakpoint
        if (running_) {
            uint16_t cur_pc = st.pc;
            for (auto bp : breakpoints_) {
                if (bp == cur_pc) { running_ = false; hit_bp = true; break; }
            }
        }

        if (!hit_bp) {
            if (step_once_) {
                machine_.step();
                step_once_ = false;
            } else if (speed_mhz_ <= 0.0) {
                // Unlimited: run a batch
                for (int i = 0; i < 10000 && !machine_.cpu_state().stopped; ++i)
                    machine_.step();
            } else {
                uint64_t us = (uint64_t)(dt * 1e6);
                if (us > 20000) us = 20000; // cap to prevent spiral
                machine_.run_us(us);
            }
        }
    }

    // ── ImGui frame ──
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    cursor_timer_ += dt;
    if (cursor_timer_ > 0.5) { cursor_blink_ = !cursor_blink_; cursor_timer_ = 0; }

    draw_terminal();
    draw_cpu_state();
    draw_hex_viewer();
    draw_control();

    ImGui::Render();

    int w, h;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(window_), &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(static_cast<GLFWwindow*>(window_));
}

// ─── Terminal window ──────────────────────────────────────────────────────────
void GuiFrontend::draw_terminal() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
    ImGui::Begin("Apple 1 Terminal");

    // Green-on-black style for the terminal text area
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0.05f, 0, 1));
    ImGui::BeginChild("##screen", ImVec2(0, -30), false, ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

    for (const auto& line : screen_lines_) {
        ImGui::TextUnformatted(line.c_str());
    }
    // Cursor on last line
    if (cursor_blink_) {
        ImGui::SameLine(0, 0);
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 0.7f), "_");
    }
    ImGui::SetScrollHereY(1.0f); // auto-scroll to bottom

    ImGui::PopStyleColor(); // text
    ImGui::EndChild();
    ImGui::PopStyleColor(); // childbg

    // Status bar
    const auto& s = machine_.cpu_state();
    bool paused = !running_;
    ImGui::TextDisabled("PC:%04X  %s  Bank:%d  Cycles:%llu",
        s.pc, paused ? "[PAUSED]" : "[RUNNING]",
        machine_.current_bank(), (unsigned long long)s.clockticks);

    ImGui::End();
}

// ─── CPU State window ─────────────────────────────────────────────────────────
void GuiFrontend::draw_cpu_state() {
    ImGui::SetNextWindowPos(ImVec2(580, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("CPU State");

    const auto& s = machine_.cpu_state();

    ImGui::Text("PC  $%04X    SP  $%02X", s.pc, s.sp);
    ImGui::Text("A   $%02X      X   $%02X    Y   $%02X", s.a, s.x, s.y);
    ImGui::Separator();

    // Flags
    auto flag = [&](const char* name, uint8_t bit) {
        bool set = (s.status & bit) != 0;
        if (set) ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "%s", name);
        else     ImGui::TextDisabled("%s", name);
        ImGui::SameLine();
    };
    flag("N", fake6502::FLAG_SIGN);
    flag("V", fake6502::FLAG_OVERFLOW);
    flag("-", fake6502::FLAG_CONSTANT);
    flag("B", fake6502::FLAG_BREAK);
    flag("D", fake6502::FLAG_DECIMAL);
    flag("I", fake6502::FLAG_INTERRUPT);
    flag("Z", fake6502::FLAG_ZERO);
    flag("C", fake6502::FLAG_CARRY);
    ImGui::NewLine();

    ImGui::Separator();
    ImGui::Text("Cycles:  %llu", (unsigned long long)s.clockticks);
    ImGui::Text("Bank:    %d", machine_.current_bank());
    ImGui::Text("Speed:   %.2f MHz", speed_mhz_ <= 0 ? 999.0 : speed_mhz_);

    ImGui::End();
}

// ─── Hex Viewer window ────────────────────────────────────────────────────────
void GuiFrontend::draw_hex_viewer() {
    ImGui::SetNextWindowPos(ImVec2(580, 240), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(610, 460), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory / Disassembler");

    // Controls
    ImGui::Text("Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char addr_buf[8];
    snprintf(addr_buf, sizeof(addr_buf), "%04X", hex_base_addr_);
    if (ImGui::InputText("##addr", addr_buf, sizeof(addr_buf),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        hex_base_addr_ = (uint16_t)std::stoul(addr_buf, nullptr, 16);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow PC", &hex_follow_pc_);
    ImGui::SameLine();
    ImGui::Checkbox("Disasm", &hex_disasm_);

    if (hex_follow_pc_)
        hex_base_addr_ = machine_.cpu_state().pc & 0xFFF0;

    ImGui::Separator();

    // Build a flat read-back of the address space
    // (only accessible memory: just read via bus)
    auto read_mem = [&](uint16_t a) -> uint8_t {
        return machine_.bus().read(a);
    };

    if (hex_disasm_) {
        // Disassembly view
        // Snapshot up to 256 bytes for disassembly
        uint8_t snapshot[256];
        for (int i = 0; i < 256; ++i)
            snapshot[i] = read_mem((uint16_t)(hex_base_addr_ + i));

        ImGui::BeginChild("##disasm", ImVec2(0, 0), false);
        uint16_t cur = hex_base_addr_;
        uint16_t pc  = machine_.cpu_state().pc;
        while (cur < hex_base_addr_ + 250) {
            uint16_t start = cur;
            uint16_t local = cur - hex_base_addr_;
            std::string mnemonic = disasm_one(snapshot, local);
            cur = hex_base_addr_ + local;

            // Bytes
            char bytes_str[16]{};
            int instr_len = cur - start;
            for (int j = 0; j < instr_len && j < 3; ++j)
                snprintf(bytes_str + j*3, sizeof(bytes_str)-j*3, "%02X ", read_mem(start+j));

            bool is_pc = (start == pc);
            if (is_pc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,0,1));
            ImGui::Text("%c %04X  %-9s  %s",
                is_pc ? '>' : ' ', start, bytes_str, mnemonic.c_str());
            if (is_pc) ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    } else {
        // Hex dump view (16 bytes per row)
        ImGui::BeginChild("##hex", ImVec2(0, 0), false);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetIO().Fonts->Fonts[0]);
        for (int row = 0; row < 32; ++row) {
            uint16_t base = (uint16_t)(hex_base_addr_ + row * 16);
            ImGui::Text("%04X  ", base);
            ImGui::SameLine();
            for (int col = 0; col < 16; ++col) {
                uint16_t a = (uint16_t)(base + col);
                uint8_t  v = read_mem(a);
                if (a == machine_.cpu_state().pc)
                    ImGui::TextColored(ImVec4(1,1,0,1), "%02X", v);
                else
                    ImGui::Text("%02X", v);
                ImGui::SameLine(0, 4);
            }
            ImGui::SameLine();
            ImGui::TextDisabled(" ");
            ImGui::SameLine();
            for (int col = 0; col < 16; ++col) {
                uint16_t a = (uint16_t)(base + col);
                char c = (char)(read_mem(a) & 0x7F);
                if (c < 0x20 || c >= 0x7F) c = '.';
                char s[2] = {c, 0};
                ImGui::Text("%s", s); ImGui::SameLine(0, 0);
            }
            ImGui::NewLine();
        }
        ImGui::PopFont();
        ImGui::EndChild();
    }
    ImGui::End();
}

// ─── Control window ───────────────────────────────────────────────────────────
void GuiFrontend::draw_control() {
    ImGui::SetNextWindowPos(ImVec2(580, 710), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(610, 80), ImGuiCond_FirstUseEver);
    ImGui::Begin("Control");

    if (ImGui::Button(running_ ? "Pause  [F5]" : "Resume [F5]"))
        running_ = !running_;
    ImGui::SameLine();
    if (ImGui::Button("Step [F6]")) step_once_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset [F7]")) {
        fake6502::reset();
        screen_lines_.clear(); screen_lines_.push_back("");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    float spd = (float)speed_mhz_;
    if (ImGui::SliderFloat("MHz (0=max)", &spd, 0.0f, 4.0f))
        speed_mhz_ = spd;

    // Breakpoints
    ImGui::Separator();
    ImGui::Text("Breakpoints:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputText("##bp", bp_input_buf_, sizeof(bp_input_buf_),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        uint16_t addr = (uint16_t)std::stoul(bp_input_buf_, nullptr, 16);
        if (std::find(breakpoints_.begin(), breakpoints_.end(), addr) == breakpoints_.end())
            breakpoints_.push_back(addr);
        memset(bp_input_buf_, 0, sizeof(bp_input_buf_));
    }
    for (size_t i = 0; i < breakpoints_.size(); ) {
        ImGui::SameLine();
        char lbl[16]; snprintf(lbl, sizeof(lbl), "$%04X x##%zu", breakpoints_[i], i);
        if (ImGui::SmallButton(lbl)) breakpoints_.erase(breakpoints_.begin() + i);
        else ++i;
    }

    ImGui::End();
}

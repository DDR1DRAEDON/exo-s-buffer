#pragma once
// Native Win32 runtime diagnostics. Never changes the game's stdout/stderr or Geode logger.
// Opened when Geode loads this mod; all output is also saved to a durable local file.
#ifndef _WIN32
#error "DebugConsole targets Win64 only"
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>

namespace pb::diagnostic {

class Console final {
    std::mutex mutex_;
    HANDLE output_ = INVALID_HANDLE_VALUE;
    HWND window_ = nullptr;
    FILE* logfile_ = nullptr;
    bool started_ = false;
    bool ownsWindow_ = false;
    bool visible_ = true;
    bool trace_ = false;
    std::wstring path_;

    void lineUnlocked(const char* category, const char* message) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char line[2350]{};
        std::snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] [PB5] [%s] %s\r\n",
                      now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                      category ? category : "INFO", message ? message : "");
        if (output_ != INVALID_HANDLE_VALUE && output_ != nullptr) {
            DWORD written = 0;
            WriteFile(output_, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
        }
        if (logfile_) {
            std::fwrite(line, 1, std::strlen(line), logfile_);
            std::fflush(logfile_);
        }
    }

public:
    Console() = default;
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;
    ~Console() {
        if (logfile_) std::fclose(logfile_);
        // Do not FreeConsole() here: Geode may still use the console at shutdown.
        if (output_ != INVALID_HANDLE_VALUE && output_) CloseHandle(output_);
    }

    static Console& instance() {
        static Console singleton;
        return singleton;
    }
    void init() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) return;
        started_ = true;
        ownsWindow_ = GetConsoleWindow() == nullptr && AllocConsole() != FALSE;
        window_ = GetConsoleWindow();
        if (window_) {
            SetConsoleTitleW(L"Predictive Buffer V5 - Live Diagnostics (F7 trace / F8 console / F9 stats / F10 raw)");
            SetConsoleOutputCP(CP_UTF8);
            output_ = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode{};
            if (input && input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode)) {
                SetConsoleMode(input, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
            }
        }
        std::array<wchar_t, 32768> base{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base.data(), static_cast<DWORD>(base.size()));
        if (length != 0 && length < base.size()) {
            path_ = std::wstring(base.data()) + L"\\PredictiveBufferV5";
            if (CreateDirectoryW(path_.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
                path_ += L"\\debug.log";
                _wfopen_s(&logfile_, path_.c_str(), L"ab");
            }
        }
        lineUnlocked("BOOT", "Native Windows console initialized; trace=OFF (F7), toggle console=F8, stats=F9");
        if (logfile_) lineUnlocked("BOOT", "Logging to %LOCALAPPDATA%\\PredictiveBufferV5\\debug.log");
        else lineUnlocked("WARN", "Unable to open debug.log; console output only");
    }

    bool trace() const { return trace_; }
    void toggleTrace() {
        trace_ = !trace_;
        write("CONFIG", trace_ ? "Verbose INPUT/EMIT tracing ON" : "Verbose INPUT/EMIT tracing OFF");
    }
    void toggleVisible() {
        if (!ownsWindow_ || !window_) {
            write("WARN", "F8 unavailable: console belongs to an external process");
            return;
        }
        visible_ = !visible_;
        ShowWindow(window_, visible_ ? SW_SHOW : SW_HIDE);
        write("CONFIG", visible_ ? "Console visible" : "Console hidden (log file still active)");
    }
    void write(const char* category, const char* message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) return;
        lineUnlocked(category, message);
    }
    void writef(const char* category, const char* format, ...) {
        char buffer[1700]{};
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        write(category, buffer);
    }
};

inline Console& console() { return Console::instance(); }

} // namespace pb::diagnostic

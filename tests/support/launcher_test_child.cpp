// Deterministic child process used by the launcher unit tests.
//
// This is a test fixture, not a test and not part of WinIOS itself. It exists so
// the launcher can be exercised (stdout/stderr capture, exit codes, timeouts,
// output bounding, argument quoting and the child-only environment) without
// depending on a locally installed Inferno build.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <windows.h>

namespace {
std::string to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string environment_value(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(written);
    return to_utf8(value);
}
} // namespace

int main(int argc, char* argv[]) {
    // Binary mode so captured output has predictable LF line endings regardless
    // of whether stdout is a pipe, a file or a console.
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);

    const std::string mode = argc > 1 ? argv[1] : "ok";

    if (mode == "ok") {
        std::printf("child-ok\n");
        return 0;
    }

    if (mode == "echo") {
        for (int index = 2; index < argc; ++index) {
            std::printf("arg%02d=[%s]\n", index, argv[index]);
        }
        std::fflush(stdout);
        return 0;
    }

    if (mode == "fail") {
        std::fprintf(stderr, "child failure text\n");
        std::fflush(stderr);
        return 3;
    }

    if (mode == "sleep") {
        const int milliseconds = argc > 2 ? std::atoi(argv[2]) : 5000;
        std::printf("child sleeping\n");
        std::fflush(stdout);
        Sleep(static_cast<DWORD>(milliseconds));
        std::printf("child slept\n");
        return 0;
    }

    if (mode == "spam") {
        const int total = argc > 2 ? std::atoi(argv[2]) : 100000;
        std::string block(64, 'x');
        block.push_back('\n');
        int written = 0;
        while (written < total) {
            std::fwrite(block.data(), 1, block.size(), stdout);
            written += static_cast<int>(block.size());
        }
        std::fflush(stdout);
        return 0;
    }

    if (mode == "spamerr") {
        const int total = argc > 2 ? std::atoi(argv[2]) : 100000;
        std::string block(64, 'e');
        block.push_back('\n');
        int written = 0;
        while (written < total) {
            std::fwrite(block.data(), 1, block.size(), stderr);
            written += static_cast<int>(block.size());
        }
        std::fflush(stderr);
        return 0;
    }

    if (mode == "cwd") {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetCurrentDirectoryW(
                static_cast<DWORD>(buffer.size()), buffer.data());
            if (length == 0) {
                return 4;
            }
            if (length < buffer.size()) {
                buffer.resize(length);
                break;
            }
            buffer.resize(length + 1);
        }
        std::printf("cwd=%s\n", to_utf8(buffer).c_str());
        std::fflush(stdout);
        return 0;
    }

    if (mode == "path") {
        std::printf("path=%s\n", environment_value(L"PATH").c_str());
        std::fflush(stdout);
        return 0;
    }

    std::fprintf(stderr, "unknown child mode: %s\n", mode.c_str());
    return 5;
}

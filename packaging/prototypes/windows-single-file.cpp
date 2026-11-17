// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <process.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <stdexcept>

namespace fs = std::filesystem;

// Each argument is quoted for the Microsoft CRT, including trailing backslashes.
static std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

static int run(const fs::path &program, const std::vector<std::wstring> &args) {
    auto command = quote(program.wstring());
    for (const auto &arg : args) command += L" " + quote(arg);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &child))
        throw std::runtime_error("cannot start bundled program");
    CloseHandle(child.hThread);
    DWORD status = 126;
    if (WaitForSingleObject(child.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(child.hProcess, &status);
    CloseHandle(child.hProcess);
    return static_cast<int>(status);
}

int wmain(int argc, wchar_t **argv) {
    try {
        std::wstring ownPath(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, ownPath.data(), static_cast<DWORD>(ownPath.size()));
        if (!length || length == ownPath.size()) throw std::runtime_error("cannot locate launcher");
        ownPath.resize(length);
        std::ifstream self(fs::path(ownPath), std::ios::binary | std::ios::ate);
        const auto end = self.tellg();
        if (end < 80) throw std::runtime_error("missing package payload");
        self.seekg(end - std::streamoff(80));
        std::string footer(80, '\0');
        self.read(footer.data(), footer.size());
        const auto digest = footer.substr(0, 64);
        const auto sizeText = footer.substr(64);
        if (digest.find_first_not_of("0123456789abcdef") != std::string::npos ||
            sizeText.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("invalid package footer");
        const auto size = std::stoll(sizeText);
        if (size <= 0 || size > end - std::streamoff(80)) throw std::runtime_error("invalid payload length");
        const auto *local = _wgetenv(L"LOCALAPPDATA");
        if (!local || !*local) throw std::runtime_error("LOCALAPPDATA is unavailable");
        const auto root = fs::path(local) / L"CPMImage" / L"packages";
        const auto target = root / digest;
        fs::create_directories(root);
        if (!fs::is_directory(target)) {
            const auto staging = root / (L".extract-" + std::to_wstring(GetCurrentProcessId()) +
                                         L"-" + std::to_wstring(GetTickCount64()));
            if (!fs::create_directory(staging)) throw std::runtime_error("cannot create staging directory");
            try {
                const auto payload = staging / L"payload.tar.gz";
                const auto files = staging / L"files";
                fs::create_directory(files);
                std::ofstream output(payload, std::ios::binary);
                self.seekg(end - std::streamoff(80) - std::streamoff(size));
                std::array<char, 65536> buffer;
                for (long long remaining = size; remaining > 0;) {
                    const auto count = static_cast<std::streamsize>(remaining < buffer.size() ? remaining : buffer.size());
                    if (!self.read(buffer.data(), count) || !output.write(buffer.data(), count))
                        throw std::runtime_error("cannot stage package payload");
                    remaining -= count;
                }
                output.close();
                if (!output) throw std::runtime_error("cannot close package payload");
                wchar_t system[MAX_PATH];
                if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("cannot locate Windows system directory");
                if (run(fs::path(system) / L"tar.exe", {L"-xzf", payload.wstring(), L"-C", files.wstring()}) != 0 ||
                    !fs::is_regular_file(files / L"cpmwb.exe"))
                    throw std::runtime_error("cannot extract bundled application");
                std::error_code error;
                fs::rename(files, target, error);
                if (error && !fs::is_directory(target)) throw std::runtime_error("cannot publish extracted files");
                fs::remove_all(staging, error);
            } catch (...) {
                std::error_code ignored;
                fs::remove_all(staging, ignored);
                throw;
            }
        }
        return run(target / L"cpmwb.exe", {argv + 1, argv + argc});
    } catch (const std::exception &error) {
        std::cerr << "cpmimage: " << error.what() << '\n';
        return 126;
    }
}
